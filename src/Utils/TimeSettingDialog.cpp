#include "TimeSettingDialog.h"
#include "CollapsibleGroupBox.h"
#include "WorkTimeCalculator.h"
#include "Data/AttendanceStorage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>

TimeSettingDialog::TimeSettingDialog(const QDate& date, QWidget* parent)
    : QDialog(parent), m_date(date) {
    setWindowTitle(QString("设置打卡时间 - %1").arg(date.toString("yyyy-MM-dd")));
    setModal(true);
    resize(450, 400);

    setupUI();
    loadRecord();
}

AttendanceRecord TimeSettingDialog::getRecord() const {
    AttendanceRecord record;
    record.needAverageCal = m_needAverageCalCheckBox->isChecked();
    record.arrivalTime = m_arrivalTimeEdit->time();
    record.departureTime = m_departureTimeEdit->time();
    record.workStartTime = m_workStartTimeEdit->time();
    record.workEndTime = m_workEndTimeEdit->time();
    record.lunchBreakStart = m_lunchBreakStartEdit->time();
    record.lunchBreakEnd = m_lunchBreakEndEdit->time();
    record.dinnerBreakStart = m_dinnerBreakStartEdit->time();
    record.dinnerBreakEnd = m_dinnerBreakEndEdit->time();
    return record;
}

void TimeSettingDialog::calculateWorkTime() {
    AttendanceRecord record = getRecord();
    WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(record);

    QString resultText;

    // 显示迟到早退
    if (result.lateMinutes > 0) {
        resultText += QString("[迟到] %1小时%2分钟\n")
            .arg(result.lateMinutes / 60)
            .arg(result.lateMinutes % 60);
    }

    if (result.earlyLeaveMinutes > 0) {
        resultText += QString("[早退] %1小时%2分钟\n")
            .arg(result.earlyLeaveMinutes / 60)
            .arg(result.earlyLeaveMinutes % 60);
    }

    // 显示工作时间
    resultText += QString("[实际工作] %1小时%2分钟\n")
        .arg(result.actualWorkMinutes / 60)
        .arg(result.actualWorkMinutes % 60);

    resultText += QString("[标准工作] %1小时%2分钟\n")
        .arg(result.standardWorkMinutes / 60)
        .arg(result.standardWorkMinutes % 60);

    resultText += QString("[总休息] %1小时%2分钟\n")
        .arg(result.totalBreakMinutes / 60)
        .arg(result.totalBreakMinutes % 60);

    // 显示加班或欠时
    if (result.overtimeMinutes > 0) {
        resultText += QString("[加班时间] %1小时%2分钟")
            .arg(result.overtimeMinutes / 60)
            .arg(result.overtimeMinutes % 60);
    }
    else if (result.overtimeMinutes < 0) {
        resultText += QString("[欠缺时间] %1小时%2分钟")
            .arg((-result.overtimeMinutes) / 60)
            .arg((-result.overtimeMinutes) % 60);
    }
    else {
        resultText += QString("[完成标准时间]");
    }

    m_resultLabel->setText(resultText);
}

void TimeSettingDialog::saveAndClose() {
    saveRecord();
    accept();
}

void TimeSettingDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 基本时间设置组
    QGroupBox* basicTimeGroup = new QGroupBox(QString("基本时间"));
    QGridLayout* basicTimeLayout = new QGridLayout(basicTimeGroup);

    basicTimeLayout->addWidget(new QLabel(QString("到达公司时间:")), 0, 0);
    m_arrivalTimeEdit = new QTimeEdit();
    m_arrivalTimeEdit->setDisplayFormat("hh:mm");
    basicTimeLayout->addWidget(m_arrivalTimeEdit, 0, 1);

    basicTimeLayout->addWidget(new QLabel(QString("离开公司时间:")), 1, 0);
    m_departureTimeEdit = new QTimeEdit();
    m_departureTimeEdit->setDisplayFormat("hh:mm");
    basicTimeLayout->addWidget(m_departureTimeEdit, 1, 1);

    mainLayout->addWidget(basicTimeGroup);

    // 可折叠的详细设置
    CollapsibleGroupBox* detailsGroup = new CollapsibleGroupBox(QString("详细设置"), this);

    QVBoxLayout* detailsLayout = new QVBoxLayout();
    // 计入平均加班时间选择框
    QGroupBox* needAverageGroup = new QGroupBox(QString(""));
    QGridLayout* needAverageLayout = new QGridLayout(needAverageGroup);
    m_needAverageCalCheckBox = new QCheckBox("计入平均加班计算：");
    m_needAverageCalCheckBox->setLayoutDirection(Qt::RightToLeft);
    m_needAverageCalCheckBox->setChecked(true);
    needAverageLayout->addWidget(m_needAverageCalCheckBox);

    // 标准工作时间
    QGroupBox* standardGroup = new QGroupBox(QString("标准工作时间"));
    QGridLayout* standardLayout = new QGridLayout(standardGroup);

    standardLayout->addWidget(new QLabel(QString("标准上班时间:")), 0, 0);
    m_workStartTimeEdit = new QTimeEdit();
    m_workStartTimeEdit->setDisplayFormat("hh:mm");
    standardLayout->addWidget(m_workStartTimeEdit, 0, 1);

    standardLayout->addWidget(new QLabel(QString("标准下班时间:")), 1, 0);
    m_workEndTimeEdit = new QTimeEdit();
    m_workEndTimeEdit->setDisplayFormat("hh:mm");
    standardLayout->addWidget(m_workEndTimeEdit, 1, 1);

    // 休息时间设置
    QGroupBox* breakGroup = new QGroupBox(QString("休息时间设置"));
    QGridLayout* breakLayout = new QGridLayout(breakGroup);

    breakLayout->addWidget(new QLabel(QString("午餐开始时间:")), 0, 0);
    m_lunchBreakStartEdit = new QTimeEdit();
    m_lunchBreakStartEdit->setDisplayFormat("hh:mm");
    breakLayout->addWidget(m_lunchBreakStartEdit, 0, 1);

    breakLayout->addWidget(new QLabel(QString("午餐结束时间:")), 1, 0);
    m_lunchBreakEndEdit = new QTimeEdit();
    m_lunchBreakEndEdit->setDisplayFormat("hh:mm");
    breakLayout->addWidget(m_lunchBreakEndEdit, 1, 1);

    breakLayout->addWidget(new QLabel(QString("晚餐开始时间:")), 2, 0);
    m_dinnerBreakStartEdit = new QTimeEdit();
    m_dinnerBreakStartEdit->setDisplayFormat("hh:mm");
    breakLayout->addWidget(m_dinnerBreakStartEdit, 2, 1);

    breakLayout->addWidget(new QLabel(QString("晚餐结束时间:")), 3, 0);
    m_dinnerBreakEndEdit = new QTimeEdit();
    m_dinnerBreakEndEdit->setDisplayFormat("hh:mm");
    breakLayout->addWidget(m_dinnerBreakEndEdit, 3, 1);

    detailsLayout->addWidget(needAverageGroup);
    detailsLayout->addWidget(standardGroup);
    detailsLayout->addWidget(breakGroup);
    detailsGroup->setContentLayout(detailsLayout);

    // 结果显示
    QGroupBox* resultGroup = new QGroupBox(QString("计算结果"));
    QVBoxLayout* resultLayout = new QVBoxLayout(resultGroup);
    m_resultLabel = new QLabel(QString(""));
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet("padding: 10px; background-color: #f0f0f0; border-radius: 5px;");
    resultLayout->addWidget(m_resultLabel);
    mainLayout->addWidget(resultGroup);
    mainLayout->addWidget(detailsGroup);

    // 按钮布局
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* saveBtn = new QPushButton(QString("保存"));
    QPushButton* cancelBtn = new QPushButton(QString("取消"));

    connect(saveBtn, &QPushButton::clicked, this, &TimeSettingDialog::saveAndClose);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    buttonLayout->addStretch();
    buttonLayout->addWidget(saveBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // 监听时间变化信号，自动更新计算
    connect(m_arrivalTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_departureTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_workStartTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_workEndTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_lunchBreakStartEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_lunchBreakEndEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_dinnerBreakStartEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_dinnerBreakEndEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
}

void TimeSettingDialog::loadRecord() {
    const AttendanceRecord record = AttendanceStorage::loadRecord(m_date);

    m_needAverageCalCheckBox->setChecked(record.needAverageCal);
    m_arrivalTimeEdit->setTime(record.arrivalTime);
    m_departureTimeEdit->setTime(record.departureTime);
    m_workStartTimeEdit->setTime(record.workStartTime);
    m_workEndTimeEdit->setTime(record.workEndTime);
    m_lunchBreakStartEdit->setTime(record.lunchBreakStart);
    m_lunchBreakEndEdit->setTime(record.lunchBreakEnd);
    m_dinnerBreakStartEdit->setTime(record.dinnerBreakStart);
    m_dinnerBreakEndEdit->setTime(record.dinnerBreakEnd);
}

void TimeSettingDialog::saveRecord() {
    AttendanceStorage::saveRecord(m_date, getRecord());
}
