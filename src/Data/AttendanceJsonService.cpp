#include "AttendanceJsonService.h"
#include "AttendanceStorage.h"
#include "Types/AttendanceTypes.h"
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

bool updateTimeIfPresent(const QJsonObject& row, const char* key, QTime& target)
{
    if (!row.contains(key)) {
        return false;
    }

    const QTime value = QTime::fromString(row.value(key).toString(), "hh:mm");
    if (!value.isValid()) {
        return false;
    }

    target = value;
    return true;
}

bool hasValidSchedule(const WorkSchedule& schedule)
{
    return schedule.workStartTime < schedule.workEndTime
        && (!schedule.lunchBreakEnabled || schedule.lunchBreakStart < schedule.lunchBreakEnd)
        && (!schedule.dinnerBreakEnabled || schedule.dinnerBreakStart < schedule.dinnerBreakEnd)
        && schedule.mealAllowanceTime.isValid();
}

}

AttendanceImportResult AttendanceJsonService::importFromLarkJson(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return { false, 0, QStringLiteral("无法读取文件") };
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return { false, 0, QStringLiteral("JSON 文件格式无效，请确保是由 Lark-OCR-Sync 生成") };
    }

    int importedCount = 0;
    WorkSchedule importedSchedule = AttendanceStorage::loadWorkSchedule();
    bool hasImportedSchedule = false;
    // Compatible with dates from Python side: yyyy-MM-d and yyyy-MM-dd.
    const QJsonArray rows = doc.array();
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        const QDate date = QDate::fromString(row.value("date").toString(), "yyyy-MM-d");
        if (!date.isValid()) {
            continue;
        }

        const QString checkIn = row.value("check_in").toString();
        const QString checkOut = row.value("check_out").toString();
        if (checkIn.isEmpty() && checkOut.isEmpty()) {
            continue;
        }

        AttendanceStorage::upsertCheckTimes(date, checkIn, checkOut);
        if (row.value("note").isString()) {
            AttendanceRecord record = AttendanceStorage::loadRecord(date);
            record.note = row.value("note").toString();
            AttendanceStorage::saveRecord(date, record);
        }
        hasImportedSchedule = updateTimeIfPresent(row, "workStart", importedSchedule.workStartTime)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(row, "workEnd", importedSchedule.workEndTime)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(row, "lunchStart", importedSchedule.lunchBreakStart)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(row, "lunchEnd", importedSchedule.lunchBreakEnd)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(row, "dinnerStart", importedSchedule.dinnerBreakStart)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(row, "dinnerEnd", importedSchedule.dinnerBreakEnd)
            || hasImportedSchedule;
        hasImportedSchedule = updateTimeIfPresent(
            row, "mealAllowanceTime", importedSchedule.mealAllowanceTime)
            || hasImportedSchedule;
        if (row.value("lunchBreakEnabled").isBool()) {
            importedSchedule.lunchBreakEnabled = row.value("lunchBreakEnabled").toBool();
            hasImportedSchedule = true;
        }
        if (row.value("dinnerBreakEnabled").isBool()) {
            importedSchedule.dinnerBreakEnabled = row.value("dinnerBreakEnabled").toBool();
            hasImportedSchedule = true;
        }
        importedCount++;
    }

    if (hasImportedSchedule && hasValidSchedule(importedSchedule)) {
        AttendanceStorage::saveWorkSchedule(importedSchedule);
    }

    return { true, importedCount, QString() };
}

AttendanceExportResult AttendanceJsonService::exportToJson(const QString& filePath) {
    const QStringList dates = AttendanceStorage::recordedDates();
    if (dates.isEmpty()) {
        return { true, false, 0, QString() };
    }

    QJsonArray rows;
    const WorkSchedule schedule = AttendanceStorage::loadWorkSchedule();
    for (const QString& dateText : dates) {
        const QDate date = QDate::fromString(dateText, "yyyy-MM-dd");
        const AttendanceRecord record = AttendanceStorage::loadRecord(date);

        QJsonObject row;
        row["date"] = dateText;
        row["check_in"] = record.arrivalTime.toString("hh:mm");
        row["check_out"] = record.departureTime.toString("hh:mm");
        row["workStart"] = schedule.workStartTime.toString("hh:mm");
        row["workEnd"] = schedule.workEndTime.toString("hh:mm");
        row["lunchBreakEnabled"] = schedule.lunchBreakEnabled;
        row["lunchStart"] = schedule.lunchBreakStart.toString("hh:mm");
        row["lunchEnd"] = schedule.lunchBreakEnd.toString("hh:mm");
        row["dinnerBreakEnabled"] = schedule.dinnerBreakEnabled;
        row["dinnerStart"] = schedule.dinnerBreakStart.toString("hh:mm");
        row["dinnerEnd"] = schedule.dinnerBreakEnd.toString("hh:mm");
        row["mealAllowanceTime"] = schedule.mealAllowanceTime.toString("hh:mm");
        row["needAverageCal"] = record.needAverageCal;
        if (!record.note.isEmpty()) {
            row["note"] = record.note;
        }
        rows.append(row);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return { false, true, 0, QStringLiteral("无法保存文件，请检查权限或路径") };
    }

    file.write(QJsonDocument(rows).toJson());
    return { true, true, rows.size(), QString() };
}
