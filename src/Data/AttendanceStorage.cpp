#include "AttendanceStorage.h"

#include <QSettings>
#include <QSet>

#include <algorithm>

namespace {

constexpr auto kTimeFormat = "hh:mm";

QTime readTime(const QSettings& settings, const QString& key, const QTime& fallback)
{
    const QTime value = QTime::fromString(settings.value(key).toString(), kTimeFormat);
    return value.isValid() ? value : fallback;
}

void removeLegacySchedule(QSettings& settings, const QString& key)
{
    settings.remove(key + "/workStart");
    settings.remove(key + "/workEnd");
    settings.remove(key + "/lunchStart");
    settings.remove(key + "/lunchEnd");
    settings.remove(key + "/dinnerStart");
    settings.remove(key + "/dinnerEnd");
}

WorkSchedule readLegacySchedule(const QSettings& settings, const QString& key)
{
    WorkSchedule schedule;
    schedule.workStartTime = readTime(settings, key + "/workStart", schedule.workStartTime);
    schedule.workEndTime = readTime(settings, key + "/workEnd", schedule.workEndTime);
    schedule.lunchBreakStart = readTime(settings, key + "/lunchStart", schedule.lunchBreakStart);
    schedule.lunchBreakEnd = readTime(settings, key + "/lunchEnd", schedule.lunchBreakEnd);
    schedule.dinnerBreakStart = readTime(settings, key + "/dinnerStart", schedule.dinnerBreakStart);
    schedule.dinnerBreakEnd = readTime(settings, key + "/dinnerEnd", schedule.dinnerBreakEnd);
    return schedule;
}

}

WorkSchedule AttendanceStorage::loadWorkSchedule()
{
    QSettings settings;
    WorkSchedule schedule;

    if (!settings.contains("schedule/workStart")) {
        const QStringList dates = recordedDates();
        return dates.isEmpty() ? schedule : readLegacySchedule(settings, dates.last());
    }

    schedule.workStartTime = readTime(settings, "schedule/workStart", schedule.workStartTime);
    schedule.workEndTime = readTime(settings, "schedule/workEnd", schedule.workEndTime);
    schedule.lunchBreakEnabled = settings.value("schedule/lunchBreakEnabled", schedule.lunchBreakEnabled).toBool();
    schedule.lunchBreakStart = readTime(settings, "schedule/lunchStart", schedule.lunchBreakStart);
    schedule.lunchBreakEnd = readTime(settings, "schedule/lunchEnd", schedule.lunchBreakEnd);
    schedule.dinnerBreakEnabled = settings.value("schedule/dinnerBreakEnabled", schedule.dinnerBreakEnabled).toBool();
    schedule.dinnerBreakStart = readTime(settings, "schedule/dinnerStart", schedule.dinnerBreakStart);
    schedule.dinnerBreakEnd = readTime(settings, "schedule/dinnerEnd", schedule.dinnerBreakEnd);
    return schedule;
}

void AttendanceStorage::saveWorkSchedule(const WorkSchedule& schedule)
{
    QSettings settings;
    settings.setValue("schedule/workStart", schedule.workStartTime.toString(kTimeFormat));
    settings.setValue("schedule/workEnd", schedule.workEndTime.toString(kTimeFormat));
    settings.setValue("schedule/lunchBreakEnabled", schedule.lunchBreakEnabled);
    settings.setValue("schedule/lunchStart", schedule.lunchBreakStart.toString(kTimeFormat));
    settings.setValue("schedule/lunchEnd", schedule.lunchBreakEnd.toString(kTimeFormat));
    settings.setValue("schedule/dinnerBreakEnabled", schedule.dinnerBreakEnabled);
    settings.setValue("schedule/dinnerStart", schedule.dinnerBreakStart.toString(kTimeFormat));
    settings.setValue("schedule/dinnerEnd", schedule.dinnerBreakEnd.toString(kTimeFormat));
}

AttendanceRecord AttendanceStorage::loadRecord(const QDate& date)
{
    QSettings settings;
    const QString key = dateKey(date);

    AttendanceRecord record;
    record.needAverageCal = settings.value(key + "/needAverageCal", record.needAverageCal).toBool();
    record.arrivalTime = readTime(settings, key + "/arrival", record.arrivalTime);
    record.departureTime = readTime(settings, key + "/departure", record.departureTime);
    return record;
}

void AttendanceStorage::saveRecord(const QDate& date, const AttendanceRecord& record)
{
    QSettings settings;
    const QString key = dateKey(date);

    settings.setValue(key + "/needAverageCal", record.needAverageCal);
    settings.setValue(key + "/arrival", record.arrivalTime.toString(kTimeFormat));
    settings.setValue(key + "/departure", record.departureTime.toString(kTimeFormat));
    removeLegacySchedule(settings, key);
}

void AttendanceStorage::deleteRecord(const QDate& date)
{
    QSettings settings;
    const QString key = dateKey(date);

    settings.remove(key + "/needAverageCal");
    settings.remove(key + "/arrival");
    settings.remove(key + "/departure");
    removeLegacySchedule(settings, key);
}

bool AttendanceStorage::hasArrivalRecord(const QDate& date)
{
    QSettings settings;
    return settings.contains(dateKey(date) + "/arrival");
}

QStringList AttendanceStorage::recordedDates()
{
    QSettings settings;
    const QStringList allKeys = settings.allKeys();
    QSet<QString> validDates;

    for (const QString& key : allKeys) {
        if (!key.endsWith("/arrival")) {
            continue;
        }

        const QString date = key.section('/', 0, 0);
        if (QDate::fromString(date, "yyyy-MM-dd").isValid()) {
            validDates.insert(date);
        }
    }

    QStringList dates = validDates.values();
    std::sort(dates.begin(), dates.end());
    return dates;
}

void AttendanceStorage::upsertCheckTimes(const QDate& date, const QString& checkIn, const QString& checkOut)
{
    QSettings settings;
    const QString key = dateKey(date);
    settings.setValue(key + "/arrival", checkIn);
    settings.setValue(key + "/departure", checkOut);
}

QString AttendanceStorage::dateKey(const QDate& date)
{
    return date.toString("yyyy-MM-dd");
}
