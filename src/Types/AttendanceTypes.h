#pragma once

#include <QTime>
#include <QString>

struct WorkSchedule {
    QTime workStartTime = QTime(9, 0);
    QTime workEndTime = QTime(18, 0);
    bool lunchBreakEnabled = true;
    QTime lunchBreakStart = QTime(12, 30);
    QTime lunchBreakEnd = QTime(13, 30);
    bool dinnerBreakEnabled = true;
    QTime dinnerBreakStart = QTime(18, 0);
    QTime dinnerBreakEnd = QTime(18, 30);
    QTime mealAllowanceTime = QTime(21, 0);
    bool showMealAllowanceMarker = false;
};

struct AttendanceRecord {
    bool needAverageCal = true;
    QTime arrivalTime = QTime(9, 0);
    QTime departureTime = QTime(18, 0);
    QString note;
};

struct WorkTimeResult {
    int actualWorkMinutes = 0;
    int standardWorkMinutes = 0;
    int lateMinutes = 0;
    int earlyLeaveMinutes = 0;
    int overtimeMinutes = 0;
    int totalBreakMinutes = 0;
};
