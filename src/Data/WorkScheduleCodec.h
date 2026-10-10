#pragma once
#include "Types/AttendanceTypes.h"
#include <QJsonDocument>
#include <QJsonObject>

inline bool workSchedulesEqual(const WorkSchedule& a, const WorkSchedule& b)
{
    return a.workStartTime == b.workStartTime && a.workEndTime == b.workEndTime
        && a.lunchBreakEnabled == b.lunchBreakEnabled && a.lunchBreakStart == b.lunchBreakStart
        && a.lunchBreakEnd == b.lunchBreakEnd && a.dinnerBreakEnabled == b.dinnerBreakEnabled
        && a.dinnerBreakStart == b.dinnerBreakStart && a.dinnerBreakEnd == b.dinnerBreakEnd
        && a.mealAllowanceTime == b.mealAllowanceTime && a.showMealAllowanceMarker == b.showMealAllowanceMarker;
}
inline WorkSchedule scheduleFromJson(const QJsonObject& object, WorkSchedule schedule = {})
{
    auto time = [&object](const char* key, QTime& target) {
        const QTime parsed = QTime::fromString(object.value(key).toString(), "hh:mm");
        if (parsed.isValid()) target = parsed;
    };
    time("workStart", schedule.workStartTime); time("workEnd", schedule.workEndTime);
    time("lunchStart", schedule.lunchBreakStart); time("lunchEnd", schedule.lunchBreakEnd);
    time("dinnerStart", schedule.dinnerBreakStart); time("dinnerEnd", schedule.dinnerBreakEnd);
    time("mealAllowanceTime", schedule.mealAllowanceTime);
    if (object.value("lunchBreakEnabled").isBool()) schedule.lunchBreakEnabled = object.value("lunchBreakEnabled").toBool();
    if (object.value("dinnerBreakEnabled").isBool()) schedule.dinnerBreakEnabled = object.value("dinnerBreakEnabled").toBool();
    if (object.value("showMealAllowanceMarker").isBool()) schedule.showMealAllowanceMarker = object.value("showMealAllowanceMarker").toBool();
    return schedule;
}
inline QJsonObject scheduleToJson(const WorkSchedule& schedule, QJsonObject object = {})
{
    object["workStart"] = schedule.workStartTime.toString("hh:mm");
    object["workEnd"] = schedule.workEndTime.toString("hh:mm");
    object["lunchBreakEnabled"] = schedule.lunchBreakEnabled;
    object["lunchStart"] = schedule.lunchBreakStart.toString("hh:mm");
    object["lunchEnd"] = schedule.lunchBreakEnd.toString("hh:mm");
    object["dinnerBreakEnabled"] = schedule.dinnerBreakEnabled;
    object["dinnerStart"] = schedule.dinnerBreakStart.toString("hh:mm");
    object["dinnerEnd"] = schedule.dinnerBreakEnd.toString("hh:mm");
    object["mealAllowanceTime"] = schedule.mealAllowanceTime.toString("hh:mm");
    object["showMealAllowanceMarker"] = schedule.showMealAllowanceMarker;
    return object;
}
inline QString recordScheduleJson(const AttendanceRecord& record)
{
    if (!record.hasCustomSchedule) return record.customScheduleJson;
    const auto original = QJsonDocument::fromJson(record.customScheduleJson.toUtf8());
    if (original.isObject() && workSchedulesEqual(scheduleFromJson(original.object()), record.customSchedule))
        return record.customScheduleJson;
    return QString::fromUtf8(QJsonDocument(scheduleToJson(record.customSchedule, original.object())).toJson(QJsonDocument::Compact));
}
inline bool isValidWorkSchedule(const WorkSchedule& schedule)
{
    return schedule.workStartTime < schedule.workEndTime
        && (!schedule.lunchBreakEnabled || schedule.lunchBreakStart < schedule.lunchBreakEnd)
        && (!schedule.dinnerBreakEnabled || schedule.dinnerBreakStart < schedule.dinnerBreakEnd)
        && schedule.mealAllowanceTime.isValid();
}
inline bool isValidScheduleJson(const QJsonObject& object)
{
    for (const char* key : {"workStart", "workEnd", "lunchStart", "lunchEnd", "dinnerStart", "dinnerEnd", "mealAllowanceTime"}) {
        if (object.contains(key) && (!object.value(key).isString()
            || !QTime::fromString(object.value(key).toString(), "hh:mm").isValid())) return false;
    }
    for (const char* key : {"lunchBreakEnabled", "dinnerBreakEnabled", "showMealAllowanceMarker"}) {
        if (object.contains(key) && !object.value(key).isBool()) return false;
    }
    return true;
}
