#include "AttendanceStorage.h"

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

namespace {

constexpr auto kTimeFormat = "hh:mm";
constexpr auto kConnectionName = "attendance-storage";
constexpr auto kSchemaVersion = 3;

QTime readTime(const QString& value, const QTime& fallback)
{
    const QTime time = QTime::fromString(value, kTimeFormat);
    return time.isValid() ? time : fallback;
}

void logQueryError(const QSqlQuery& query, const QString& operation)
{
    qWarning() << "Attendance storage" << operation << "failed:" << query.lastError().text();
}

QSqlDatabase openDatabase()
{
    QSqlDatabase database;
    if (QSqlDatabase::contains(kConnectionName)) {
        database = QSqlDatabase::database(kConnectionName);
    }
    else {
        database = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
        const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataPath);
        database.setDatabaseName(QDir(dataPath).filePath("attendance.db"));
    }

    if (!database.isOpen() && !database.open()) {
        qWarning() << "Unable to open attendance database:" << database.lastError().text();
    }
    return database;
}

QStringList legacyRecordedDates(const QSettings& settings)
{
    QSet<QString> dates;
    for (const QString& key : settings.allKeys()) {
        if (!key.endsWith("/arrival")) {
            continue;
        }

        const QString dateText = key.section('/', 0, 0);
        if (QDate::fromString(dateText, "yyyy-MM-dd").isValid()) {
            dates.insert(dateText);
        }
    }

    QStringList result = dates.values();
    std::sort(result.begin(), result.end());
    return result;
}

WorkSchedule readLegacySchedule(const QSettings& settings, const QString& key)
{
    WorkSchedule schedule;
    schedule.workStartTime = readTime(settings.value(key + "/workStart").toString(), schedule.workStartTime);
    schedule.workEndTime = readTime(settings.value(key + "/workEnd").toString(), schedule.workEndTime);
    schedule.lunchBreakStart = readTime(settings.value(key + "/lunchStart").toString(), schedule.lunchBreakStart);
    schedule.lunchBreakEnd = readTime(settings.value(key + "/lunchEnd").toString(), schedule.lunchBreakEnd);
    schedule.dinnerBreakStart = readTime(settings.value(key + "/dinnerStart").toString(), schedule.dinnerBreakStart);
    schedule.dinnerBreakEnd = readTime(settings.value(key + "/dinnerEnd").toString(), schedule.dinnerBreakEnd);
    return schedule;
}

AttendanceRecord readLegacyRecord(const QSettings& settings, const QString& dateText)
{
    AttendanceRecord record;
    record.needAverageCal = settings.value(dateText + "/needAverageCal", record.needAverageCal).toBool();
    record.arrivalTime = readTime(settings.value(dateText + "/arrival").toString(), record.arrivalTime);
    record.departureTime = readTime(settings.value(dateText + "/departure").toString(), record.departureTime);
    return record;
}

