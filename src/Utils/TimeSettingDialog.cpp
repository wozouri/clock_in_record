#include "TimeSettingDialog.h"

#include "Data/AttendanceStorage.h"
#include "WorkTimeCalculator.h"

#include <ElaGroupBox.h>
#include <ElaMessageBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaToggleSwitch.h>

#include <QAbstractSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace {

QString formatDuration(int minutes)
{
    return QStringLiteral("%1小时%2分钟").arg(minutes / 60).arg(minutes % 60);
}

QWidget* createTimeEditorRow(QTimeEdit* editor, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(editor, 1);

    auto* nowButton = new ElaPushButton(QStringLiteral("当前时间"), row);
    nowButton->setFixedSize(82, 34);
    nowButton->setToolTip(QStringLiteral("填入当前时间"));
    QObject::connect(nowButton, &ElaPushButton::clicked, editor, [editor]() {
        editor->setTime(QTime::currentTime());
    });
    layout->addWidget(nowButton);
    return row;
}

QString dialogGroupStyle()
{
    return QStringLiteral(
        "ElaGroupBox { background: #ffffff; border: 1px solid #d8e3ee; border-radius: 6px;"
        " margin-top: 9px; padding-top: 6px; }"
        "ElaGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px;"
        " color: #223550; font-weight: 600; }");
}

}

TimeSettingDialog::TimeSettingDialog(
    const QDate& date,
    const WorkSchedule& schedule,
    QWidget* parent)
    : ElaDialog(parent)
    , m_date(date)
    , m_schedule(schedule)
{
    setWindowTitle(QStringLiteral("记录考勤 - %1").arg(date.toString(QStringLiteral("yyyy-MM-dd"))));
    setModal(true);
    setMinimumWidth(480);
    setAppBarHeight(42);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);

    setupUI();
    loadRecord();
}

AttendanceRecord TimeSettingDialog::getRecord() const
{
    AttendanceRecord record;
    record.needAverageCal = m_needAverageCalCheckBox->getIsToggled();
    record.arrivalTime = m_arrivalTimeEdit->time();
    record.departureTime = m_departureTimeEdit->time();
    record.note = m_noteEdit->toPlainText().trimmed();
    return record;
}

void TimeSettingDialog::calculateWorkTime()
{
    const WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(getRecord(), m_schedule);
    QStringList lines;
    lines << QStringLiteral("实际工作  %1").arg(formatDuration(result.actualWorkMinutes));
    lines << QStringLiteral("标准工作  %1").arg(formatDuration(result.standardWorkMinutes));
    lines << QStringLiteral("休息扣除  %1").arg(formatDuration(result.totalBreakMinutes));

    if (result.lateMinutes > 0) {
        lines << QStringLiteral("迟到  %1").arg(formatDuration(result.lateMinutes));
    }
    if (result.earlyLeaveMinutes > 0) {
        lines << QStringLiteral("早退  %1").arg(formatDuration(result.earlyLeaveMinutes));
    }

    const QString overtimeText = result.overtimeMinutes >= 0
        ? QStringLiteral("加班  %1").arg(formatDuration(result.overtimeMinutes))
        : QStringLiteral("欠时  %1").arg(formatDuration(-result.overtimeMinutes));
    lines << overtimeText;
    m_resultLabel->setText(lines.join(QLatin1Char('\n')));
}

void TimeSettingDialog::saveAndClose()
{
    if (m_arrivalTimeEdit->time() >= m_departureTimeEdit->time()) {
        ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("无法保存"),
            QStringLiteral("离岗时间必须晚于到岗时间。"), 2000, this, 18);
        return;
    }

    accept();
}

