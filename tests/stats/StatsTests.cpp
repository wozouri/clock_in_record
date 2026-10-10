#include "Data/AttendanceStatsService.h"
#include "Data/AttendanceStorage.h"
#include "Data/AttendanceJsonService.h"
#include "Data/WorkScheduleCodec.h"
#include "Cal/WorkTimeCalculator.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QTest>

class StatsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("AttendanceRegressionTests");
        QCoreApplication::setApplicationName("MonthlyStatisticsTests");
        auto db = QSqlDatabase::addDatabase("QSQLITE", "attendance-storage");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        QString error;
        QVERIFY2(AttendanceStorage::initialize(error), qPrintable(error));
    }
    void init() {
        QSqlQuery query(QSqlDatabase::database("attendance-storage"));
        QVERIFY(query.exec("DELETE FROM records"));
        AttendanceStorage::saveWorkSchedule(WorkSchedule{});
    }
    void screenshotTargetsUseCumulativeMinutes() {
        MonthlyAttendanceSnapshot snapshot;
        snapshot.workDays = 18;
        snapshot.totalOvertimeMinutes = 44 * 60 + 25;
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 120),
            QStringLiteral("距离日均2.0小时: 已超出 8小时25分钟"));
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 150),
            QStringLiteral("距离日均2.5小时: 还差 35分钟"));
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 180),
            QStringLiteral("距离日均3.0小时: 还差 9小时35分钟"));
    }
    void exactTargetAndExcess() {
        MonthlyAttendanceSnapshot snapshot;
        snapshot.workDays = 18;
        snapshot.totalOvertimeMinutes = 45 * 60;
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 150),
            QStringLiteral("距离日均2.5小时: 已达标"));
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 180),
            QStringLiteral("距离日均3.0小时: 还差 9小时"));
        snapshot.totalOvertimeMinutes = 54 * 60 + 1;
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 180),
            QStringLiteral("距离日均3.0小时: 已超出 1分钟"));
    }
    void emptyMonthDoesNotClaimTargetReached() {
        MonthlyAttendanceSnapshot snapshot;
        for (const int target : {120, 150, 180}) {
            QVERIFY(AttendanceStatsService::describeOvertimeTargetGap(snapshot, target)
                .endsWith(QStringLiteral("暂无参与平均的记录")));
        }
        snapshot.totalOvertimeMinutes = 90;
        QVERIFY(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 150)
            .endsWith(QStringLiteral("暂无参与平均的记录")));
    }
    void usesUnroundedMinutes() {
        MonthlyAttendanceSnapshot snapshot;
        snapshot.workDays = 18;
        snapshot.totalOvertimeMinutes = 45 * 60 - 1;
        QCOMPARE(AttendanceStatsService::describeOvertimeTargetGap(snapshot, 150),
            QStringLiteral("距离日均2.5小时: 还差 1分钟"));
    }
    void nonWorkdayFlagsAndCustomScheduleAffectCalculation() {
        AttendanceRecord record;
        record.needAverageCal = false;
        record.arrivalTime = QTime(10,0);
        record.departureTime = QTime(20,0);
        WorkSchedule schedule;
        // 10:00–20:00 共十小时，扣除午休一小时、晚餐半小时。
        QCOMPARE(WorkTimeCalculator::calculateWorkTimeResult(record, schedule).overtimeMinutes, 90);
        record.excludeStandardOvertime = false;
        QCOMPARE(WorkTimeCalculator::calculateWorkTimeResult(record, schedule).overtimeMinutes, 510);
        record.needAverageCal = true;
        record.hasCustomSchedule = true;
        record.customSchedule = schedule;
        record.customSchedule.workStartTime = QTime(10,0);
        record.customSchedule.workEndTime = QTime(19,0);
        QCOMPARE(WorkTimeCalculator::calculateWorkTimeResult(record, schedule).overtimeMinutes, 60);
        record.customSchedule.mealAllowanceTime = QTime(19,30);
        AttendanceStorage::saveRecord(QDate(2026,10,1), record);
        const auto loaded = AttendanceStorage::loadRecord(QDate(2026,10,1));
        QVERIFY(loaded.hasCustomSchedule);
        QVERIFY(workSchedulesEqual(loaded.customSchedule, record.customSchedule));
        const auto month = AttendanceStatsService::buildMonthlySnapshot(2026,10);
        QCOMPARE(month.totalOvertimeMinutes, 60);
        QVERIFY(month.dayViews.value(QDate(2026,10,1)).hasCustomSchedule);
        QVERIFY(month.dayViews.value(QDate(2026,10,1)).hasMealAllowance);
    }
    void jsonRoundtripPreservesFlagsAndIndependentSchedule() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AttendanceRecord record;
        record.needAverageCal = false;
        record.excludeStandardOvertime = false;
        record.hasCustomSchedule = true;
        record.customSchedule.workStartTime = QTime(8,30);
        record.customSchedule.workEndTime = QTime(17,30);
        record.note = QStringLiteral("单日作息");
        const QDate date(2026,10,3);
        AttendanceStorage::saveRecord(date, record);
        const QString file = directory.filePath("records.json");
        QVERIFY(AttendanceJsonService::exportToJson(file).success);
        const auto beforePreview = AttendanceStorage::loadRecord(date);
        const auto preview = AttendanceJsonService::previewImport(file);
        QVERIFY(preview.success);
        QCOMPARE(preview.overwrittenCount, 1);
        QCOMPARE(AttendanceStorage::loadRecord(date).note, beforePreview.note);
        AttendanceStorage::deleteRecord(date);
        QVERIFY(AttendanceJsonService::importFromLarkJson(file).success);
        const auto restored = AttendanceStorage::loadRecord(date);
        QCOMPARE(restored.needAverageCal, false);
        QCOMPARE(restored.excludeStandardOvertime, false);
        QVERIFY(restored.hasCustomSchedule);
        QVERIFY(workSchedulesEqual(restored.customSchedule, record.customSchedule));
        QCOMPARE(restored.note, record.note);
        QCOMPARE(restored.arrivalTime, record.arrivalTime);
        // 旧的打卡导入文件不含这些字段时，不能把已有扩展字段重置。
        QFile times(directory.filePath("times.json"));
        QVERIFY(times.open(QIODevice::WriteOnly));
        times.write("[{\"date\":\"2026-10-3\",\"check_in\":\"08:35\",\"check_out\":\"19:00\"}]");
        times.close();
        QVERIFY(AttendanceJsonService::importFromLarkJson(times.fileName()).success);
        const auto updated = AttendanceStorage::loadRecord(date);
        QVERIFY(updated.hasCustomSchedule);
        QCOMPARE(updated.arrivalTime, QTime(8,35));
        QCOMPARE(updated.excludeStandardOvertime, false);
        QVERIFY(workSchedulesEqual(updated.customSchedule, record.customSchedule));
    }
    void invalidImportedScheduleDoesNotModifyRecords() {
        QTemporaryDir directory;
        QFile file(directory.filePath("invalid.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[{\"date\":\"2026-10-1\",\"check_in\":\"09:00\",\"check_out\":\"18:00\",\"hasCustomSchedule\":true,\"customSchedule\":{\"workStart\":\"19:00\",\"workEnd\":\"09:00\"}}]");
        file.close();
        QVERIFY(!AttendanceJsonService::previewImport(file.fileName()).success);
        QVERIFY(!AttendanceJsonService::importFromLarkJson(file.fileName()).success);
        QVERIFY(AttendanceStorage::recordedDates().isEmpty());
    }
    void emptyNotesAndUnknownScheduleFieldsRoundtrip() {
        QTemporaryDir directory;
        const QDate date(2026,10,3);
        AttendanceRecord original;
        original.hasCustomSchedule = true;
        original.customSchedule.workStartTime = QTime(8,30);
        original.customScheduleJson = QStringLiteral("{\"workStart\":\"08:30\",\"futureField\":42}");
        AttendanceStorage::saveRecord(date, original);
        const auto path = directory.filePath("export.json");
        QVERIFY(AttendanceJsonService::exportToJson(path).success);
        original.note = QStringLiteral("旧备注需要清空");
        AttendanceStorage::saveRecord(date, original);
        QVERIFY(AttendanceJsonService::importFromLarkJson(path).success);
        const auto restored = AttendanceStorage::loadRecord(date);
        QVERIFY(restored.note.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(restored.customScheduleJson.toUtf8()).object().value("futureField").toInt(), 42);
    }
    void malformedGlobalScheduleRejectsWholeImport() {
        QTemporaryDir directory;
        QFile file(directory.filePath("invalid-time.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[{\"date\":\"2026-10-3\",\"check_in\":\"08:30\",\"check_out\":\"19:00\",\"workStart\":\"wrong\"}]");
        file.close();
        QVERIFY(!AttendanceJsonService::importFromLarkJson(file.fileName()).success);
        QVERIFY(AttendanceStorage::recordedDates().isEmpty());
    }
};
QTEST_GUILESS_MAIN(StatsTests)
#include "StatsTests.moc"
