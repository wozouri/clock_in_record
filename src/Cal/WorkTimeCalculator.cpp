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
    const WorkSchedule& globalSchedule)
{
    const WorkSchedule& schedule = record.hasCustomSchedule ? record.customSchedule : globalSchedule;
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
    if (!record.needAverageCal) {
        if (!record.excludeStandardOvertime) {
            result.overtimeMinutes = result.actualWorkMinutes;
        } else {
            const QTime insideStart = std::max(record.arrivalTime, schedule.workStartTime);
            const QTime insideEnd = std::min(record.departureTime, schedule.workEndTime);
            int insideBreak = 0;
            if (insideStart < insideEnd) {
                if (schedule.lunchBreakEnabled) insideBreak += overlapMinutes(insideStart, insideEnd, schedule.lunchBreakStart, schedule.lunchBreakEnd);
                if (schedule.dinnerBreakEnabled) insideBreak += overlapMinutes(insideStart, insideEnd, schedule.dinnerBreakStart, schedule.dinnerBreakEnd);
            }
            const int insideWork = overlapMinutes(record.arrivalTime, record.departureTime,
                schedule.workStartTime, schedule.workEndTime) - insideBreak;
            result.overtimeMinutes = std::max(0, result.actualWorkMinutes - insideWork);
        }
    }
    return result;
}