void TimeSettingDialog::setupUI()
{
    setStyleSheet(QStringLiteral(
        "ElaDialog { background: #f7f9fc; }"
        "QTimeEdit { min-height: 32px; color: #223550; background: #ffffff;"
        " border: 1px solid #cfdbe7; border-radius: 5px; padding: 0 9px; }"
        "QTimeEdit:hover { border-color: #9ebdd8; }"
        "QTimeEdit:focus { border-color: #5b9bd5; }"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 22);
    mainLayout->setSpacing(14);

    auto* recordGroup = new ElaGroupBox(QStringLiteral("当日记录"), this);
    recordGroup->setStyleSheet(dialogGroupStyle());
    auto* recordLayout = new QFormLayout(recordGroup);
    recordLayout->setContentsMargins(20, 23, 20, 16);
    recordLayout->setHorizontalSpacing(14);
    recordLayout->setVerticalSpacing(11);
    recordLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_arrivalTimeEdit = new QTimeEdit(recordGroup);
    m_arrivalTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_arrivalTimeEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_arrivalTimeEdit->setFixedHeight(34);
    recordLayout->addRow(QStringLiteral("到岗时间"), createTimeEditorRow(m_arrivalTimeEdit, recordGroup));

    m_departureTimeEdit = new QTimeEdit(recordGroup);
    m_departureTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_departureTimeEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_departureTimeEdit->setFixedHeight(34);
    recordLayout->addRow(QStringLiteral("离岗时间"), createTimeEditorRow(m_departureTimeEdit, recordGroup));

    auto* statisticsRow = new QWidget(recordGroup);
    auto* statisticsLayout = new QHBoxLayout(statisticsRow);
    statisticsLayout->setContentsMargins(0, 0, 0, 0);
    statisticsLayout->setSpacing(9);
    m_needAverageCalCheckBox = new ElaToggleSwitch(statisticsRow);
    statisticsLayout->addWidget(m_needAverageCalCheckBox);
    auto* statisticsLabel = new QLabel(QStringLiteral("计入工作日统计"), statisticsRow);
    statisticsLabel->setStyleSheet(QStringLiteral("color: #40566f;"));
    statisticsLayout->addWidget(statisticsLabel);
    statisticsLayout->addStretch();
    recordLayout->addRow(QStringLiteral("统计"), statisticsRow);

    m_noteEdit = new ElaPlainTextEdit(recordGroup);
    m_noteEdit->setPlaceholderText(QStringLiteral("添加备注（可选）"));
    m_noteEdit->setFixedHeight(72);
    recordLayout->addRow(QStringLiteral("备注"), m_noteEdit);
    mainLayout->addWidget(recordGroup);

    auto* resultGroup = new ElaGroupBox(QStringLiteral("自动计算"), this);
    resultGroup->setStyleSheet(dialogGroupStyle());
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->setContentsMargins(20, 23, 20, 15);
    m_resultLabel = new QLabel(resultGroup);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet(QStringLiteral(
        "padding: 9px 12px; color: #314861; background: #f2f7fb; border-radius: 4px;"));
    resultLayout->addWidget(m_resultLabel);
    mainLayout->addWidget(resultGroup);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 2, 0, 0);
    actionLayout->addStretch();
    auto* cancelButton = new ElaPushButton(QStringLiteral("取消"), this);
    cancelButton->setMinimumSize(84, 34);
    connect(cancelButton, &ElaPushButton::clicked, this, &QDialog::reject);
    actionLayout->addWidget(cancelButton);
    auto* saveButton = new ElaPushButton(QStringLiteral("保存"), this);
    saveButton->setMinimumSize(84, 34);
    saveButton->setLightDefaultColor(QColor(QStringLiteral("#1769aa")));
    saveButton->setLightHoverColor(QColor(QStringLiteral("#0f5c9b")));
    saveButton->setLightTextColor(Qt::white);
    connect(saveButton, &ElaPushButton::clicked, this, &TimeSettingDialog::saveAndClose);
    actionLayout->addWidget(saveButton);
    mainLayout->addLayout(actionLayout);

    connect(m_arrivalTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_departureTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
}

void TimeSettingDialog::loadRecord()
{
    AttendanceRecord record;
    if (AttendanceStorage::hasArrivalRecord(m_date)) {
        record = AttendanceStorage::loadRecord(m_date);
    } else {
        record.arrivalTime = m_schedule.workStartTime;
        record.departureTime = m_schedule.workEndTime;
    }

    m_needAverageCalCheckBox->setIsToggled(record.needAverageCal);
    m_arrivalTimeEdit->setTime(record.arrivalTime);
    m_departureTimeEdit->setTime(record.departureTime);
    m_noteEdit->setPlainText(record.note);
    calculateWorkTime();
}
