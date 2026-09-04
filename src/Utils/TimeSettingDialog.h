#pragma once

#include "AttendanceTypes.h"

#include <QDate>
#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTimeEdit;

class TimeSettingDialog : public QDialog {
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
    QCheckBox* m_needAverageCalCheckBox = nullptr;
    QTimeEdit* m_arrivalTimeEdit = nullptr;
    QTimeEdit* m_departureTimeEdit = nullptr;
    QPlainTextEdit* m_noteEdit = nullptr;
    QLabel* m_resultLabel = nullptr;
};
