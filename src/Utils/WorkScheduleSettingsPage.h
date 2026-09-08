#pragma once

#include "AttendanceTypes.h"

#include <QWidget>

class ElaToggleSwitch;
class ElaPushButton;
class ElaLineEdit;
class QGraphicsDropShadowEffect;
class QLabel;
class QTimeEdit;

class WorkScheduleSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit WorkScheduleSettingsPage(QWidget* parent = nullptr);
    void setWorkSchedule(const WorkSchedule& schedule);
    void setUpdateServiceEndpoint(const QString& host, quint16 port);

signals:
    void workScheduleSaved(const WorkSchedule& schedule);
    void updateServiceEndpointSaved(const QString& host, quint16 port);
    void aboutRequested();

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
    void updateLineEditChangeState(ElaLineEdit* editor, const QString& originalValue);
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
    ElaToggleSwitch* m_showMealAllowanceMarkerCheckBox = nullptr;
    ElaLineEdit* m_updateServerHostEdit = nullptr;
    ElaLineEdit* m_updateServerPortEdit = nullptr;
    QString m_savedUpdateServerHost;
    quint16 m_savedUpdateServerPort = 47980;
    ElaPushButton* m_saveButton = nullptr;
    QLabel* m_pendingChangesLabel = nullptr;
    QGraphicsDropShadowEffect* m_lunchBreakChangeEffect = nullptr;
    QGraphicsDropShadowEffect* m_dinnerBreakChangeEffect = nullptr;
    QGraphicsDropShadowEffect* m_mealAllowanceMarkerChangeEffect = nullptr;
};
