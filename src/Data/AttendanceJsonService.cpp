#include "AttendanceJsonService.h"
#include "AttendanceStorage.h"
#include "Types/AttendanceTypes.h"
#include "WorkScheduleCodec.h"
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

AttendanceImportPreview AttendanceJsonService::previewImport(const QString& filePath)
{
    AttendanceImportPreview preview;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        preview.errorMessage = QStringLiteral("无法读取文件");
        return preview;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        preview.errorMessage = QStringLiteral("JSON 文件格式无效");
        return preview;
    }
    WorkSchedule imported = AttendanceStorage::loadWorkSchedule();
    for (const auto& value : document.array()) {
        const auto row = value.toObject();
        const QDate date = QDate::fromString(row.value("date").toString(), "yyyy-MM-d");
        if (!date.isValid() || (!QTime::fromString(row.value("check_in").toString(), "hh:mm").isValid()
            && !QTime::fromString(row.value("check_out").toString(), "hh:mm").isValid())) continue;
        if (!preview.dates.contains(date)) {
            preview.dates.append(date);
            if (AttendanceStorage::hasArrivalRecord(date)) ++preview.overwrittenCount;
        }
        if (row.value("hasCustomSchedule").toBool() || row.value("customSchedule").isObject()) {
            if (!row.value("customSchedule").isObject()
                || !isValidScheduleJson(row.value("customSchedule").toObject())
                || !isValidWorkSchedule(scheduleFromJson(row.value("customSchedule").toObject()))) {
                preview.errorMessage = QStringLiteral("文件中的单日作息设置无效，未导入任何记录。");
                return preview;
            }
        }
        if (!isValidScheduleJson(row)) {
            preview.errorMessage = QStringLiteral("文件中的工作制度字段格式无效，未导入任何记录。");
            return preview;
        }
        for (const QString& key : {QStringLiteral("workStart"), QStringLiteral("workEnd"), QStringLiteral("lunchStart"),
             QStringLiteral("lunchEnd"), QStringLiteral("dinnerStart"), QStringLiteral("dinnerEnd"),
             QStringLiteral("lunchBreakEnabled"), QStringLiteral("dinnerBreakEnabled"), QStringLiteral("mealAllowanceTime"),
             QStringLiteral("showMealAllowanceMarker")}) {
            if (row.contains(key)) preview.hasWorkSchedule = true;
        }
        imported = scheduleFromJson(row, imported);
    }
    if (preview.hasWorkSchedule && !isValidWorkSchedule(imported)) {
        preview.errorMessage = QStringLiteral("文件中的工作制度无效，未导入任何记录。");
        return preview;
    }
    preview.success = true;
    return preview;
}

AttendanceImportResult AttendanceJsonService::importFromLarkJson(const QString& filePath) {
    const auto preview = previewImport(filePath);
    if (!preview.success) return {false, 0, preview.errorMessage};
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
        if (!QTime::fromString(checkIn, "hh:mm").isValid()
            && !QTime::fromString(checkOut, "hh:mm").isValid()) {
            continue;
        }

        AttendanceStorage::upsertCheckTimes(date, checkIn, checkOut);
        AttendanceRecord record = AttendanceStorage::loadRecord(date);
        if (row.value("note").isString()) record.note = row.value("note").toString();
        if (row.value("needAverageCal").isBool()) record.needAverageCal = row.value("needAverageCal").toBool();
        if (row.value("excludeStandardOvertime").isBool()) record.excludeStandardOvertime = row.value("excludeStandardOvertime").toBool();
        if (row.value("hasCustomSchedule").isBool() || row.value("customSchedule").isObject()) {
            record.hasCustomSchedule = row.value("hasCustomSchedule").isBool()
                ? row.value("hasCustomSchedule").toBool() : true;
            record.customScheduleJson = record.hasCustomSchedule
                ? QString::fromUtf8(QJsonDocument(row.value("customSchedule").toObject()).toJson(QJsonDocument::Compact)) : QString();
            if (record.hasCustomSchedule) record.customSchedule = scheduleFromJson(row.value("customSchedule").toObject());
        }
        AttendanceStorage::saveRecord(date, record);
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
        if (row.value("showMealAllowanceMarker").isBool()) {
            importedSchedule.showMealAllowanceMarker =
                row.value("showMealAllowanceMarker").toBool();
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
        row["showMealAllowanceMarker"] = schedule.showMealAllowanceMarker;
        row["needAverageCal"] = record.needAverageCal;
        row["excludeStandardOvertime"] = record.excludeStandardOvertime;
        row["hasCustomSchedule"] = record.hasCustomSchedule;
        if (record.hasCustomSchedule) row["customSchedule"] = QJsonDocument::fromJson(recordScheduleJson(record).toUtf8()).object();
        row["note"] = record.note.isNull() ? QStringLiteral("") : record.note;
        rows.append(row);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return { false, true, 0, QStringLiteral("无法保存文件，请检查权限或路径") };
    }

    file.write(QJsonDocument(rows).toJson());
    return { true, true, rows.size(), QString() };
}
