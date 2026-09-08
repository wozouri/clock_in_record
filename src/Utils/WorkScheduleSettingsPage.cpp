#include "WorkScheduleSettingsPage.h"

#include <ElaGroupBox.h>
#include <ElaIcon.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>

#include <QGridLayout>
#include <QAbstractSpinBox>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QIntValidator>
#include <QLabel>
#include <QMessageBox>
#include <QStyle>
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
        "QTimeEdit[hasPendingChange=\"true\"] { color: #734500; background: #fff8e8;"
        " border: 1px solid #e6ad55; }"
        "QTimeEdit[hasPendingChange=\"true\"]:focus { border-color: #d78b20; }"
        "ElaLineEdit[hasPendingChange=\"true\"] { color: #734500; background: #fff8e8;"
        " border: 1px solid #e6ad55; }"
        "ElaLineEdit[hasPendingChange=\"true\"]:focus { border-color: #d78b20; }"
        "QTimeEdit:disabled { color: #9aa8b5; background: #f3f5f7; border-color: #e0e6ec; }"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(36, 28, 36, 32);
    mainLayout->setSpacing(18);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);
    auto* title = new ElaText(QStringLiteral("工作制度"), 23, this);
    title->setStyleSheet(QStringLiteral("color: #12213d; font-weight: 600;"));
    headerLayout->addWidget(title);
    m_pendingChangesLabel = new QLabel(QStringLiteral("未保存修改"), this);
    m_pendingChangesLabel->setStyleSheet(
        QStringLiteral("color: #a66308; font-weight: 600; padding-left: 6px;"));
    m_pendingChangesLabel->hide();
    headerLayout->addWidget(m_pendingChangesLabel);
    headerLayout->addStretch();

    m_saveButton = new ElaPushButton(QStringLiteral("保存设置"), this);
    m_saveButton->setMinimumSize(112, 34);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->setLightDefaultColor(QColor(QStringLiteral("#1769aa")));
    m_saveButton->setLightHoverColor(QColor(QStringLiteral("#0f5c9b")));
    m_saveButton->setLightTextColor(Qt::white);
    connect(m_saveButton, &ElaPushButton::clicked, this, &WorkScheduleSettingsPage::saveWorkSchedule);

    auto* aboutButton = new ElaPushButton(QStringLiteral("关于"), this);
    aboutButton->setIcon(ElaIcon::getInstance()->getElaIcon(ElaIconType::CircleInfo, 15));
    aboutButton->setIconSize(QSize(15, 15));
    aboutButton->setMinimumSize(86, 34);
    aboutButton->setCursor(Qt::PointingHandCursor);
    aboutButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #40566f; background: #ffffff; border: 1px solid #cfdbe7;"
        " border-radius: 5px; padding: 0 12px; }"
        "QPushButton:hover { color: #1769aa; background: #edf4fb; border-color: #9ebdd8; }"));
    connect(aboutButton, &ElaPushButton::clicked, this, &WorkScheduleSettingsPage::aboutRequested);
    headerLayout->addWidget(aboutButton);
    headerLayout->addWidget(m_saveButton);
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

    auto* mealAllowanceGroup = new ElaGroupBox(QStringLiteral("餐补统计"), settingsPanel);
    mealAllowanceGroup->setStyleSheet(workGroup->styleSheet());
    auto* mealAllowanceLayout = new QGridLayout(mealAllowanceGroup);
    mealAllowanceLayout->setContentsMargins(20, 24, 20, 18);
    mealAllowanceLayout->setHorizontalSpacing(12);
    m_mealAllowanceTimeEdit = createTimeEdit();
    m_showMealAllowanceMarkerCheckBox = new ElaToggleSwitch(mealAllowanceGroup);
    m_showMealAllowanceMarkerCheckBox->setToolTip(QStringLiteral("在日历中显示餐补标志"));
    mealAllowanceLayout->addWidget(createFieldLabel(QStringLiteral("餐补起算"), mealAllowanceGroup), 0, 0);
    mealAllowanceLayout->addWidget(m_mealAllowanceTimeEdit, 0, 1);
    mealAllowanceLayout->addWidget(createFieldLabel(QStringLiteral("显示标志"), mealAllowanceGroup), 0, 2);
    mealAllowanceLayout->addWidget(m_showMealAllowanceMarkerCheckBox, 0, 3);
    mealAllowanceLayout->setColumnStretch(1, 1);
    mealAllowanceLayout->setColumnStretch(3, 1);
    panelLayout->addWidget(mealAllowanceGroup);

    auto* updateGroup = new ElaGroupBox(QStringLiteral("更新服务"), settingsPanel);
    updateGroup->setStyleSheet(workGroup->styleSheet());
    auto* updateLayout = new QGridLayout(updateGroup);
    updateLayout->setContentsMargins(20, 24, 20, 18);
    updateLayout->setHorizontalSpacing(12);
    m_updateServerHostEdit = new ElaLineEdit(updateGroup);
    m_updateServerHostEdit->setPlaceholderText(QStringLiteral("例如 192.168.3.35"));
    m_updateServerHostEdit->setMinimumWidth(220);
    m_updateServerHostEdit->setFixedHeight(34);
    m_updateServerPortEdit = new ElaLineEdit(updateGroup);
    m_updateServerPortEdit->setValidator(new QIntValidator(1, 65535, m_updateServerPortEdit));
    m_updateServerPortEdit->setMinimumWidth(100);
    m_updateServerPortEdit->setFixedHeight(34);
    updateLayout->addWidget(createFieldLabel(QStringLiteral("服务器 IP"), updateGroup), 0, 0);
    updateLayout->addWidget(m_updateServerHostEdit, 0, 1);
    updateLayout->addWidget(createFieldLabel(QStringLiteral("端口"), updateGroup), 0, 2);
    updateLayout->addWidget(m_updateServerPortEdit, 0, 3);
    updateLayout->setColumnStretch(1, 1);
    updateLayout->setColumnStretch(3, 1);
    panelLayout->addWidget(updateGroup);

    mainLayout->addWidget(settingsPanel);
    mainLayout->addStretch();

    m_lunchBreakChangeEffect = new QGraphicsDropShadowEffect(m_lunchBreakEnabledCheckBox);
    m_lunchBreakChangeEffect->setBlurRadius(10);
    m_lunchBreakChangeEffect->setOffset(0, 0);
    m_lunchBreakChangeEffect->setColor(QColor(QStringLiteral("#e6ad55")));
    m_lunchBreakChangeEffect->setEnabled(false);
    m_lunchBreakEnabledCheckBox->setGraphicsEffect(m_lunchBreakChangeEffect);

    m_dinnerBreakChangeEffect = new QGraphicsDropShadowEffect(m_dinnerBreakEnabledCheckBox);
    m_dinnerBreakChangeEffect->setBlurRadius(10);
    m_dinnerBreakChangeEffect->setOffset(0, 0);
    m_dinnerBreakChangeEffect->setColor(QColor(QStringLiteral("#e6ad55")));
    m_dinnerBreakChangeEffect->setEnabled(false);
    m_dinnerBreakEnabledCheckBox->setGraphicsEffect(m_dinnerBreakChangeEffect);

    m_mealAllowanceMarkerChangeEffect =
        new QGraphicsDropShadowEffect(m_showMealAllowanceMarkerCheckBox);
    m_mealAllowanceMarkerChangeEffect->setBlurRadius(10);
    m_mealAllowanceMarkerChangeEffect->setOffset(0, 0);
    m_mealAllowanceMarkerChangeEffect->setColor(QColor(QStringLiteral("#e6ad55")));
    m_mealAllowanceMarkerChangeEffect->setEnabled(false);
    m_showMealAllowanceMarkerCheckBox->setGraphicsEffect(m_mealAllowanceMarkerChangeEffect);

    connect(m_lunchBreakEnabledCheckBox, &ElaToggleSwitch::toggled,
        this, &WorkScheduleSettingsPage::updateLunchBreakState);
    connect(m_dinnerBreakEnabledCheckBox, &ElaToggleSwitch::toggled,
        this, &WorkScheduleSettingsPage::updateDinnerBreakState);
    const auto updateChanges = [this] { updateChangeState(); };
    connect(m_workStartTimeEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_workEndTimeEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_lunchBreakStartEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_lunchBreakEndEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_dinnerBreakStartEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_dinnerBreakEndEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_mealAllowanceTimeEdit, &QTimeEdit::timeChanged, this, updateChanges);
    connect(m_lunchBreakEnabledCheckBox, &ElaToggleSwitch::toggled, this, updateChanges);
    connect(m_dinnerBreakEnabledCheckBox, &ElaToggleSwitch::toggled, this, updateChanges);
    connect(m_showMealAllowanceMarkerCheckBox, &ElaToggleSwitch::toggled, this, updateChanges);
    connect(m_updateServerHostEdit, &ElaLineEdit::textChanged, this, updateChanges);
    connect(m_updateServerPortEdit, &ElaLineEdit::textChanged, this, updateChanges);
}

