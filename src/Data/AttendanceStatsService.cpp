#include "AttendanceStatsService.h"
#include "Cal/WorkTimeCalculator.h"
#include "Data/AttendanceStorage.h"

MonthlyAttendanceSnapshot AttendanceStatsService::buildMonthlySnapshot(int year, int month) {
    MonthlyAttendanceSnapshot snapshot;
    snapshot.year = year;
    snapshot.month = month;

    const QDate startDate(year, month, 1);
    const QDate endDate = startDate.addMonths(1).addDays(-1);

    QDate date = startDate;
    while (date <= endDate) {
        AttendanceDayView dayView;
        dayView.hasRecord = AttendanceStorage::hasArrivalRecord(date);

        if (dayView.hasRecord) {
            const AttendanceRecord record = AttendanceStorage::loadRecord(date);
            const WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(record);

            dayView.needAverageCal = record.needAverageCal;
            dayView.arrivalText = record.arrivalTime.toString("hh:mm");
            dayView.departureText = record.departureTime.toString("hh:mm");

            snapshot.workDays++;
            if (!record.needAverageCal) {
                snapshot.workDays--;
            }

            if (result.overtimeMinutes > 0) {
                snapshot.totalOvertimeMinutes += result.overtimeMinutes;
            }
            snapshot.totalLateMinutes += result.lateMinutes;
            snapshot.totalEarlyLeaveMinutes += result.earlyLeaveMinutes;
        }

        snapshot.dayViews.insert(date, dayView);
        date = date.addDays(1);
    }

    return snapshot;
}
