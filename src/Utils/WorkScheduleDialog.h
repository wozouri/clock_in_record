#pragma once

#include "AttendanceTypes.h"

#include <QDialog>

class QCheckBox;
class QTimeEdit;

class WorkScheduleDialog : public QDialog {
    Q_OBJECT

public:
    WorkScheduleDialog(const WorkSchedule& schedule, QWidget* parent = nullptr);
    WorkSchedule workSchedule() const;

private slots:
    void updateLunchBreakState(bool enabled);
    void updateDinnerBreakState(bool enabled);
    void saveAndClose();

private:
    QTimeEdit* createTimeEdit();
    bool hasValidTimeRange(const QTimeEdit* start, const QTimeEdit* end) const;

    QTimeEdit* m_workStartTimeEdit = nullptr;
    QTimeEdit* m_workEndTimeEdit = nullptr;
    QCheckBox* m_lunchBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_lunchBreakStartEdit = nullptr;
    QTimeEdit* m_lunchBreakEndEdit = nullptr;
    QCheckBox* m_dinnerBreakEnabledCheckBox = nullptr;
    QTimeEdit* m_dinnerBreakStartEdit = nullptr;
    QTimeEdit* m_dinnerBreakEndEdit = nullptr;
};
