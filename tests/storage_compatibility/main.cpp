#include "Data/AttendanceStorage.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <cstdlib>
#include <cstdio>

void require(bool condition, const char* message)
{
    if (!condition) { qCritical() << "FAIL:" << message; std::exit(1); }
}
void execute(QSqlDatabase db, const QString& statement)
{
    QSqlQuery query(db);
    if (!query.exec(statement)) { qCritical() << query.lastError(); std::exit(1); }
}
QList<QVariantList> extraFields(QSqlDatabase db, int version)
{
    if (version < 5) return {};
    QSqlQuery query(db);
    require(query.exec(version >= 6
        ? "SELECT record_date, exclude_standard_overtime, custom_schedule FROM records ORDER BY record_date"
        : "SELECT record_date, exclude_standard_overtime FROM records ORDER BY record_date"), "read extra fields");
    QList<QVariantList> result;
    while (query.next()) {
        QVariantList row{query.value(0), query.value(1)};
        if (version >= 6) row.append(query.value(2));
        result.append(row);
    }
    return result;
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    });
    app.setOrganizationName("StorageCompatibilityTests");
    app.setApplicationName("StorageCompatibilityTests");
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    const bool fromSnapshot = app.arguments().value(1) == "--snapshot";
    int version = fromSnapshot ? 6 : app.arguments().value(1).toInt();
    const QString path = directory.filePath("attendance.db");
    if (fromSnapshot) require(QFile::copy(app.arguments().value(2), path), "copy snapshot; original stays unchanged");
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "attendance-storage");
    db.setDatabaseName(path);
    require(db.open(), "open database");
    if (!fromSnapshot) {
        execute(db, "CREATE TABLE schema_info (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        execute(db, QString("INSERT INTO schema_info VALUES ('schema_version', '%1')").arg(version));
        QString extra;
        if (version >= 5) extra += ", exclude_standard_overtime INTEGER NOT NULL DEFAULT 1";
        if (version >= 6) extra += ", custom_schedule TEXT NOT NULL DEFAULT ''";
        execute(db, "CREATE TABLE records (record_date TEXT PRIMARY KEY NOT NULL, need_average_cal INTEGER NOT NULL DEFAULT 1, arrival_time TEXT NOT NULL, departure_time TEXT NOT NULL, note TEXT NOT NULL DEFAULT ''" + extra + ")");
        execute(db, "CREATE TABLE work_schedule (id INTEGER PRIMARY KEY CHECK(id = 1), work_start TEXT NOT NULL, work_end TEXT NOT NULL, lunch_break_enabled INTEGER NOT NULL, lunch_start TEXT NOT NULL, lunch_end TEXT NOT NULL, dinner_break_enabled INTEGER NOT NULL, dinner_start TEXT NOT NULL, dinner_end TEXT NOT NULL, meal_allowance_time TEXT NOT NULL DEFAULT '21:00', show_meal_allowance_marker INTEGER NOT NULL DEFAULT 0)");
        execute(db, "INSERT INTO work_schedule VALUES (1, '08:00','17:00',1,'12:00','13:00',0,'18:00','18:30','20:30',1)");
        execute(db, "INSERT INTO records (record_date,arrival_time,departure_time,note) VALUES ('2026-09-16','08:11','19:22','original')");
        if (version >= 5) execute(db, "UPDATE records SET exclude_standard_overtime=0");
        if (version >= 6) execute(db, "UPDATE records SET custom_schedule='{\"workStart\":\"08:30\",\"workEnd\":\"17:30\"}'");
    }
    QString error;
    if (version > 6) {
        require(!AttendanceStorage::initialize(error), "future schema must be rejected explicitly");
        require(!error.isEmpty(), "failure is visible to user");
        qInfo() << "PASS: Unsupported schema has explicit error; data unchanged";
        db.close();
        return 0;
    }
    const auto extras = extraFields(db, version);
    QSqlQuery count(db);
    require(count.exec("SELECT COUNT(*) FROM records") && count.next(), "count fixture records");
    const int expected = count.value(0).toInt();
    count.finish();
    require(expected > 0, "fixture contains records");
    require(AttendanceStorage::initialize(error), "initialize existing database");
    const auto dates = AttendanceStorage::recordedDates();
    require(dates.size() == expected, "all original records remain visible");
    const QDate date = QDate::fromString(dates.first(), "yyyy-MM-dd");
    require(AttendanceStorage::hasArrivalRecord(date), "calendar sees existing date");
    AttendanceRecord record = AttendanceStorage::loadRecord(date);
    require(record.arrivalTime.isValid(), "read original record");
    record.note = "compatibility test";
    AttendanceStorage::saveRecord(date, record);
    require(AttendanceStorage::loadRecord(date).note == record.note, "edit saves successfully");
    AttendanceStorage::upsertCheckTimes(date, "08:25", "19:30");
    require(AttendanceStorage::loadRecord(date).arrivalTime == QTime(8,25), "import times saves successfully");
    require(extraFields(db, version) == extras, "edit and import preserve all older version additional fields");
    QSqlQuery schema(db);
    require(schema.exec("SELECT value FROM schema_info WHERE key='schema_version'") && schema.next(), "schema metadata readable");
    require(schema.value(0).toInt() == qMax(version, 6), "older schemas migrate to 6 without downgrading");
    schema.finish();
    const auto bytes = AttendanceStorage::createBackup(error);
    require(!bytes.isEmpty() && error.isEmpty(), "complete backup succeeds");
    QFile backup(directory.filePath("backup.db"));
    require(backup.open(QIODevice::WriteOnly) && backup.write(bytes) == bytes.size(), "save backup fixture");
    backup.close();
    QSqlDatabase snapshot = QSqlDatabase::addDatabase("QSQLITE", "snapshot");
    snapshot.setDatabaseName(backup.fileName());
    require(snapshot.open(), "open generated backup");
    require(extraFields(snapshot, version) == extras, "backup preserves additional fields");
    QSqlQuery check(snapshot);
    require(check.exec("PRAGMA integrity_check") && check.next() && check.value(0).toString() == "ok", "backup database integrity");
    require(check.exec("SELECT COUNT(*) FROM records") && check.next() && check.value(0).toInt() == expected, "backup contains every record");
    qInfo() << "PASS: Schema" << version << "reads all" << expected << "records, preserves extra fields and backs up complete database";
    check.finish();
    snapshot.close();
    db.close();
    return 0;
}
