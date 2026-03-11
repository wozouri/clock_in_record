#ifndef TIMESETTINGDIALOG_H
#define TIMESETTINGDIALOG_H

#include "AttendanceTypes.h"
#include <QDialog>
#include <QTimeEdit>
#include <QLabel>
#include <QCheckBox>
#include <QDate>

class CollapsibleGroupBox;

// ʱ�����öԻ���
class TimeSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit TimeSettingDialog(const QDate& date, QWidget* parent = nullptr);
    AttendanceRecord getRecord() const;

private slots:
    void calculateWorkTime();
    void saveAndClose();

private:
    void setupUI();
    void loadRecord();

    QDate m_date;
    QCheckBox* m_needAverageCalCheckBox;
    QTimeEdit* m_arrivalTimeEdit;
    QTimeEdit* m_departureTimeEdit;
    QTimeEdit* m_workStartTimeEdit;
    QTimeEdit* m_workEndTimeEdit;
    QTimeEdit* m_lunchBreakStartEdit;
    QTimeEdit* m_lunchBreakEndEdit;
    QTimeEdit* m_dinnerBreakStartEdit;
    QTimeEdit* m_dinnerBreakEndEdit;
    QLabel* m_resultLabel;
};

#endif // TIMESETTINGDIALOG_H
