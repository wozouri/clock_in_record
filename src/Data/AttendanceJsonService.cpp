#include "AttendanceJsonService.h"
#include "AttendanceStorage.h"
#include "Types/AttendanceTypes.h"
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
        importedCount++;
    }

    return { true, importedCount, QString() };
}

AttendanceExportResult AttendanceJsonService::exportToJson(const QString& filePath) {
    const QStringList dates = AttendanceStorage::recordedDates();
    if (dates.isEmpty()) {
        return { true, false, 0, QString() };
    }

    QJsonArray rows;
    for (const QString& dateText : dates) {
        const QDate date = QDate::fromString(dateText, "yyyy-MM-dd");
        const AttendanceRecord record = AttendanceStorage::loadRecord(date);

        QJsonObject row;
        row["date"] = dateText;
        row["check_in"] = record.arrivalTime.toString("hh:mm");
        row["check_out"] = record.departureTime.toString("hh:mm");
        row["workStart"] = record.workStartTime.toString("hh:mm");
        row["workEnd"] = record.workEndTime.toString("hh:mm");
        row["needAverageCal"] = record.needAverageCal;
        rows.append(row);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return { false, true, 0, QStringLiteral("无法保存文件，请检查权限或路径") };
    }

    file.write(QJsonDocument(rows).toJson());
    return { true, true, rows.size(), QString() };
}
