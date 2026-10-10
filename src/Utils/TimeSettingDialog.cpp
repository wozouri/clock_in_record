#include "TimeSettingDialog.h"

#include "Data/AttendanceStorage.h"
#include "Data/WorkScheduleCodec.h"
#include "WorkTimeCalculator.h"
#include "ScreenLayout.h"

#include <ElaGroupBox.h>
#include <ElaIconButton.h>
#include <ElaMessageBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaToggleSwitch.h>

#include <QAbstractSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QStringList>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <iterator>

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

struct NoteEmoji {
    ElaIconType::IconName icon;
    const char* text;
    const char* description;
};

constexpr NoteEmoji kNoteEmojis[] = {
    {ElaIconType::FaceSmile, u8"🙂", "微笑"},
    {ElaIconType::FaceGrin, u8"😀", "开心"},
    {ElaIconType::FaceLaugh, u8"😄", "大笑"},
    {ElaIconType::FaceSmileWink, u8"😉", "眨眼"},
    {ElaIconType::FaceThinking, u8"🤔", "思考"},
    {ElaIconType::FaceGrinHearts, u8"🥰", "喜欢"},
    {ElaIconType::FaceGrinStars, u8"🤩", "惊喜"},
    {ElaIconType::FaceSadTear, u8"😢", "难过"},
    {ElaIconType::FaceTired, u8"😫", "疲惫"},
    {ElaIconType::FaceSunglasses, u8"😎", "轻松"},
    {ElaIconType::FaceParty, u8"🥳", "庆祝"},
    {ElaIconType::FaceSaluting, u8"🫡", "收到"},
};

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
    setFixedWidth(520);
    setIsFixedSize(true);
    setAppBarHeight(42);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);

    setupUI();
    m_resizeAnimation = new QPropertyAnimation(this, "geometry", this);
    m_resizeAnimation->setDuration(160);
    m_resizeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_resizeAnimation, &QPropertyAnimation::finished, this, [this] {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    });
    loadRecord();
    new WindowScreenTracker(this, [this] {
        // The first native show can settle screen/DPI asynchronously. Recenter
        // once after those notifications, then retain the user's position.
        if (!m_initialScreenSettled) m_initialPositioned = false;
        updateDialogSize(false);
        m_initialPositioned = true;
        m_initialScreenSettled = true;
    });
}

QRect TimeSettingDialog::contentGeometry(const QRect& available) const
{
    const auto margins = layout()->contentsMargins();
    const auto windowMargins = contentsMargins();
    const int height = m_content->height() + m_actions->sizeHint().height() + layout()->spacing()
        + margins.top() + margins.bottom() + windowMargins.top() + windowMargins.bottom();
    const QRect bounds = available.adjusted(12, 12, -12, -12);
    const QSize targetSize(width(), qMin(height, bounds.height()));
    QPoint anchor = pos();
    if (!m_initialPositioned) {
        const auto* owner = parentWidget() ? parentWidget()->window() : nullptr;
        const QPoint center = owner && owner->isVisible() ? owner->frameGeometry().center() : available.center();
        anchor = center - QPoint((targetSize.width() - 1) / 2, (targetSize.height() - 1) / 2);
    }
    return ScreenLayout::fittedGeometry(QRect(anchor, targetSize), available, 12);
}