void WorkScheduleSettingsPage::setWorkSchedule(const WorkSchedule& schedule)
{
    m_savedSchedule = schedule;
    m_workStartTimeEdit->setTime(schedule.workStartTime);
    m_workEndTimeEdit->setTime(schedule.workEndTime);
    m_lunchBreakEnabledCheckBox->setIsToggled(schedule.lunchBreakEnabled);
    m_lunchBreakStartEdit->setTime(schedule.lunchBreakStart);
    m_lunchBreakEndEdit->setTime(schedule.lunchBreakEnd);
    m_dinnerBreakEnabledCheckBox->setIsToggled(schedule.dinnerBreakEnabled);
    m_dinnerBreakStartEdit->setTime(schedule.dinnerBreakStart);
    m_dinnerBreakEndEdit->setTime(schedule.dinnerBreakEnd);
    m_mealAllowanceTimeEdit->setTime(schedule.mealAllowanceTime);
    m_showMealAllowanceMarkerCheckBox->setIsToggled(schedule.showMealAllowanceMarker);
    updateLunchBreakState(schedule.lunchBreakEnabled);
    updateDinnerBreakState(schedule.dinnerBreakEnabled);
    updateChangeState();
}

void WorkScheduleSettingsPage::setUpdateServiceEndpoint(const QString& host, quint16 port)
{
    m_savedUpdateServerHost = host.trimmed();
    m_savedUpdateServerPort = port;
    m_updateServerHostEdit->setText(m_savedUpdateServerHost);
    m_updateServerPortEdit->setText(QString::number(m_savedUpdateServerPort));
    updateChangeState();
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

    QHostAddress serverAddress;
    const QString serverHost = m_updateServerHostEdit->text().trimmed();
    bool portValid = false;
    const uint portValue = m_updateServerPortEdit->text().toUInt(&portValid);
    if (!serverAddress.setAddress(serverHost)
        || serverAddress.protocol() != QAbstractSocket::IPv4Protocol
        || !portValid || portValue == 0 || portValue > 65535) {
        QMessageBox::warning(this, QStringLiteral("无法保存"),
            QStringLiteral("更新服务器需要填写有效的 IPv4 地址和端口。"));
        return;
    }

    const WorkSchedule schedule = currentWorkSchedule();
    const QString normalizedServerHost = serverAddress.toString();
    const quint16 serverPort = static_cast<quint16>(portValue);
    emit workScheduleSaved(schedule);
    if (normalizedServerHost != m_savedUpdateServerHost || serverPort != m_savedUpdateServerPort) {
        emit updateServiceEndpointSaved(normalizedServerHost, serverPort);
    }
    m_savedSchedule = schedule;
    m_savedUpdateServerHost = normalizedServerHost;
    m_savedUpdateServerPort = serverPort;
    updateChangeState();
}

