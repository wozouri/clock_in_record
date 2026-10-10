#include "AttendanceStatsService.h"
#include "Cal/WorkTimeCalculator.h"
#include "Data/AttendanceStorage.h"

QString AttendanceStatsService::describeOvertimeTargetGap(
    const MonthlyAttendanceSnapshot& snapshot, int dailyTargetMinutes)
{
    const QString prefix = QStringLiteral("距离日均%1小时: ")
        .arg(QString::number(dailyTargetMinutes / 60.0, 'f', 1));
    if (snapshot.workDays <= 0) {
        return prefix + QStringLiteral("暂无参与平均的记录");
    }
    const qint64 gapMinutes = qint64(dailyTargetMinutes) * snapshot.workDays
        - snapshot.totalOvertimeMinutes;
    if (gapMinutes == 0) {
        return prefix + QStringLiteral("已达标");
    }
    const qint64 minutes = qAbs(gapMinutes);
    QString duration;
    if (minutes < 60) {
        duration = QStringLiteral("%1分钟").arg(minutes);
    } else if (minutes % 60 == 0) {
        duration = QStringLiteral("%1小时").arg(minutes / 60);
    } else {
        duration = QStringLiteral("%1小时%2分钟").arg(minutes / 60).arg(minutes % 60);
    }
    return prefix + (gapMinutes > 0 ? QStringLiteral("还差 %1") : QStringLiteral("已超出 %1"))
        .arg(duration);
}

MonthlyAttendanceSnapshot AttendanceStatsService::buildMonthlySnapshot(int year, int month) {
    MonthlyAttendanceSnapshot snapshot;
    snapshot.year = year;
    snapshot.month = month;
    const WorkSchedule schedule = AttendanceStorage::loadWorkSchedule();
    snapshot.showMealAllowanceMarker = schedule.showMealAllowanceMarker;

    const QDate startDate(year, month, 1);
    const QDate endDate = startDate.addMonths(1).addDays(-1);

    QDate date = startDate;
    while (date <= endDate) {
        AttendanceDayView dayView;
        dayView.hasRecord = AttendanceStorage::hasArrivalRecord(date);

        if (dayView.hasRecord) {
            const AttendanceRecord record = AttendanceStorage::loadRecord(date);
            const WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(record, schedule);

            dayView.needAverageCal = record.needAverageCal;
            dayView.arrivalText = record.arrivalTime.toString("hh:mm");
            dayView.departureText = record.departureTime.toString("hh:mm");
            dayView.hasNote = !record.note.trimmed().isEmpty();
            dayView.hasCustomSchedule = record.hasCustomSchedule;
            dayView.note = record.note.trimmed();
            dayView.hasMealAllowance = record.departureTime >= (record.hasCustomSchedule
                ? record.customSchedule.mealAllowanceTime : schedule.mealAllowanceTime);

            snapshot.workDays++;
            if (!record.needAverageCal) {
                snapshot.workDays--;
            }

            if (result.overtimeMinutes > 0) {
                snapshot.totalOvertimeMinutes += result.overtimeMinutes;
            }
            if (dayView.hasMealAllowance) {
                snapshot.mealAllowanceCount++;
            }
            snapshot.totalLateMinutes += result.lateMinutes;
            snapshot.totalEarlyLeaveMinutes += result.earlyLeaveMinutes;
        }

        snapshot.dayViews.insert(date, dayView);
        date = date.addDays(1);
    }

    return snapshot;
}
