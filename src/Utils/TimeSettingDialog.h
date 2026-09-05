#pragma once

#include "AttendanceTypes.h"
#include <ElaDialog.h>

#include <QDate>

class ElaPlainTextEdit;
class ElaToggleSwitch;
class QLabel;
class QTimeEdit;

class TimeSettingDialog : public ElaDialog {
    Q_OBJECT

public:
    TimeSettingDialog(const QDate& date, const WorkSchedule& schedule, QWidget* parent = nullptr);
    AttendanceRecord getRecord() const;

private slots:
    void calculateWorkTime();
    void saveAndClose();

private:
    void setupUI();
    void loadRecord();

    QDate m_date;
    WorkSchedule m_schedule;
    ElaToggleSwitch* m_needAverageCalCheckBox = nullptr;
    QTimeEdit* m_arrivalTimeEdit = nullptr;
    QTimeEdit* m_departureTimeEdit = nullptr;
    ElaPlainTextEdit* m_noteEdit = nullptr;
    QLabel* m_resultLabel = nullptr;
};