bool writeWorkSchedule(QSqlDatabase database, const WorkSchedule& schedule)
{
    QSqlQuery query(database);
    query.prepare(
        "INSERT OR REPLACE INTO work_schedule "
        "(id, work_start, work_end, lunch_break_enabled, lunch_start, lunch_end, "
        "dinner_break_enabled, dinner_start, dinner_end, meal_allowance_time) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(schedule.workStartTime.toString(kTimeFormat));
    query.addBindValue(schedule.workEndTime.toString(kTimeFormat));
    query.addBindValue(schedule.lunchBreakEnabled);
    query.addBindValue(schedule.lunchBreakStart.toString(kTimeFormat));
    query.addBindValue(schedule.lunchBreakEnd.toString(kTimeFormat));
    query.addBindValue(schedule.dinnerBreakEnabled);
    query.addBindValue(schedule.dinnerBreakStart.toString(kTimeFormat));
    query.addBindValue(schedule.dinnerBreakEnd.toString(kTimeFormat));
    query.addBindValue(schedule.mealAllowanceTime.toString(kTimeFormat));
    if (!query.exec()) {
        logQueryError(query, QStringLiteral("saving work schedule"));
        return false;
    }
    return true;
}

bool writeRecord(QSqlDatabase database, const QDate& date, const AttendanceRecord& record)
{
    QSqlQuery query(database);
    query.prepare(
        "INSERT OR REPLACE INTO records "
        "(record_date, need_average_cal, arrival_time, departure_time, note) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.addBindValue(record.needAverageCal);
    query.addBindValue(record.arrivalTime.toString(kTimeFormat));
    query.addBindValue(record.departureTime.toString(kTimeFormat));
    query.addBindValue(record.note);
    if (!query.exec()) {
        logQueryError(query, QStringLiteral("saving record"));
        return false;
    }
    return true;
}

bool initializeSchema(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec("CREATE TABLE IF NOT EXISTS schema_info (key TEXT PRIMARY KEY, value TEXT NOT NULL)")) {
        logQueryError(query, QStringLiteral("creating schema metadata"));
        return false;
    }
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS records ("
            "record_date TEXT PRIMARY KEY NOT NULL, "
            "need_average_cal INTEGER NOT NULL DEFAULT 1, "
            "arrival_time TEXT NOT NULL, "
            "departure_time TEXT NOT NULL, "
            "note TEXT NOT NULL DEFAULT '')")) {
        logQueryError(query, QStringLiteral("creating records table"));
        return false;
    }
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS work_schedule ("
            "id INTEGER PRIMARY KEY CHECK(id = 1), "
            "work_start TEXT NOT NULL, work_end TEXT NOT NULL, "
            "lunch_break_enabled INTEGER NOT NULL, lunch_start TEXT NOT NULL, lunch_end TEXT NOT NULL, "
            "dinner_break_enabled INTEGER NOT NULL, dinner_start TEXT NOT NULL, dinner_end TEXT NOT NULL, "
            "meal_allowance_time TEXT NOT NULL DEFAULT '21:00')")) {
        logQueryError(query, QStringLiteral("creating work schedule table"));
        return false;
    }
    return true;
}

bool migrateLegacySettings(QSqlDatabase database)
{
    QSettings legacy;
    const QStringList dates = legacyRecordedDates(legacy);

    WorkSchedule schedule;
    if (legacy.contains("schedule/workStart")) {
        schedule.workStartTime = readTime(legacy.value("schedule/workStart").toString(), schedule.workStartTime);
        schedule.workEndTime = readTime(legacy.value("schedule/workEnd").toString(), schedule.workEndTime);
        schedule.lunchBreakEnabled = legacy.value("schedule/lunchBreakEnabled", schedule.lunchBreakEnabled).toBool();
        schedule.lunchBreakStart = readTime(legacy.value("schedule/lunchStart").toString(), schedule.lunchBreakStart);
        schedule.lunchBreakEnd = readTime(legacy.value("schedule/lunchEnd").toString(), schedule.lunchBreakEnd);
        schedule.dinnerBreakEnabled = legacy.value("schedule/dinnerBreakEnabled", schedule.dinnerBreakEnabled).toBool();
        schedule.dinnerBreakStart = readTime(legacy.value("schedule/dinnerStart").toString(), schedule.dinnerBreakStart);
        schedule.dinnerBreakEnd = readTime(legacy.value("schedule/dinnerEnd").toString(), schedule.dinnerBreakEnd);
    }
    else if (!dates.isEmpty()) {
        schedule = readLegacySchedule(legacy, dates.last());
    }

    if (!database.transaction()) {
        qWarning() << "Unable to begin attendance database migration:" << database.lastError().text();
        return false;
    }

    bool success = writeWorkSchedule(database, schedule);
    for (const QString& dateText : dates) {
        const QDate date = QDate::fromString(dateText, "yyyy-MM-dd");
        success = writeRecord(database, date, readLegacyRecord(legacy, dateText)) && success;
    }

    QSqlQuery versionQuery(database);
    versionQuery.prepare("INSERT INTO schema_info (key, value) VALUES ('schema_version', ?)");
    versionQuery.addBindValue(kSchemaVersion);
    success = versionQuery.exec() && success;
    if (!success) {
        if (!versionQuery.isActive()) {
            logQueryError(versionQuery, QStringLiteral("recording schema version"));
        }
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        qWarning() << "Unable to commit attendance database migration:" << database.lastError().text();
        return false;
    }
    return true;
}

