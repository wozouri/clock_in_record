#include "AttendanceStorage.h"
#include <QSettings>
#include <QSet>
#include <algorithm>

namespace {
// Import path may only write check-in/check-out. This helper guarantees
// all calculation-related keys exist so downstream logic stays stable.
void ensureDefaults(QSettings& settings, const QString& key) {
    if (!settings.contains(key + "/needAverageCal")) settings.setValue(key + "/needAverageCal", true);
    if (!settings.contains(key + "/workStart")) settings.setValue(key + "/workStart", "09:00");
    if (!settings.contains(key + "/workEnd")) settings.setValue(key + "/workEnd", "18:00");
    if (!settings.contains(key + "/lunchStart")) settings.setValue(key + "/lunchStart", "12:30");
    if (!settings.contains(key + "/lunchEnd")) settings.setValue(key + "/lunchEnd", "13:30");
    if (!settings.contains(key + "/dinnerStart")) settings.setValue(key + "/dinnerStart", "18:00");
    if (!settings.contains(key + "/dinnerEnd")) settings.setValue(key + "/dinnerEnd", "18:30");
}
}

AttendanceRecord AttendanceStorage::loadRecord(const QDate& date) {
    QSettings settings;
    const QString key = dateKey(date);

    AttendanceRecord record;
    record.needAverageCal = settings.value(key + "/needAverageCal", record.needAverageCal).toBool();
    record.arrivalTime = QTime::fromString(settings.value(key + "/arrival", "09:00").toString(), "hh:mm");
    record.departureTime = QTime::fromString(settings.value(key + "/departure", "18:00").toString(), "hh:mm");
    record.workStartTime = QTime::fromString(settings.value(key + "/workStart", "09:00").toString(), "hh:mm");
    record.workEndTime = QTime::fromString(settings.value(key + "/workEnd", "18:00").toString(), "hh:mm");
    record.lunchBreakStart = QTime::fromString(settings.value(key + "/lunchStart", "12:30").toString(), "hh:mm");
    record.lunchBreakEnd = QTime::fromString(settings.value(key + "/lunchEnd", "13:30").toString(), "hh:mm");
    record.dinnerBreakStart = QTime::fromString(settings.value(key + "/dinnerStart", "18:00").toString(), "hh:mm");
    record.dinnerBreakEnd = QTime::fromString(settings.value(key + "/dinnerEnd", "18:30").toString(), "hh:mm");

    return record;
}

void AttendanceStorage::saveRecord(const QDate& date, const AttendanceRecord& record) {
    QSettings settings;
    const QString key = dateKey(date);

    settings.setValue(key + "/needAverageCal", record.needAverageCal);
    settings.setValue(key + "/arrival", record.arrivalTime.toString("hh:mm"));
    settings.setValue(key + "/departure", record.departureTime.toString("hh:mm"));
    settings.setValue(key + "/workStart", record.workStartTime.toString("hh:mm"));
    settings.setValue(key + "/workEnd", record.workEndTime.toString("hh:mm"));
    settings.setValue(key + "/lunchStart", record.lunchBreakStart.toString("hh:mm"));
    settings.setValue(key + "/lunchEnd", record.lunchBreakEnd.toString("hh:mm"));
    settings.setValue(key + "/dinnerStart", record.dinnerBreakStart.toString("hh:mm"));
    settings.setValue(key + "/dinnerEnd", record.dinnerBreakEnd.toString("hh:mm"));
}

void AttendanceStorage::deleteRecord(const QDate& date) {
    QSettings settings;
    const QString key = dateKey(date);

    settings.remove(key + "/needAverageCal");
    settings.remove(key + "/arrival");
    settings.remove(key + "/departure");
    settings.remove(key + "/workStart");
    settings.remove(key + "/workEnd");
    settings.remove(key + "/lunchStart");
    settings.remove(key + "/lunchEnd");
    settings.remove(key + "/dinnerStart");
    settings.remove(key + "/dinnerEnd");
}

bool AttendanceStorage::hasArrivalRecord(const QDate& date) {
    QSettings settings;
    return settings.contains(dateKey(date) + "/arrival");
}

QStringList AttendanceStorage::recordedDates() {
    QSettings settings;
    const QStringList allKeys = settings.allKeys();
    QSet<QString> validDates;

    // QSettings key format: yyyy-MM-dd/field
    for (const QString& key : allKeys) {
        const QString date = key.section('/', 0, 0);
        if (QDate::fromString(date, "yyyy-MM-dd").isValid()) {
            validDates.insert(date);
        }
    }

    QStringList dates = validDates.values();
    std::sort(dates.begin(), dates.end());
    return dates;
}

void AttendanceStorage::upsertCheckTimes(const QDate& date, const QString& checkIn, const QString& checkOut) {
    QSettings settings;
    const QString key = dateKey(date);

    settings.setValue(key + "/arrival", checkIn);
    settings.setValue(key + "/departure", checkOut);
    ensureDefaults(settings, key);
}

QString AttendanceStorage::dateKey(const QDate& date) {
    return date.toString("yyyy-MM-dd");
}
