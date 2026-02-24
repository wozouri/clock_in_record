#ifndef ATTENDANCESTATSSERVICE_H
#define ATTENDANCESTATSSERVICE_H

#include <QDate>
#include <QMap>
#include <QString>

struct AttendanceDayView {
    bool hasRecord = false;
    bool needAverageCal = true;
    QString arrivalText;
    QString departureText;
};

struct MonthlyAttendanceSnapshot {
    int year = 0;
    int month = 0;
    int workDays = 0;
    int totalOvertimeMinutes = 0;
    int totalLateMinutes = 0;
    int totalEarlyLeaveMinutes = 0;
    QMap<QDate, AttendanceDayView> dayViews;
};

// Provides month-level statistics and day display data for UI rendering.
class AttendanceStatsService {
public:
    static MonthlyAttendanceSnapshot buildMonthlySnapshot(int year, int month);
};

#endif // ATTENDANCESTATSSERVICE_H