bool migrateSchema(QSqlDatabase database, int currentVersion)
{
    if (currentVersion > kSchemaVersion) {
        qWarning() << "Attendance database schema is newer than this application:" << currentVersion;
        return false;
    }
    if (currentVersion == kSchemaVersion) {
        return true;
    }
    if (!database.transaction()) {
        qWarning() << "Unable to begin attendance database schema migration:" << database.lastError().text();
        return false;
    }

    bool success = true;
    if (currentVersion < 2) {
        QSqlQuery query(database);
        success = query.exec("ALTER TABLE records ADD COLUMN note TEXT NOT NULL DEFAULT ''");
        if (!success) {
            logQueryError(query, QStringLiteral("adding record note column"));
        }
    }
    if (success && currentVersion < 3) {
        QSqlQuery query(database);
        success = query.exec(
            "ALTER TABLE work_schedule ADD COLUMN meal_allowance_time TEXT NOT NULL DEFAULT '21:00'");
        if (!success) {
            logQueryError(query, QStringLiteral("adding meal allowance time"));
        }
    }

    QSqlQuery versionQuery(database);
    versionQuery.prepare("UPDATE schema_info SET value = ? WHERE key = 'schema_version'");
    versionQuery.addBindValue(kSchemaVersion);
    success = versionQuery.exec() && success;
    if (!success) {
        if (!versionQuery.isActive()) {
            logQueryError(versionQuery, QStringLiteral("updating schema version"));
        }
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        qWarning() << "Unable to commit attendance database schema migration:" << database.lastError().text();
        return false;
    }
    return true;
}

bool ensureInitialized(QSqlDatabase database)
{
    static bool initialized = false;
    static bool initializationFailed = false;
    if (initialized) {
        return true;
    }
    if (initializationFailed || !database.isOpen() || !initializeSchema(database)) {
        initializationFailed = true;
        return false;
    }

    QSqlQuery versionQuery(database);
    if (!versionQuery.exec("SELECT value FROM schema_info WHERE key = 'schema_version'")) {
        logQueryError(versionQuery, QStringLiteral("reading schema version"));
        initializationFailed = true;
        return false;
    }
    if (!versionQuery.next()) {
        if (!migrateLegacySettings(database)) {
            initializationFailed = true;
            return false;
        }
    }
    else if (!migrateSchema(database, versionQuery.value(0).toInt())) {
        initializationFailed = true;
        return false;
    }

    initialized = true;
    return true;
}

QSqlDatabase storageDatabase()
{
    QSqlDatabase database = openDatabase();
    if (!ensureInitialized(database)) {
        return QSqlDatabase();
    }
    return database;
}

}

WorkSchedule AttendanceStorage::loadWorkSchedule()
{
    WorkSchedule schedule;
    const QSqlDatabase database = storageDatabase();
    if (!database.isOpen()) {
        return schedule;
    }

    QSqlQuery query(database);
    if (!query.exec(
            "SELECT work_start, work_end, lunch_break_enabled, lunch_start, lunch_end, "
            "dinner_break_enabled, dinner_start, dinner_end, meal_allowance_time "
            "FROM work_schedule WHERE id = 1")) {
        logQueryError(query, QStringLiteral("loading work schedule"));
        return schedule;
    }
    if (!query.next()) {
        return schedule;
    }

    schedule.workStartTime = readTime(query.value(0).toString(), schedule.workStartTime);
    schedule.workEndTime = readTime(query.value(1).toString(), schedule.workEndTime);
    schedule.lunchBreakEnabled = query.value(2).toBool();
    schedule.lunchBreakStart = readTime(query.value(3).toString(), schedule.lunchBreakStart);
    schedule.lunchBreakEnd = readTime(query.value(4).toString(), schedule.lunchBreakEnd);
    schedule.dinnerBreakEnabled = query.value(5).toBool();
    schedule.dinnerBreakStart = readTime(query.value(6).toString(), schedule.dinnerBreakStart);
    schedule.dinnerBreakEnd = readTime(query.value(7).toString(), schedule.dinnerBreakEnd);
    schedule.mealAllowanceTime = readTime(query.value(8).toString(), schedule.mealAllowanceTime);
    return schedule;
}