WorkSchedule WorkScheduleSettingsPage::currentWorkSchedule() const
{
    WorkSchedule schedule;
    schedule.workStartTime = m_workStartTimeEdit->time();
    schedule.workEndTime = m_workEndTimeEdit->time();
    schedule.lunchBreakEnabled = m_lunchBreakEnabledCheckBox->getIsToggled();
    schedule.lunchBreakStart = m_lunchBreakStartEdit->time();
    schedule.lunchBreakEnd = m_lunchBreakEndEdit->time();
    schedule.dinnerBreakEnabled = m_dinnerBreakEnabledCheckBox->getIsToggled();
    schedule.dinnerBreakStart = m_dinnerBreakStartEdit->time();
    schedule.dinnerBreakEnd = m_dinnerBreakEndEdit->time();
    schedule.mealAllowanceTime = m_mealAllowanceTimeEdit->time();
    schedule.showMealAllowanceMarker = m_showMealAllowanceMarkerCheckBox->getIsToggled();
    return schedule;
}

void WorkScheduleSettingsPage::updateChangeState()
{
    const WorkSchedule current = currentWorkSchedule();
    const bool hasChanges = current.workStartTime != m_savedSchedule.workStartTime
        || current.workEndTime != m_savedSchedule.workEndTime
        || current.lunchBreakEnabled != m_savedSchedule.lunchBreakEnabled
        || current.lunchBreakStart != m_savedSchedule.lunchBreakStart
        || current.lunchBreakEnd != m_savedSchedule.lunchBreakEnd
        || current.dinnerBreakEnabled != m_savedSchedule.dinnerBreakEnabled
        || current.dinnerBreakStart != m_savedSchedule.dinnerBreakStart
        || current.dinnerBreakEnd != m_savedSchedule.dinnerBreakEnd
        || current.mealAllowanceTime != m_savedSchedule.mealAllowanceTime
        || current.showMealAllowanceMarker != m_savedSchedule.showMealAllowanceMarker
        || m_updateServerHostEdit->text().trimmed() != m_savedUpdateServerHost
        || m_updateServerPortEdit->text().trimmed() != QString::number(m_savedUpdateServerPort);
    m_pendingChangesLabel->setVisible(hasChanges);
    m_saveButton->setEnabled(hasChanges);

    updateTimeEditChangeState(m_workStartTimeEdit, m_savedSchedule.workStartTime);
    updateTimeEditChangeState(m_workEndTimeEdit, m_savedSchedule.workEndTime);
    updateTimeEditChangeState(m_lunchBreakStartEdit, m_savedSchedule.lunchBreakStart);
    updateTimeEditChangeState(m_lunchBreakEndEdit, m_savedSchedule.lunchBreakEnd);
    updateTimeEditChangeState(m_dinnerBreakStartEdit, m_savedSchedule.dinnerBreakStart);
    updateTimeEditChangeState(m_dinnerBreakEndEdit, m_savedSchedule.dinnerBreakEnd);
    updateTimeEditChangeState(m_mealAllowanceTimeEdit, m_savedSchedule.mealAllowanceTime);
    updateLineEditChangeState(m_updateServerHostEdit, m_savedUpdateServerHost);
    updateLineEditChangeState(m_updateServerPortEdit, QString::number(m_savedUpdateServerPort));
    updateToggleChangeState(m_lunchBreakEnabledCheckBox, m_lunchBreakChangeEffect,
        current.lunchBreakEnabled != m_savedSchedule.lunchBreakEnabled,
        m_savedSchedule.lunchBreakEnabled, QStringLiteral("午休"));
    updateToggleChangeState(m_dinnerBreakEnabledCheckBox, m_dinnerBreakChangeEffect,
        current.dinnerBreakEnabled != m_savedSchedule.dinnerBreakEnabled,
        m_savedSchedule.dinnerBreakEnabled, QStringLiteral("晚餐休息"));
    updateToggleChangeState(m_showMealAllowanceMarkerCheckBox, m_mealAllowanceMarkerChangeEffect,
        current.showMealAllowanceMarker != m_savedSchedule.showMealAllowanceMarker,
        m_savedSchedule.showMealAllowanceMarker, QStringLiteral("餐补标志"));
}

