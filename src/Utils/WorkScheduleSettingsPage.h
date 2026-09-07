#pragma once

#include "AttendanceTypes.h"

#include <QWidget>

class ElaToggleSwitch;
class ElaPushButton;
class QGraphicsDropShadowEffect;
class QLabel;
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
    WorkSchedule currentWorkSchedule() const;
    void updateChangeState();
    void updateTimeEditChangeState(QTimeEdit* editor, const QTime& originalTime);
    void updateToggleChangeState(ElaToggleSwitch* toggle, QGraphicsDropShadowEffect* effect,
        bool changed, bool originalEnabled, const QString& label);

    WorkSchedule m_savedSchedule;
    QTimeEdit* m_workStartTimeEdit = nullptr;
    QTimeEdit* m_workEndTimeEdit = nullptr;
    ElaToggleSwitch* m_lunchBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_lunchBreakStartEdit = nullptr;
    QTimeEdit* m_lunchBreakEndEdit = nullptr;
    ElaToggleSwitch* m_dinnerBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_dinnerBreakStartEdit = nullptr;
    QTimeEdit* m_dinnerBreakEndEdit = nullptr;
    QTimeEdit* m_mealAllowanceTimeEdit = nullptr;
    ElaPushButton* m_saveButton = nullptr;
    QLabel* m_pendingChangesLabel = nullptr;
    QGraphicsDropShadowEffect* m_lunchBreakChangeEffect = nullptr;
    QGraphicsDropShadowEffect* m_dinnerBreakChangeEffect = nullptr;
};
