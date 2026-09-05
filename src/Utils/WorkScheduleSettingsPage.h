#pragma once

#include "AttendanceTypes.h"

#include <QWidget>

class ElaToggleSwitch;
class QTimeEdit;

class WorkScheduleSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit WorkScheduleSettingsPage(QWidget* parent = nullptr);
    void setWorkSchedule(const WorkSchedule& schedule);

signals:
    void workScheduleSaved(const WorkSchedule& schedule);

private slots:
    void updateLunchBreakState(bool enabled);
    void updateDinnerBreakState(bool enabled);
    void saveWorkSchedule();

private:
    QTimeEdit* createTimeEdit();
    bool hasValidTimeRange(const QTimeEdit* start, const QTimeEdit* end) const;

    QTimeEdit* m_workStartTimeEdit = nullptr;
    QTimeEdit* m_workEndTimeEdit = nullptr;
    ElaToggleSwitch* m_lunchBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_lunchBreakStartEdit = nullptr;
    QTimeEdit* m_lunchBreakEndEdit = nullptr;
    ElaToggleSwitch* m_dinnerBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_dinnerBreakStartEdit = nullptr;
    QTimeEdit* m_dinnerBreakEndEdit = nullptr;
};
