#ifndef ATTENDANCESTORAGE_H
#define ATTENDANCESTORAGE_H

#include "Types/AttendanceTypes.h"
#include <QDate>
#include <QStringList>

// Centralizes all QSettings access for attendance data.
class AttendanceStorage {
public:
    static AttendanceRecord loadRecord(const QDate& date);
    static void saveRecord(const QDate& date, const AttendanceRecord& record);
    static void deleteRecord(const QDate& date);

    static bool hasArrivalRecord(const QDate& date);
    static QStringList recordedDates();

    // Used by JSON import to write raw check-in/check-out values while
    // preserving default configuration keys required by calculations.
    static void upsertCheckTimes(const QDate& date, const QString& checkIn, const QString& checkOut);

private:
    static QString dateKey(const QDate& date);
};

#endif // ATTENDANCESTORAGE_H
