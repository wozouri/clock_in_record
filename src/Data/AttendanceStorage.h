#ifndef ATTENDANCESTORAGE_H
#define ATTENDANCESTORAGE_H

#include "Types/AttendanceTypes.h"
#include <QDate>
#include <QStringList>

// Centralizes SQLite access for attendance data.
class AttendanceStorage {
public:
    static WorkSchedule loadWorkSchedule();
    static void saveWorkSchedule(const WorkSchedule& schedule);

    static AttendanceRecord loadRecord(const QDate& date);
    static void saveRecord(const QDate& date, const AttendanceRecord& record);
    static void deleteRecord(const QDate& date);

    static bool hasArrivalRecord(const QDate& date);
    static QStringList recordedDates();

    static void upsertCheckTimes(const QDate& date, const QString& checkIn, const QString& checkOut);

private:
    static QString dateKey(const QDate& date);
};

#endif // ATTENDANCESTORAGE_H