void TimeSettingDialog::updateDialogSize(bool animate)
{
    if (m_loadingRecord) return;
    m_resizeAnimation->stop();
    for (int i = 0; i < m_contentLayout->count(); ++i) {
        if (auto* child = m_contentLayout->itemAt(i)->widget()) {
            if (auto* childLayout = child->layout()) {
                childLayout->invalidate();
                childLayout->activate();
            }
        }
    }
    m_contentLayout->invalidate();
    m_contentLayout->activate();
    m_content->setFixedHeight(m_contentLayout->totalSizeHint().height());
    layout()->invalidate();
    layout()->activate();
    const auto* owner = parentWidget() ? parentWidget()->window() : nullptr;
    QScreen* currentScreen = ScreenLayout::screenForWidget(!m_initialPositioned && owner ? owner : this);
    if (!currentScreen) return;
    if (!m_initialPositioned) {
        // Select the native monitor before calculating geometry. Otherwise the
        // first show can convert a secondary-screen position using primary DPI.
        winId();
        if (windowHandle()->screen() != currentScreen) windowHandle()->setScreen(currentScreen);
    }
    const QRect target = contentGeometry(currentScreen->availableGeometry());
    m_scrollArea->verticalScrollBar()->setValue(0);
    if (animate && isVisible() && geometry() != target) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_resizeAnimation->setStartValue(geometry());
        m_resizeAnimation->setEndValue(target);
        m_resizeAnimation->start();
    } else {
        setGeometry(target);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void TimeSettingDialog::showEvent(QShowEvent* event)
{
    ElaDialog::showEvent(event);
    updateDialogSize(false);
    m_initialPositioned = true;
}

AttendanceRecord TimeSettingDialog::getRecord() const
{
    AttendanceRecord record = m_loadedRecord;
    record.needAverageCal = m_needAverageCalCheckBox->getIsToggled();
    record.arrivalTime = m_arrivalTimeEdit->time();
    record.departureTime = m_departureTimeEdit->time();
    record.note = m_noteEdit->toPlainText().trimmed();
    record.excludeStandardOvertime = m_excludeStandardOvertime->getIsToggled();
    record.hasCustomSchedule = m_customScheduleToggle->getIsToggled();
    if (record.hasCustomSchedule) {
        record.customSchedule.workStartTime = m_scheduleTimes[0]->time();
        record.customSchedule.workEndTime = m_scheduleTimes[1]->time();
        record.customSchedule.lunchBreakStart = m_scheduleTimes[2]->time();
        record.customSchedule.lunchBreakEnd = m_scheduleTimes[3]->time();
        record.customSchedule.dinnerBreakStart = m_scheduleTimes[4]->time();
        record.customSchedule.dinnerBreakEnd = m_scheduleTimes[5]->time();
        record.customSchedule.mealAllowanceTime = m_scheduleTimes[6]->time();
        record.customSchedule.lunchBreakEnabled = m_customLunchToggle->getIsToggled();
        record.customSchedule.dinnerBreakEnabled = m_customDinnerToggle->getIsToggled();
    } else {
        record.customScheduleJson.clear();
    }
    return record;
}

void TimeSettingDialog::calculateWorkTime()
{
    const WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(getRecord(), m_schedule);
    const int values[]{result.actualWorkMinutes, result.standardWorkMinutes, result.totalBreakMinutes,
        qAbs(result.overtimeMinutes), result.lateMinutes, result.earlyLeaveMinutes};
    for (int i = 0; i < 6; ++i) m_resultValues[i]->setText(formatDuration(values[i]));
    m_overtimeTitle->setText(result.overtimeMinutes >= 0 ? QStringLiteral("加班") : QStringLiteral("欠时"));
}

void TimeSettingDialog::saveAndClose()
{
    if (getRecord().hasCustomSchedule && !isValidWorkSchedule(getRecord().customSchedule)) {
        ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("无法保存"),
            QStringLiteral("当天作息的每个时间段结束时间必须晚于开始时间。"), 2000, this, 18);
        return;
    }
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
        "QTimeEdit:focus { border-color: #5b9bd5; }"
        "QTimeEdit:disabled { color: #9aa7b5; background: #f4f6f8; border-color: #e1e7ed; }"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 12, 20, 16);
    mainLayout->setSpacing(12);
    m_scrollArea = new ElaScrollArea(this);
    m_scrollArea->setObjectName("recordEditorScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setMinimumHeight(0);
    m_scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    m_scrollArea->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    m_content = new QWidget;
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(10);
    m_scrollArea->setWidget(m_content);
    mainLayout->addWidget(m_scrollArea, 1);

    auto* recordGroup = new ElaGroupBox(QStringLiteral("当日记录"), this);
    recordGroup->setStyleSheet(dialogGroupStyle());
    auto* recordLayout = new QFormLayout(recordGroup);
    recordLayout->setContentsMargins(16, 21, 16, 14);
    recordLayout->setHorizontalSpacing(14);
    recordLayout->setVerticalSpacing(9);
    recordLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_arrivalTimeEdit = new QTimeEdit(recordGroup);
    m_arrivalTimeEdit->setObjectName("arrivalTimeEditor");
    m_arrivalTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_arrivalTimeEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_arrivalTimeEdit->setFixedHeight(34);
    recordLayout->addRow(QStringLiteral("到岗时间"), createTimeEditorRow(m_arrivalTimeEdit, recordGroup));

    m_departureTimeEdit = new QTimeEdit(recordGroup);
    m_departureTimeEdit->setObjectName("departureTimeEditor");
    m_departureTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_departureTimeEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_departureTimeEdit->setFixedHeight(34);
    recordLayout->addRow(QStringLiteral("离岗时间"), createTimeEditorRow(m_departureTimeEdit, recordGroup));

    auto* statisticsRow = new QWidget(recordGroup);
    auto* statisticsLayout = new QHBoxLayout(statisticsRow);
    statisticsLayout->setContentsMargins(0, 0, 0, 0);
    statisticsLayout->setSpacing(9);
    m_needAverageCalCheckBox = new ElaToggleSwitch(statisticsRow);
    m_needAverageCalCheckBox->setObjectName("includeWorkdayToggle");
    statisticsLayout->addWidget(m_needAverageCalCheckBox);
    auto* statisticsLabel = new QLabel(QStringLiteral("计入工作日统计"), statisticsRow);
    statisticsLabel->setStyleSheet(QStringLiteral("color: #40566f; font-size: 13px;"));
    statisticsLayout->addWidget(statisticsLabel);
    statisticsLayout->addSpacing(16);
    m_excludeStandardOvertime = new ElaToggleSwitch(statisticsRow);
    m_excludeStandardOvertime->setObjectName("excludeStandardOvertimeToggle");
    m_excludeStandardOvertime->setToolTip(QStringLiteral("仅在不计入工作日统计时可用；开启后仅工作时段外的时间计为加班，关闭后全部工时计为加班。"));
    statisticsLayout->addWidget(m_excludeStandardOvertime);
    auto* exclusionLabel = new QLabel(QStringLiteral("标准时段不计加班"), statisticsRow);
    exclusionLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #40566f; font-size: 13px; } QLabel:disabled { color: #9aa7b5; }"));
    exclusionLabel->setToolTip(m_excludeStandardOvertime->toolTip());
    statisticsLayout->addWidget(exclusionLabel);
    statisticsLayout->addStretch();
    recordLayout->addRow(QStringLiteral("统计"), statisticsRow);

    auto* noteHeader = new QWidget(recordGroup);
    auto* noteTools = new QHBoxLayout(noteHeader);
    noteTools->setContentsMargins(0, 0, 0, 0);
    noteTools->addWidget(new QLabel(QStringLiteral("备注"), noteHeader));
    noteTools->addStretch();
    m_noteEmojiButton = new ElaIconButton(ElaIconType::FaceSmile, 16, 28, 24, noteHeader);
    m_noteEmojiButton->setToolTip(QStringLiteral("插入表情"));
    m_noteEmojiButton->setCursor(Qt::PointingHandCursor);
    noteTools->addWidget(m_noteEmojiButton);
    recordLayout->addRow(noteHeader);

    m_noteEdit = new ElaPlainTextEdit(recordGroup);
    m_noteEdit->setObjectName("recordNoteEditor");
    m_noteEdit->setPlaceholderText(QStringLiteral("添加备注（可选）"));
    m_noteEdit->setFixedHeight(68);
    recordLayout->addRow(m_noteEdit);
    m_contentLayout->addWidget(recordGroup);

    auto* scheduleGroup = new ElaGroupBox(QStringLiteral("当日作息"), this);
    scheduleGroup->setStyleSheet(dialogGroupStyle());
    auto* scheduleLayout = new QVBoxLayout(scheduleGroup);
    scheduleLayout->setContentsMargins(16, 21, 16, 14);
    scheduleLayout->setSpacing(10);
    auto* toggleRow = new QHBoxLayout();
    m_customScheduleToggle = new ElaToggleSwitch(scheduleGroup);
    m_customScheduleToggle->setObjectName("customScheduleToggle");
    toggleRow->addWidget(m_customScheduleToggle);
    toggleRow->addWidget(new QLabel(QStringLiteral("使用当天独立作息"), scheduleGroup));
    toggleRow->addStretch();
    m_scheduleHint = new QLabel(scheduleGroup);
    m_scheduleHint->setStyleSheet(QStringLiteral("color: #7b8b9c; font-size: 11px;"));
    toggleRow->addWidget(m_scheduleHint);
    scheduleLayout->addLayout(toggleRow);
    m_customScheduleBody = new QWidget(scheduleGroup);
    m_customScheduleBody->setObjectName("customScheduleBody");
    auto* scheduleGrid = new QGridLayout(m_customScheduleBody);
    scheduleGrid->setContentsMargins(0, 2, 0, 0);
    scheduleGrid->setHorizontalSpacing(9);
    scheduleGrid->setVerticalSpacing(8);
    scheduleGrid->setColumnStretch(1, 1);
    scheduleGrid->setColumnStretch(3, 1);
    const QStringList labels{QStringLiteral("上班"), QStringLiteral("下班"), QStringLiteral("午休开始"),
        QStringLiteral("午休结束"), QStringLiteral("晚餐开始"), QStringLiteral("晚餐结束"), QStringLiteral("餐补起算")};
    for (int i = 0; i < 7; ++i) {
        m_scheduleTimes[i] = new QTimeEdit(m_customScheduleBody);
        m_scheduleTimes[i]->setObjectName(QStringLiteral("customScheduleTime%1").arg(i));
        m_scheduleTimes[i]->setDisplayFormat("HH:mm");
        m_scheduleTimes[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_scheduleTimes[i]->setFixedHeight(32);
        m_scheduleTimes[i]->setMinimumWidth(86);
        if (i < 2 || i == 6)
            scheduleGrid->addWidget(new QLabel(labels[i], m_customScheduleBody), i / 2, (i % 2) * 2);
        scheduleGrid->addWidget(m_scheduleTimes[i], i / 2, (i % 2) * 2 + 1);
    }
    m_customLunchToggle = new ElaToggleSwitch(m_customScheduleBody);
    m_customLunchToggle->setObjectName("customLunchToggle");
    m_customDinnerToggle = new ElaToggleSwitch(m_customScheduleBody);
    m_customDinnerToggle->setObjectName("customDinnerToggle");
    for (int row = 1; row <= 2; ++row) {
        auto* breakRow = new QHBoxLayout;
        breakRow->setContentsMargins(0, 0, 0, 0);
        breakRow->setSpacing(6);
        breakRow->addWidget(row == 1 ? m_customLunchToggle : m_customDinnerToggle);
        breakRow->addWidget(new QLabel(row == 1 ? QStringLiteral("午休") : QStringLiteral("晚餐"), m_customScheduleBody));
        scheduleGrid->addLayout(breakRow, row, 0);
        scheduleGrid->addWidget(new QLabel(QStringLiteral("至"), m_customScheduleBody), row, 2);
    }
    auto* mealHint = new QLabel(QStringLiteral("离岗达到此时间计餐补"), m_customScheduleBody);
    mealHint->setStyleSheet(QStringLiteral("color: #7b8b9c; font-size: 11px;"));
    scheduleGrid->addWidget(mealHint, 3, 2, 1, 2);
    scheduleLayout->addWidget(m_customScheduleBody);
    m_contentLayout->addWidget(scheduleGroup);

    auto* resultGroup = new ElaGroupBox(QStringLiteral("自动计算"), this);
    resultGroup->setStyleSheet(dialogGroupStyle());
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->setContentsMargins(16, 21, 16, 14);
    auto* resultBody = new QWidget(resultGroup);
    resultBody->setObjectName("workTimeResults");
    resultBody->setStyleSheet(QStringLiteral("QWidget#workTimeResults { background: #f2f7fb; border-radius: 4px; }"));
    auto* resultGrid = new QGridLayout(resultBody);
    resultGrid->setContentsMargins(10, 10, 10, 10);
    resultGrid->setHorizontalSpacing(8);
    resultGrid->setVerticalSpacing(8);
    const QStringList resultTitles{QStringLiteral("实际工作"), QStringLiteral("标准工作"), QStringLiteral("休息扣除"),
        QStringLiteral("加班"), QStringLiteral("迟到"), QStringLiteral("早退")};
    for (int i = 0; i < 6; ++i) {
        auto* title = new QLabel(resultTitles[i], resultBody);
        title->setStyleSheet(QStringLiteral("color: #63778d;"));
        if (i == 3) m_overtimeTitle = title;
        m_resultValues[i] = new QLabel(resultBody);
        m_resultValues[i]->setStyleSheet(i == 3
            ? QStringLiteral("color: #1769aa; font-weight: 600;")
            : QStringLiteral("color: #314861; font-weight: 600;"));
        resultGrid->addWidget(title, i / 2, (i % 2) * 2);
        resultGrid->addWidget(m_resultValues[i], i / 2, (i % 2) * 2 + 1, Qt::AlignRight);
    }
    resultLayout->addWidget(resultBody);
    m_contentLayout->addWidget(resultGroup);

    m_actions = new QWidget(this);
    m_actions->setObjectName("recordEditorActions");
    auto* actionLayout = new QHBoxLayout(m_actions);
    actionLayout->setContentsMargins(0, 2, 0, 0);
    auto* deleteButton = new ElaPushButton(QStringLiteral("删除记录"), this);
    deleteButton->setObjectName("deleteRecordButton");
    deleteButton->setVisible(AttendanceStorage::hasArrivalRecord(m_date));
    deleteButton->setMinimumSize(94, 34);
    connect(deleteButton, &ElaPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("删除记录"),
            QStringLiteral("确定删除当天的考勤记录吗？"), QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) == QMessageBox::Yes) {
            m_deleteRequested = true;
            accept();
        }
    });
    actionLayout->addWidget(deleteButton);
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
    mainLayout->addWidget(m_actions);

    connect(m_arrivalTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_departureTimeEdit, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_needAverageCalCheckBox, &ElaToggleSwitch::toggled, this, [this, exclusionLabel](bool enabled) {
        m_excludeStandardOvertime->setEnabled(!enabled);
        exclusionLabel->setEnabled(!enabled);
        calculateWorkTime();
    });
    connect(m_excludeStandardOvertime, &ElaToggleSwitch::toggled, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_customScheduleToggle, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        if (isVisible()) m_initialScreenSettled = true;
        m_customScheduleBody->setVisible(enabled);
        m_scheduleHint->setText(enabled ? QStringLiteral("仅影响当天") : QStringLiteral("使用全局工作制度"));
        calculateWorkTime();
        // 先让 Qt 完成子布局的显示状态更新，再读取高度，避免收起时沿用展开的最小尺寸。
        if (!m_loadingRecord) QTimer::singleShot(0, this, [this] { updateDialogSize(true); });
    });
    for (auto* editor : m_scheduleTimes) connect(editor, &QTimeEdit::timeChanged, this, &TimeSettingDialog::calculateWorkTime);
    connect(m_customLunchToggle, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        m_scheduleTimes[2]->setEnabled(enabled);
        m_scheduleTimes[3]->setEnabled(enabled);
        calculateWorkTime();
    });
    connect(m_customDinnerToggle, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        m_scheduleTimes[4]->setEnabled(enabled);
        m_scheduleTimes[5]->setEnabled(enabled);
        calculateWorkTime();
    });
    connect(m_noteEmojiButton, &ElaIconButton::clicked, this, [this] {
        constexpr int kEmojiButtonSize = 34;
        constexpr int kEmojiColumnCount = 4;
        auto* picker = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
        picker->setObjectName(QStringLiteral("noteEmojiPicker"));
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setStyleSheet(QStringLiteral(
            "QFrame#noteEmojiPicker { background: #ffffff; border: 1px solid #d4e2ef; border-radius: 8px; }"));

        auto* pickerLayout = new QGridLayout(picker);
        pickerLayout->setContentsMargins(8, 8, 8, 8);
        pickerLayout->setHorizontalSpacing(4);
        pickerLayout->setVerticalSpacing(4);
        for (int index = 0; index < std::size(kNoteEmojis); ++index) {
            const NoteEmoji& emoji = kNoteEmojis[index];
            auto* emojiButton = new ElaIconButton(emoji.icon, 17, kEmojiButtonSize, kEmojiButtonSize, picker);
            emojiButton->setToolTip(QString::fromUtf8(emoji.description));
            emojiButton->setCursor(Qt::PointingHandCursor);
            emojiButton->setLightHoverColor(QColor(QStringLiteral("#e9f3ff")));
            emojiButton->setLightIconColor(QColor(QStringLiteral("#40566f")));
            emojiButton->setLightHoverIconColor(QColor(QStringLiteral("#1769aa")));
            connect(emojiButton, &ElaIconButton::clicked, this, [this, picker, text = QString::fromUtf8(emoji.text)] {
                m_noteEdit->setFocus();
                m_noteEdit->textCursor().insertText(text);
                picker->close();
            });
            pickerLayout->addWidget(emojiButton, index / kEmojiColumnCount, index % kEmojiColumnCount);
        }

        picker->adjustSize();
        const QRect anchor(m_noteEmojiButton->mapToGlobal(QPoint()), m_noteEmojiButton->size());
        QScreen* screen = QGuiApplication::screenAt(anchor.center());
        if (!screen) screen = ScreenLayout::screenForWidget(this);
        if (screen) {
            picker->winId();
            picker->windowHandle()->setScreen(screen);
            picker->setGeometry(ScreenLayout::popupGeometry(anchor, picker->size(), screen->availableGeometry()));
        }
        picker->show();
    });
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

    m_loadedRecord = record;
    const WorkSchedule custom = record.hasCustomSchedule ? record.customSchedule : m_schedule;
    m_loadedRecord.customSchedule = custom;
    const QTime times[]{custom.workStartTime, custom.workEndTime, custom.lunchBreakStart,
        custom.lunchBreakEnd, custom.dinnerBreakStart, custom.dinnerBreakEnd, custom.mealAllowanceTime};
    for (int i = 0; i < 7; ++i) m_scheduleTimes[i]->setTime(times[i]);
    m_customLunchToggle->setIsToggled(custom.lunchBreakEnabled);
    m_customDinnerToggle->setIsToggled(custom.dinnerBreakEnabled);
    m_customScheduleToggle->setIsToggled(record.hasCustomSchedule);
    m_customScheduleBody->setVisible(record.hasCustomSchedule);
    m_scheduleHint->setText(record.hasCustomSchedule ? QStringLiteral("仅影响当天") : QStringLiteral("使用全局工作制度"));
    m_scheduleTimes[2]->setEnabled(custom.lunchBreakEnabled);
    m_scheduleTimes[3]->setEnabled(custom.lunchBreakEnabled);
    m_scheduleTimes[4]->setEnabled(custom.dinnerBreakEnabled);
    m_scheduleTimes[5]->setEnabled(custom.dinnerBreakEnabled);
    m_excludeStandardOvertime->setIsToggled(record.excludeStandardOvertime);
    m_excludeStandardOvertime->setEnabled(!record.needAverageCal);
    m_needAverageCalCheckBox->setIsToggled(record.needAverageCal);
    m_arrivalTimeEdit->setTime(record.arrivalTime);
    m_departureTimeEdit->setTime(record.departureTime);
    m_noteEdit->setPlainText(record.note);
    calculateWorkTime();
    m_loadingRecord = false;
    updateDialogSize(false);
}
