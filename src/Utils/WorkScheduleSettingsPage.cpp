#include "WorkScheduleSettingsPage.h"

#include <ElaGroupBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>

#include <QGridLayout>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace {
QLabel* createFieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: #40566f; font-weight: 600;"));
    return label;
}

QLabel* createSectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: #223550; font-weight: 600;"));
    return label;
}
}

WorkScheduleSettingsPage::WorkScheduleSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workScheduleSettingsPage"));
    setMinimumSize(840, 560);
    setStyleSheet(QStringLiteral(
        "QWidget#workScheduleSettingsPage { background: #f7f9fc; }"
        "QTimeEdit { min-height: 32px; color: #223550; background: #ffffff;"
        " border: 1px solid #cfdbe7; border-radius: 5px; padding: 0 9px; }"
        "QTimeEdit:hover { border-color: #9ebdd8; }"
        "QTimeEdit:focus { border: 1px solid #5b9bd5; }"
        "QTimeEdit:disabled { color: #9aa8b5; background: #f3f5f7; border-color: #e0e6ec; }"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(36, 28, 36, 32);
    mainLayout->setSpacing(18);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);
    auto* title = new ElaText(QStringLiteral("工作制度"), 23, this);
    title->setStyleSheet(QStringLiteral("color: #12213d; font-weight: 600;"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    auto* saveButton = new ElaPushButton(QStringLiteral("保存设置"), this);
    saveButton->setMinimumSize(112, 34);
    saveButton->setCursor(Qt::PointingHandCursor);
    saveButton->setLightDefaultColor(QColor(QStringLiteral("#1769aa")));
    saveButton->setLightHoverColor(QColor(QStringLiteral("#0f5c9b")));
    saveButton->setLightTextColor(Qt::white);
    connect(saveButton, &ElaPushButton::clicked, this, &WorkScheduleSettingsPage::saveWorkSchedule);
    headerLayout->addWidget(saveButton);
    mainLayout->addLayout(headerLayout);

    auto* settingsPanel = new QWidget(this);
    settingsPanel->setMaximumWidth(760);
    auto* panelLayout = new QVBoxLayout(settingsPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(14);

    auto* workGroup = new ElaGroupBox(QStringLiteral("标准工作时间"), settingsPanel);
    workGroup->setStyleSheet(QStringLiteral(
        "ElaGroupBox { background: #ffffff; border: 1px solid #d8e3ee; border-radius: 6px;"
        " margin-top: 9px; padding-top: 6px; }"
        "ElaGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; color: #223550; font-weight: 600; }"));
    auto* workLayout = new QGridLayout(workGroup);
    workLayout->setContentsMargins(20, 24, 20, 18);
    workLayout->setHorizontalSpacing(12);
    workLayout->setVerticalSpacing(10);
    m_workStartTimeEdit = createTimeEdit();
    m_workEndTimeEdit = createTimeEdit();
    workLayout->addWidget(createFieldLabel(QStringLiteral("上班"), workGroup), 0, 0);
    workLayout->addWidget(m_workStartTimeEdit, 0, 1);
    workLayout->addWidget(createFieldLabel(QStringLiteral("下班"), workGroup), 0, 2);
    workLayout->addWidget(m_workEndTimeEdit, 0, 3);
    workLayout->setColumnStretch(1, 1);
    workLayout->setColumnStretch(3, 1);
    panelLayout->addWidget(workGroup);

    auto* breakGroup = new ElaGroupBox(QStringLiteral("休息时间"), settingsPanel);
    breakGroup->setStyleSheet(workGroup->styleSheet());
    auto* breakLayout = new QGridLayout(breakGroup);
    breakLayout->setContentsMargins(20, 24, 20, 18);
    breakLayout->setHorizontalSpacing(12);
    breakLayout->setVerticalSpacing(14);

    m_lunchBreakEnabledCheckBox = new ElaToggleSwitch(breakGroup);
    m_lunchBreakEnabledCheckBox->setToolTip(QStringLiteral("启用午休"));
    m_lunchBreakStartEdit = createTimeEdit();
    m_lunchBreakEndEdit = createTimeEdit();
    breakLayout->addWidget(createSectionLabel(QStringLiteral("午休"), breakGroup), 0, 0);
    breakLayout->addWidget(m_lunchBreakEnabledCheckBox, 0, 1);
    breakLayout->addWidget(createFieldLabel(QStringLiteral("开始"), breakGroup), 0, 2);
    breakLayout->addWidget(m_lunchBreakStartEdit, 0, 3);
    breakLayout->addWidget(createFieldLabel(QStringLiteral("结束"), breakGroup), 0, 4);
    breakLayout->addWidget(m_lunchBreakEndEdit, 0, 5);

    m_dinnerBreakEnabledCheckBox = new ElaToggleSwitch(breakGroup);
    m_dinnerBreakEnabledCheckBox->setToolTip(QStringLiteral("启用晚餐休息"));
    m_dinnerBreakStartEdit = createTimeEdit();
    m_dinnerBreakEndEdit = createTimeEdit();
    breakLayout->addWidget(createSectionLabel(QStringLiteral("晚餐"), breakGroup), 1, 0);
    breakLayout->addWidget(m_dinnerBreakEnabledCheckBox, 1, 1);
    breakLayout->addWidget(createFieldLabel(QStringLiteral("开始"), breakGroup), 1, 2);
    breakLayout->addWidget(m_dinnerBreakStartEdit, 1, 3);
    breakLayout->addWidget(createFieldLabel(QStringLiteral("结束"), breakGroup), 1, 4);
    breakLayout->addWidget(m_dinnerBreakEndEdit, 1, 5);
    breakLayout->setColumnStretch(3, 1);
    breakLayout->setColumnStretch(5, 1);
    panelLayout->addWidget(breakGroup);

    mainLayout->addWidget(settingsPanel);
    mainLayout->addStretch();

    connect(m_lunchBreakEnabledCheckBox, &ElaToggleSwitch::toggled,
        this, &WorkScheduleSettingsPage::updateLunchBreakState);
    connect(m_dinnerBreakEnabledCheckBox, &ElaToggleSwitch::toggled,
        this, &WorkScheduleSettingsPage::updateDinnerBreakState);
}

void WorkScheduleSettingsPage::setWorkSchedule(const WorkSchedule& schedule)
{
    m_workStartTimeEdit->setTime(schedule.workStartTime);
    m_workEndTimeEdit->setTime(schedule.workEndTime);
    m_lunchBreakEnabledCheckBox->setIsToggled(schedule.lunchBreakEnabled);
    m_lunchBreakStartEdit->setTime(schedule.lunchBreakStart);
    m_lunchBreakEndEdit->setTime(schedule.lunchBreakEnd);
    m_dinnerBreakEnabledCheckBox->setIsToggled(schedule.dinnerBreakEnabled);
    m_dinnerBreakStartEdit->setTime(schedule.dinnerBreakStart);
    m_dinnerBreakEndEdit->setTime(schedule.dinnerBreakEnd);
    updateLunchBreakState(schedule.lunchBreakEnabled);
    updateDinnerBreakState(schedule.dinnerBreakEnabled);
}

void WorkScheduleSettingsPage::updateLunchBreakState(bool enabled)
{
    m_lunchBreakStartEdit->setEnabled(enabled);
    m_lunchBreakEndEdit->setEnabled(enabled);
}

void WorkScheduleSettingsPage::updateDinnerBreakState(bool enabled)
{
    m_dinnerBreakStartEdit->setEnabled(enabled);
    m_dinnerBreakEndEdit->setEnabled(enabled);
}

void WorkScheduleSettingsPage::saveWorkSchedule()
{
    if (!hasValidTimeRange(m_workStartTimeEdit, m_workEndTimeEdit)
        || (m_lunchBreakEnabledCheckBox->getIsToggled()
            && !hasValidTimeRange(m_lunchBreakStartEdit, m_lunchBreakEndEdit))
        || (m_dinnerBreakEnabledCheckBox->getIsToggled()
            && !hasValidTimeRange(m_dinnerBreakStartEdit, m_dinnerBreakEndEdit))) {
        QMessageBox::warning(this, QStringLiteral("无法保存"),
            QStringLiteral("每个时间段的结束时间必须晚于开始时间。"));
        return;
    }

    WorkSchedule schedule;
    schedule.workStartTime = m_workStartTimeEdit->time();
    schedule.workEndTime = m_workEndTimeEdit->time();
    schedule.lunchBreakEnabled = m_lunchBreakEnabledCheckBox->getIsToggled();
    schedule.lunchBreakStart = m_lunchBreakStartEdit->time();
    schedule.lunchBreakEnd = m_lunchBreakEndEdit->time();
    schedule.dinnerBreakEnabled = m_dinnerBreakEnabledCheckBox->getIsToggled();
    schedule.dinnerBreakStart = m_dinnerBreakStartEdit->time();
    schedule.dinnerBreakEnd = m_dinnerBreakEndEdit->time();
    emit workScheduleSaved(schedule);
}

QTimeEdit* WorkScheduleSettingsPage::createTimeEdit()
{
    auto* editor = new QTimeEdit(this);
    editor->setDisplayFormat(QStringLiteral("HH:mm"));
    editor->setButtonSymbols(QAbstractSpinBox::NoButtons);
    editor->setMinimumWidth(116);
    editor->setFixedHeight(34);
    return editor;
}

bool WorkScheduleSettingsPage::hasValidTimeRange(const QTimeEdit* start, const QTimeEdit* end) const
{
    return start->time() < end->time();
}