void AttendanceStorage::saveWorkSchedule(const WorkSchedule& schedule)
{
    const QSqlDatabase database = storageDatabase();
    if (database.isOpen()) {
        writeWorkSchedule(database, schedule);
    }
}

AttendanceRecord AttendanceStorage::loadRecord(const QDate& date)
{
    AttendanceRecord record;
    const QSqlDatabase database = storageDatabase();
    if (!database.isOpen()) {
        return record;
    }

    QSqlQuery query(database);
    query.prepare("SELECT need_average_cal, arrival_time, departure_time, note FROM records WHERE record_date = ?");
    query.addBindValue(dateKey(date));
    if (!query.exec()) {
        logQueryError(query, QStringLiteral("loading record"));
        return record;
    }
    if (!query.next()) {
        return record;
    }

    record.needAverageCal = query.value(0).toBool();
    record.arrivalTime = readTime(query.value(1).toString(), record.arrivalTime);
    record.departureTime = readTime(query.value(2).toString(), record.departureTime);
    record.note = query.value(3).toString();
    return record;
}

void AttendanceStorage::saveRecord(const QDate& date, const AttendanceRecord& record)
{
    const QSqlDatabase database = storageDatabase();
    if (database.isOpen()) {
        writeRecord(database, date, record);
    }
}

void AttendanceStorage::deleteRecord(const QDate& date)
{
    const QSqlDatabase database = storageDatabase();
    if (!database.isOpen()) {
        return;
    }

    QSqlQuery query(database);
    query.prepare("DELETE FROM records WHERE record_date = ?");
    query.addBindValue(dateKey(date));
    if (!query.exec()) {
        logQueryError(query, QStringLiteral("deleting record"));
    }
}

bool AttendanceStorage::hasArrivalRecord(const QDate& date)
{
    const QSqlDatabase database = storageDatabase();
    if (!database.isOpen()) {
        return false;
    }

    QSqlQuery query(database);
    query.prepare("SELECT 1 FROM records WHERE record_date = ? LIMIT 1");
    query.addBindValue(dateKey(date));
    if (!query.exec()) {
        logQueryError(query, QStringLiteral("checking record"));
        return false;
    }
    return query.next();
}

QStringList AttendanceStorage::recordedDates()
{
    QStringList dates;
    const QSqlDatabase database = storageDatabase();
    if (!database.isOpen()) {
        return dates;
    }

    QSqlQuery query(database);
    if (!query.exec("SELECT record_date FROM records ORDER BY record_date")) {
        logQueryError(query, QStringLiteral("listing records"));
        return dates;
    }
    while (query.next()) {
        dates.append(query.value(0).toString());
    }
    return dates;
}

void AttendanceStorage::upsertCheckTimes(const QDate& date, const QString& checkIn, const QString& checkOut)
{
    AttendanceRecord record = loadRecord(date);
    const QTime arrival = QTime::fromString(checkIn, kTimeFormat);
    const QTime departure = QTime::fromString(checkOut, kTimeFormat);
    if (arrival.isValid()) {
        record.arrivalTime = arrival;
    }
    if (departure.isValid()) {
        record.departureTime = departure;
    }
    saveRecord(date, record);
}

QString AttendanceStorage::dateKey(const QDate& date)
{
    return date.toString("yyyy-MM-dd");
}
