#include "WorkScheduleDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QTimeEdit>
#include <QVBoxLayout>

WorkScheduleDialog::WorkScheduleDialog(const WorkSchedule& schedule, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("工作制度设置"));
    setModal(true);
    setMinimumWidth(380);

    auto* mainLayout = new QVBoxLayout(this);

    auto* workGroup = new QGroupBox(QStringLiteral("标准工作时间"), this);
    auto* workLayout = new QFormLayout(workGroup);
    m_workStartTimeEdit = createTimeEdit();
    m_workEndTimeEdit = createTimeEdit();
    workLayout->addRow(QStringLiteral("上班时间："), m_workStartTimeEdit);
    workLayout->addRow(QStringLiteral("下班时间："), m_workEndTimeEdit);
    mainLayout->addWidget(workGroup);

    auto* breakGroup = new QGroupBox(QStringLiteral("休息时间"), this);
    auto* breakLayout = new QFormLayout(breakGroup);
    m_lunchBreakEnabledCheckBox = new QCheckBox(QStringLiteral("启用午休"), breakGroup);
    breakLayout->addRow(QString(), m_lunchBreakEnabledCheckBox);
    m_lunchBreakStartEdit = createTimeEdit();
    m_lunchBreakEndEdit = createTimeEdit();
    breakLayout->addRow(QStringLiteral("午休开始："), m_lunchBreakStartEdit);
    breakLayout->addRow(QStringLiteral("午休结束："), m_lunchBreakEndEdit);

    m_dinnerBreakEnabledCheckBox = new QCheckBox(QStringLiteral("启用晚餐休息"), breakGroup);
    breakLayout->addRow(QString(), m_dinnerBreakEnabledCheckBox);
    m_dinnerBreakStartEdit = createTimeEdit();
    m_dinnerBreakEndEdit = createTimeEdit();
    breakLayout->addRow(QStringLiteral("晚餐开始："), m_dinnerBreakStartEdit);
    breakLayout->addRow(QStringLiteral("晚餐结束："), m_dinnerBreakEndEdit);
    mainLayout->addWidget(breakGroup);

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &WorkScheduleDialog::saveAndClose);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    m_workStartTimeEdit->setTime(schedule.workStartTime);
    m_workEndTimeEdit->setTime(schedule.workEndTime);
    m_lunchBreakEnabledCheckBox->setChecked(schedule.lunchBreakEnabled);
    m_lunchBreakStartEdit->setTime(schedule.lunchBreakStart);
    m_lunchBreakEndEdit->setTime(schedule.lunchBreakEnd);
    m_dinnerBreakEnabledCheckBox->setChecked(schedule.dinnerBreakEnabled);
    m_dinnerBreakStartEdit->setTime(schedule.dinnerBreakStart);
    m_dinnerBreakEndEdit->setTime(schedule.dinnerBreakEnd);
    updateLunchBreakState(schedule.lunchBreakEnabled);
    updateDinnerBreakState(schedule.dinnerBreakEnabled);

    connect(m_lunchBreakEnabledCheckBox, &QCheckBox::toggled,
        this, &WorkScheduleDialog::updateLunchBreakState);
    connect(m_dinnerBreakEnabledCheckBox, &QCheckBox::toggled,
        this, &WorkScheduleDialog::updateDinnerBreakState);
}

WorkSchedule WorkScheduleDialog::workSchedule() const
{
    WorkSchedule schedule;
    schedule.workStartTime = m_workStartTimeEdit->time();
    schedule.workEndTime = m_workEndTimeEdit->time();
    schedule.lunchBreakEnabled = m_lunchBreakEnabledCheckBox->isChecked();
    schedule.lunchBreakStart = m_lunchBreakStartEdit->time();
    schedule.lunchBreakEnd = m_lunchBreakEndEdit->time();
    schedule.dinnerBreakEnabled = m_dinnerBreakEnabledCheckBox->isChecked();
    schedule.dinnerBreakStart = m_dinnerBreakStartEdit->time();
    schedule.dinnerBreakEnd = m_dinnerBreakEndEdit->time();
    return schedule;
}

void WorkScheduleDialog::updateLunchBreakState(bool enabled)
{
    m_lunchBreakStartEdit->setEnabled(enabled);
    m_lunchBreakEndEdit->setEnabled(enabled);
}

void WorkScheduleDialog::updateDinnerBreakState(bool enabled)
{
    m_dinnerBreakStartEdit->setEnabled(enabled);
    m_dinnerBreakEndEdit->setEnabled(enabled);
}

void WorkScheduleDialog::saveAndClose()
{
    if (!hasValidTimeRange(m_workStartTimeEdit, m_workEndTimeEdit)
        || (m_lunchBreakEnabledCheckBox->isChecked()
            && !hasValidTimeRange(m_lunchBreakStartEdit, m_lunchBreakEndEdit))
        || (m_dinnerBreakEnabledCheckBox->isChecked()
            && !hasValidTimeRange(m_dinnerBreakStartEdit, m_dinnerBreakEndEdit))) {
        QMessageBox::warning(this, QStringLiteral("无法保存"),
            QStringLiteral("每个时间段的结束时间必须晚于开始时间。"));
        return;
    }

    accept();
}

QTimeEdit* WorkScheduleDialog::createTimeEdit()
{
    auto* editor = new QTimeEdit(this);
    editor->setDisplayFormat("hh:mm");
    return editor;
}

bool WorkScheduleDialog::hasValidTimeRange(const QTimeEdit* start, const QTimeEdit* end) const
{
    return start->time() < end->time();
}
