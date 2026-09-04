#include "WorkTimeCalculator.h"

#include <algorithm>

namespace {

int overlapMinutes(const QTime& rangeStart,
                   const QTime& rangeEnd,
                   const QTime& breakStart,
                   const QTime& breakEnd)
{
    const QTime start = std::max(rangeStart, breakStart);
    const QTime end = std::min(rangeEnd, breakEnd);
    return start < end ? start.secsTo(end) / 60 : 0;
}

}

WorkTimeResult WorkTimeCalculator::calculateWorkTimeResult(
    const AttendanceRecord& record,
    const WorkSchedule& schedule)
{
    WorkTimeResult result;
    if (record.arrivalTime >= record.departureTime
        || schedule.workStartTime >= schedule.workEndTime) {
        return result;
    }

    if (record.arrivalTime > schedule.workStartTime) {
        result.lateMinutes = schedule.workStartTime.secsTo(record.arrivalTime) / 60;
    }
    if (record.departureTime < schedule.workEndTime) {
        result.earlyLeaveMinutes = record.departureTime.secsTo(schedule.workEndTime) / 60;
    }

    if (schedule.lunchBreakEnabled) {
        result.totalBreakMinutes = overlapMinutes(
            record.arrivalTime, record.departureTime,
            schedule.lunchBreakStart, schedule.lunchBreakEnd);
    }
    if (schedule.dinnerBreakEnabled) {
        result.totalBreakMinutes += overlapMinutes(
            record.arrivalTime, record.departureTime,
            schedule.dinnerBreakStart, schedule.dinnerBreakEnd);
    }

    const int totalMinutesAtWork = record.arrivalTime.secsTo(record.departureTime) / 60;
    result.actualWorkMinutes = std::max(0, totalMinutesAtWork - result.totalBreakMinutes);

    int standardBreakMinutes = 0;
    if (schedule.lunchBreakEnabled) {
        standardBreakMinutes = overlapMinutes(
            schedule.workStartTime, schedule.workEndTime,
            schedule.lunchBreakStart, schedule.lunchBreakEnd);
    }
    if (schedule.dinnerBreakEnabled) {
        standardBreakMinutes += overlapMinutes(
            schedule.workStartTime, schedule.workEndTime,
            schedule.dinnerBreakStart, schedule.dinnerBreakEnd);
    }

    const int standardTotalMinutes = schedule.workStartTime.secsTo(schedule.workEndTime) / 60;
    result.standardWorkMinutes = std::max(0, standardTotalMinutes - standardBreakMinutes);
    result.overtimeMinutes = result.actualWorkMinutes - result.standardWorkMinutes;
    return result;
}
