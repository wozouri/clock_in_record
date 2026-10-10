#pragma once

#include "AttendanceTypes.h"
#include <ElaDialog.h>

#include <QDate>

class ElaPlainTextEdit;
class ElaToggleSwitch;
class ElaIconButton;
class ElaScrollArea;
class QLabel;
class QTimeEdit;
class QWidget;
class QVBoxLayout;
class QPropertyAnimation;
class QShowEvent;

class TimeSettingDialog : public ElaDialog {
    Q_OBJECT

public:
    TimeSettingDialog(const QDate& date, const WorkSchedule& schedule, QWidget* parent = nullptr);
    AttendanceRecord getRecord() const;
    bool isDeleteRequested() const { return m_deleteRequested; }

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void calculateWorkTime();
    void saveAndClose();

private:
    friend class LegacyUiTests;
    void setupUI();
    void loadRecord();
    void updateDialogSize(bool animate);
    QRect contentGeometry(const QRect& available) const;

    QDate m_date;
    WorkSchedule m_schedule;
    AttendanceRecord m_loadedRecord;
    bool m_deleteRequested = false;
    bool m_loadingRecord = true;
    bool m_initialPositioned = false;
    bool m_initialScreenSettled = false;
    QWidget* m_content = nullptr;
    QWidget* m_actions = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    ElaScrollArea* m_scrollArea = nullptr;
    QPropertyAnimation* m_resizeAnimation = nullptr;
    QLabel* m_scheduleHint = nullptr;
    ElaToggleSwitch* m_excludeStandardOvertime = nullptr;
    ElaToggleSwitch* m_customScheduleToggle = nullptr;
    ElaToggleSwitch* m_customLunchToggle = nullptr;
    ElaToggleSwitch* m_customDinnerToggle = nullptr;
    QWidget* m_customScheduleBody = nullptr;
    QTimeEdit* m_scheduleTimes[7] = {};
    ElaToggleSwitch* m_needAverageCalCheckBox = nullptr;
    QTimeEdit* m_arrivalTimeEdit = nullptr;
    QTimeEdit* m_departureTimeEdit = nullptr;
    ElaIconButton* m_noteEmojiButton = nullptr;
    ElaPlainTextEdit* m_noteEdit = nullptr;
    QLabel* m_resultValues[6] = {};
    QLabel* m_overtimeTitle = nullptr;
};
