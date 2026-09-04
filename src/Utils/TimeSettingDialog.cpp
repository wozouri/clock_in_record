#include "TimeSettingDialog.h"

#include "Data/AttendanceStorage.h"
#include "WorkTimeCalculator.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace {

QString formatDuration(int minutes)
{
    return QString("%1小时%2分钟").arg(minutes / 60).arg(minutes % 60);
}

QWidget* timeEditorRow(QTimeEdit* editor, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(editor);

    auto* nowButton = new QPushButton(QStringLiteral("现在"), row);
    nowButton->setToolTip(QStringLiteral("填入当前时间"));
    QObject::connect(nowButton, &QPushButton::clicked, editor, [editor]() {
        editor->setTime(QTime::currentTime());
    });
    layout->addWidget(nowButton);
    return row;
}

}

TimeSettingDialog::TimeSettingDialog(
    const QDate& date,
    const WorkSchedule& schedule,
    QWidget* parent)
    : QDialog(parent)
    , m_date(date)
    , m_schedule(schedule)
{
    setWindowTitle(QString("记录考勤 - %1").arg(date.toString("yyyy-MM-dd")));
    setModal(true);
    setMinimumWidth(380);

    setupUI();
    loadRecord();
}

AttendanceRecord TimeSettingDialog::getRecord() const
{
    AttendanceRecord record;
    record.needAverageCal = m_needAverageCalCheckBox->isChecked();
    record.arrivalTime = m_arrivalTimeEdit->time();
    record.departureTime = m_departureTimeEdit->time();
    record.note = m_noteEdit->toPlainText().trimmed();
    return record;
}

void TimeSettingDialog::calculateWorkTime()
{
    const WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(getRecord(), m_schedule);
    QStringList lines;
    lines << QString("实际工作：%1").arg(formatDuration(result.actualWorkMinutes));
    lines << QString("标准工作：%1").arg(formatDuration(result.standardWorkMinutes));
    lines << QString("休息扣除：%1").arg(formatDuration(result.totalBreakMinutes));

    if (result.lateMinutes > 0) {
        lines << QString("迟到：%1").arg(formatDuration(result.lateMinutes));
    }
    if (result.earlyLeaveMinutes > 0) {
        lines << QString("早退：%1").arg(formatDuration(result.earlyLeaveMinutes));
    }

    const QString overtimeText = result.overtimeMinutes >= 0
        ? QString("加班：%1").arg(formatDuration(result.overtimeMinutes))
        : QString("欠时：%1").arg(formatDuration(-result.overtimeMinutes));
    lines << overtimeText;
    m_resultLabel->setText(lines.join('\n'));
}

void TimeSettingDialog::saveAndClose()
{
    if (m_arrivalTimeEdit->time() >= m_departureTimeEdit->time()) {
        QMessageBox::warning(this, QStringLiteral("无法保存"),
            QStringLiteral("离岗时间必须晚于到岗时间。"));
        return;
    }

    accept();
}

void TimeSettingDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* recordGroup = new QGroupBox(QStringLiteral("当日记录"), this);
    auto* recordLayout = new QFormLayout(recordGroup);

    m_arrivalTimeEdit = new QTimeEdit(recordGroup);
    m_arrivalTimeEdit->setDisplayFormat("hh:mm");
    recordLayout->addRow(QStringLiteral("到岗时间："), timeEditorRow(m_arrivalTimeEdit, recordGroup));

    m_departureTimeEdit = new QTimeEdit(recordGroup);
    m_departureTimeEdit->setDisplayFormat("hh:mm");
    recordLayout->addRow(QStringLiteral("离岗时间："), timeEditorRow(m_departureTimeEdit, recordGroup));

    m_needAverageCalCheckBox = new QCheckBox(QStringLiteral("计入工作日统计"), recordGroup);
    recordLayout->addRow(QString(), m_needAverageCalCheckBox);

    m_noteEdit = new QPlainTextEdit(recordGroup);
    m_noteEdit->setPlaceholderText(QStringLiteral("备注（可选）"));
    m_noteEdit->setFixedHeight(68);
    recordLayout->addRow(QStringLiteral("备注："), m_noteEdit);
    mainLayout->addWidget(recordGroup);

    auto* resultGroup = new QGroupBox(QStringLiteral("自动计算"), this);
    auto* resultLayout = new QVBoxLayout(resultGroup);
    m_resultLabel = new QLabel(resultGroup);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet("padding: 8px; background-color: #f5f5f5;");
    resultLayout->addWidget(m_resultLabel);
    mainLayout->addWidget(resultGroup);

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TimeSettingDialog::saveAndClose);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

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

    m_needAverageCalCheckBox->setChecked(record.needAverageCal);
    m_arrivalTimeEdit->setTime(record.arrivalTime);
    m_departureTimeEdit->setTime(record.departureTime);
    m_noteEdit->setPlainText(record.note);
    calculateWorkTime();
}