void WorkScheduleSettingsPage::updateLineEditChangeState(ElaLineEdit* editor, const QString& originalValue)
{
    const bool changed = editor->text().trimmed() != originalValue;
    editor->setProperty("hasPendingChange", changed);
    editor->setToolTip(changed ? QStringLiteral("原设置：%1").arg(originalValue) : QString());
    editor->style()->unpolish(editor);
    editor->style()->polish(editor);
}

void WorkScheduleSettingsPage::updateTimeEditChangeState(QTimeEdit* editor, const QTime& originalTime)
{
    const bool changed = editor->time() != originalTime;
    editor->setProperty("hasPendingChange", changed);
    editor->setToolTip(changed
            ? QStringLiteral("原设置：%1").arg(originalTime.toString(QStringLiteral("HH:mm")))
            : QString());
    editor->style()->unpolish(editor);
    editor->style()->polish(editor);
}

void WorkScheduleSettingsPage::updateToggleChangeState(ElaToggleSwitch* toggle,
    QGraphicsDropShadowEffect* effect, bool changed, bool originalEnabled, const QString& label)
{
    effect->setEnabled(changed);
    toggle->setToolTip(changed
            ? QStringLiteral("%1已修改，原设置：%2").arg(label,
                  originalEnabled ? QStringLiteral("启用") : QStringLiteral("关闭"))
            : label);
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
