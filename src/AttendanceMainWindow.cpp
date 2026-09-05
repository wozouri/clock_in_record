#include "AttendanceMainWindow.h"
#include "Utils/CustomCalendarWidget.h"
#include "Utils/TimeSettingDialog.h"
#include "Utils/WorkScheduleSettingsPage.h"
#include "Data/AttendanceJsonService.h"
#include "Data/AttendanceStatsService.h"
#include "Data/AttendanceStorage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLocale>
#include <QTextCharFormat>
#include <QMessageBox>
#include <QPushButton>
#include <QFileDialog>
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include <ElaIcon.h>
#include <ElaAppBar.h>
#include <ElaMessageBar.h>
#include <ElaNavigationBar.h>
#include <ElaPushButton.h>
#include <ElaTeachingTip.h>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kMinimumWindowWidth = 1120;
constexpr int kMinimumWindowHeight = 720;

bool recordsEqual(const AttendanceRecord& lhs, const AttendanceRecord& rhs) {
    return lhs.needAverageCal == rhs.needAverageCal
        && lhs.arrivalTime == rhs.arrivalTime
        && lhs.departureTime == rhs.departureTime
        && lhs.note == rhs.note;
}

QString recordSummaryHtml(const AttendanceRecord& record) {
    const QString workdayMarker = record.needAverageCal
        ? QStringLiteral("<span style='font-size:16px; font-weight:600; color:#238653;'>&#10003;</span>")
        : QStringLiteral("<span style='font-size:16px; font-weight:600; color:#94a3b8;'>&#9675;</span>");
    return QString(
        "<span style='font-size:13px; font-weight:600; color:#1769aa;'>&#8595; %1</span>"
        "<span style='color:#9ab4ca;'>&nbsp;&nbsp;</span>"
        "<span style='font-size:13px; font-weight:600; color:#6a54a3;'>&#8593; %2</span>"
        "<span style='color:#9ab4ca;'>&nbsp;&nbsp;</span>%3")
        .arg(record.arrivalTime.toString("hh:mm"))
        .arg(record.departureTime.toString("hh:mm"))
        .arg(workdayMarker);
}

QString recordTipSummary(const QDate& date, const AttendanceRecord& record) {
    const QString workdayMarker = record.needAverageCal
        ? QStringLiteral("工作日")
        : QStringLiteral("非工作日");
    return QStringLiteral("%1    %2    %3    %4")
        .arg(date.toString(QStringLiteral("yyyy.M.d")),
             record.arrivalTime.toString(QStringLiteral("hh:mm")),
             record.departureTime.toString(QStringLiteral("hh:mm")),
             workdayMarker);
}

}

AttendanceMainWindow::AttendanceMainWindow(QWidget* parent) : ElaWindow(parent) {
    setWindowTitle(QStringLiteral("工时簿"));
    setMinimumSize(1040, 680);
    resize(1180, 760);

    setAppBarHeight(48);
    setIsNavigationBarEnable(true);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setNavigationBarWidth(260);
    setIsAllowPageOpenInNewWindow(false);
    setIsCentralStackedWidgetTransparent(true);
    setUserInfoCardVisible(false);
    setWindowButtonFlags(ElaAppBarType::RouteBackButtonHint
        | ElaAppBarType::MinimizeButtonHint
        | ElaAppBarType::CloseButtonHint);

    setupUI();

    if (auto* navigationBar = findChild<ElaNavigationBar*>()) {
        navigationBar->setIsTransparent(true);
        for (auto* child : navigationBar->findChildren<QWidget*>()) {
            const QString className = QString::fromLatin1(child->metaObject()->className());
            if (className == QStringLiteral("ElaSuggestBox")
                || className == QStringLiteral("ElaToolButton")) {
                child->hide();
            }
        }
    }

    // ElaWindow completes its internal layout during setupUI; apply the usable size floor afterwards.
    setMinimumSize(kMinimumWindowWidth, kMinimumWindowHeight);
}

void AttendanceMainWindow::mousePressEvent(QMouseEvent* event) {
    // 检查点击位置是否在日历区域外
    if (m_calendar) {
        QPoint calendarPos = m_calendar->mapFromGlobal(event->globalPos());
        QRect calendarRect = m_calendar->rect();

        // 如果点击在日历外，重置选择状态
        if (!calendarRect.contains(calendarPos)) {
            m_calendar->clearSelection();
        }
    }

    ElaWindow::mousePressEvent(event);
}

void AttendanceMainWindow::moveEvent(QMoveEvent* event) {
    ElaWindow::moveEvent(event);

    if (m_contextTipRefreshPending) {
        return;
    }
    m_contextTipRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_contextTipRefreshPending = false;
        refreshContextTipPositions();
    });
}

#ifdef Q_OS_WIN
bool AttendanceMainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    const bool handled = ElaWindow::nativeEvent(eventType, message, result);
    auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage->message == WM_GETMINMAXINFO) {
        auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(nativeMessage->lParam);
        minMaxInfo->ptMinTrackSize.x = kMinimumWindowWidth;
        minMaxInfo->ptMinTrackSize.y = kMinimumWindowHeight;
    }
    return handled;
}
#endif

bool AttendanceMainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_statsLabel) {
        if (event->type() == QEvent::Enter) {
            if (!m_statsContextTip) {
                m_statsContextTip = new ElaTeachingTip(this);
                m_statsContextTip->setTailPosition(ElaTeachingTip::Bottom);
                m_statsContextTip->setTarget(m_statsLabel);
                m_statsContextTip->setIsLightDismiss(false);
                m_statsContextTip->setCloseButtonVisible(false);
            }
            m_statsContextTip->setTitle(QStringLiteral("月度统计"));
            m_statsContextTip->setContent(m_monthlyStatsText);
            m_statsContextTip->showTip();
        } else if (event->type() == QEvent::Leave && m_statsContextTip) {
            m_statsContextTip->closeTip();
        }
    }
    return ElaWindow::eventFilter(watched, event);
}

void AttendanceMainWindow::onDateDoubleClicked(const QDate& date) {
    const AttendanceRecordState beforeState = captureRecordState(date);
    TimeSettingDialog dialog(date, AttendanceStorage::loadWorkSchedule(), this);
    if (dialog.exec() == QDialog::Accepted) {
        AttendanceRecordState afterState;
        afterState.exists = true;
        afterState.record = dialog.getRecord();

        if (!beforeState.exists || !recordsEqual(beforeState.record, afterState.record)) {
            applyRecordState(date, afterState);

            AttendanceChange change;
            change.date = date;
            change.before = beforeState;
            change.after = afterState;
            pushHistoryEntry(QString("编辑考勤记录"), QList<AttendanceChange>{ change });
            showStatusMessage(QString("已保存 %1 的考勤记录").arg(date.toString("yyyy-MM-dd")));
        }

        refreshMonthlyView();
    }
}

void AttendanceMainWindow::onMonthChanged() {
    refreshMonthlyView();
}

void AttendanceMainWindow::onDeleteRequested(const QList<QDate>& dates) {
    deleteAttendanceRecords(dates);
}

void AttendanceMainWindow::onDeleteSelectionRequested() {
    deleteAttendanceRecords(m_calendar->selectedDates());
}

void AttendanceMainWindow::onImportJsonClicked() {
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("导入 Lark-OCR-Sync 数据"),
        "",
        tr("JSON Files (*.json);;All Files (*)"));

    if (!fileName.isEmpty()) {
        processImportFile(fileName);
    }
}


void AttendanceMainWindow::onExportJsonClicked() {
    // 弹出保存文件对话框，默认文件名带上当前日期
    QString defaultName = QString("attendance_backup_%1.json")
        .arg(QDate::currentDate().toString("yyyyMMdd"));

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("导出考勤数据"),
        defaultName,
        tr("JSON Files (*.json);;All Files (*)"));

    if (!fileName.isEmpty()) {
        processExportFile(fileName);
    }
}

void AttendanceMainWindow::onWorkScheduleChanged(const WorkSchedule& schedule)
{
    AttendanceStorage::saveWorkSchedule(schedule);
    refreshMonthlyView();
    showStatusMessage(QStringLiteral("工作制度已更新"));
}


void AttendanceMainWindow::processImportFile(const QString& filePath) {
    const AttendanceImportResult result = AttendanceJsonService::importFromLarkJson(filePath);
    if (!result.success) {
        QMessageBox::warning(this, "导入失败", result.errorMessage);
        return;
    }

    refreshMonthlyView();
    showStatusMessage(QString("已导入 %1 条考勤记录").arg(result.importedCount));
}

void AttendanceMainWindow::processExportFile(const QString& filePath) {
    const AttendanceExportResult result = AttendanceJsonService::exportToJson(filePath);
    if (!result.success) {
        QMessageBox::warning(this, "导出失败", result.errorMessage);
        return;
    }

    if (!result.hasData) {
        showStatusMessage(QString("当前没有任何考勤记录可导出"));
        return;
    }

    showStatusMessage(QString("已导出 %1 条记录到 %2").arg(result.exportedCount).arg(filePath));
}


void AttendanceMainWindow::setupUI() {
    auto* calendarPage = new QWidget();
    calendarPage->setObjectName(QStringLiteral("attendanceCalendarPage"));
    calendarPage->setMinimumSize(840, 600);
    calendarPage->setStyleSheet(QStringLiteral(
        "QWidget#attendanceCalendarPage { background: #f7f9fc; }"));
    auto* mainLayout = new QVBoxLayout(calendarPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* toolbar = new QWidget(calendarPage);
    toolbar->setObjectName(QStringLiteral("attendanceToolbar"));
    toolbar->setFixedHeight(58);
    toolbar->setStyleSheet(QStringLiteral(
        "QWidget#attendanceToolbar { background: #ffffff; border-bottom: 1px solid #dce5ee; }"));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(16, 10, 16, 10);
    toolbarLayout->setSpacing(8);

    const auto createToolbarButton = [toolbar](const QString& text, ElaIconType::IconName icon) {
        auto* button = new ElaPushButton(text, toolbar);
        button->setIcon(ElaIcon::getInstance()->getElaIcon(icon, 15));
        button->setIconSize(QSize(15, 15));
        button->setFixedHeight(34);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #ffffff; color: #223550; border: 1px solid #d8e1eb;"
            " border-radius: 5px; padding: 0 13px; }"
            "QPushButton:hover:enabled { background: #edf4fb; border-color: #b9cee2; }"
            "QPushButton:disabled { color: #9aa5b1; background: #f5f6f8; border-color: #e0e5ea; }"));
        return button;
    };

    auto* importBtn = createToolbarButton(QStringLiteral("导入"), ElaIconType::FileImport);
    connect(importBtn, &ElaPushButton::clicked, this, &AttendanceMainWindow::onImportJsonClicked);

    auto* exportBtn = createToolbarButton(QStringLiteral("导出"), ElaIconType::FileExport);
    connect(exportBtn, &ElaPushButton::clicked, this, &AttendanceMainWindow::onExportJsonClicked);

    auto* copyBtn = createToolbarButton(QStringLiteral("复制"), ElaIconType::Clipboard);
    m_copySelectedButton = copyBtn;
    connect(copyBtn, &ElaPushButton::clicked, this, &AttendanceMainWindow::onCopySelectedClicked);
    toolbarLayout->addWidget(copyBtn);

    auto* applyBtn = createToolbarButton(QStringLiteral("粘贴"), ElaIconType::ClipboardCheck);
    m_applyCopiedButton = applyBtn;
    applyBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #eaf4fd; color: #1769aa; border: 1px solid #9fc8ea;"
        " border-radius: 5px; padding: 0 13px; }"
        "QPushButton:hover:enabled { background: #dceefd; border-color: #75b1df; }"
        "QPushButton:disabled { color: #9aa5b1; background: #f5f6f8; border-color: #e0e5ea; }"));
    connect(applyBtn, &ElaPushButton::clicked, this, &AttendanceMainWindow::onApplyCopiedClicked);
    toolbarLayout->addWidget(applyBtn);

    auto* deleteBtn = createToolbarButton(QStringLiteral("删除"), ElaIconType::TrashCan);
    m_deleteSelectedButton = deleteBtn;
    deleteBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #ffffff; color: #b42318; border: 1px solid #e9b7b1;"
        " border-radius: 5px; padding: 0 13px; }"
        "QPushButton:hover:enabled { background: #fff1f0; border-color: #dc8d84; }"
        "QPushButton:disabled { color: #9aa5b1; background: #f5f6f8; border-color: #e0e5ea; }"));
    connect(deleteBtn, &ElaPushButton::clicked, this, &AttendanceMainWindow::onDeleteSelectionRequested);
    toolbarLayout->addWidget(deleteBtn);

    auto* selectMonthBtn = createToolbarButton(QStringLiteral("全选"), ElaIconType::ListCheck);
    m_selectMonthButton = selectMonthBtn;
    connect(selectMonthBtn, &ElaPushButton::clicked,
        this, &AttendanceMainWindow::onSelectAllCurrentMonthRequested);
    toolbarLayout->addWidget(selectMonthBtn);

    m_statsLabel = new QLabel(toolbar);
    m_statsLabel->setFixedHeight(30);
    m_statsLabel->setCursor(Qt::WhatsThisCursor);
    m_statsLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #36516f; background: #f3f7fb; border: 1px solid #d9e5f0;"
        " border-radius: 5px; padding: 0 12px; font-weight: 600; }"
        "QLabel:hover { background: #eaf3fb; border-color: #b8cfe3; }"));
    m_statsLabel->setText(QStringLiteral("平均加班 --"));
    m_statsLabel->installEventFilter(this);
    toolbarLayout->addWidget(m_statsLabel);

    auto* currentMonthBtn = createToolbarButton(QStringLiteral("回到本月"), ElaIconType::CalendarDay);
    currentMonthBtn->setToolTip(QStringLiteral("切换到当前月份"));
    currentMonthBtn->setIcon(ElaIcon::getInstance()->getElaIcon(
        ElaIconType::CalendarDay, 15, QColor(Qt::white)));
    currentMonthBtn->setLightDefaultColor(QColor(QStringLiteral("#1769aa")));
    currentMonthBtn->setLightHoverColor(QColor(QStringLiteral("#0f5c9b")));
    currentMonthBtn->setLightPressColor(QColor(QStringLiteral("#0b4d82")));
    currentMonthBtn->setLightTextColor(Qt::white);
    currentMonthBtn->setVisible(false);
    m_showCurrentMonthButton = currentMonthBtn;
    connect(currentMonthBtn, &ElaPushButton::clicked,
        this, &AttendanceMainWindow::onShowCurrentMonthRequested);
    toolbarLayout->addWidget(currentMonthBtn);

    toolbarLayout->addStretch();

    auto* separator = new QWidget(toolbar);
    separator->setFixedSize(1, 22);
    separator->setStyleSheet(QStringLiteral("background: #dce5ee;"));
    toolbarLayout->addWidget(separator);
    toolbarLayout->addWidget(importBtn);
    toolbarLayout->addWidget(exportBtn);
    mainLayout->addWidget(toolbar);

    auto* workspace = new QWidget(calendarPage);
    auto* workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    mainLayout->addWidget(workspace);

    // 左侧：日历
    QVBoxLayout* leftLayout = new QVBoxLayout();

    // 使用自定义日历控件
    m_calendar = new CustomCalendarWidget();
    m_calendar->setLocale(QLocale::Chinese);
    m_calendar->setFirstDayOfWeek(Qt::Monday);
    m_calendar->setGridVisible(true);
    leftLayout->addWidget(m_calendar);
    leftLayout->setContentsMargins(32, 22, 32, 30);
    leftLayout->setSpacing(0);

    QAction* copyAction = new QAction(this);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyAction, &QAction::triggered, this, &AttendanceMainWindow::onCopySelectedClicked);
    addAction(copyAction);

    QAction* pasteAction = new QAction(this);
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteAction, &QAction::triggered, this, &AttendanceMainWindow::onApplyCopiedClicked);
    addAction(pasteAction);

    QAction* selectAllAction = new QAction(this);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllAction, &QAction::triggered, this, &AttendanceMainWindow::onSelectAllCurrentMonthRequested);
    addAction(selectAllAction);

    m_undoAction = new QAction(this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (m_undoStack.isEmpty()) {
            showStatusMessage(QString("当前没有可撤销的操作"));
            return;
        }

        const AttendanceHistoryEntry entry = m_undoStack.takeLast();
        if (applyHistoryEntry(entry, false)) {
            m_redoStack.append(entry);
            showStatusMessage(QString("已撤销%1").arg(entry.actionText));
        }
        updateUndoRedoActionState();
    });
    addAction(m_undoAction);

    m_redoAction = new QAction(this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (m_redoStack.isEmpty()) {
            showStatusMessage(QString("当前没有可重做的操作"));
            return;
        }

        const AttendanceHistoryEntry entry = m_redoStack.takeLast();
        if (applyHistoryEntry(entry, true)) {
            m_undoStack.append(entry);
            showStatusMessage(QString("已重做%1").arg(entry.actionText));
        }
        updateUndoRedoActionState();
    });
    addAction(m_redoAction);

    QAction* deleteAction = new QAction(this);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteAction, &QAction::triggered, this, &AttendanceMainWindow::onDeleteSelectionRequested);
    addAction(deleteAction);

    QAction* clearSelectionAction = new QAction(this);
    clearSelectionAction->setShortcut(QKeySequence(Qt::Key_Escape));
    clearSelectionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(clearSelectionAction, &QAction::triggered, this, [this]() {
        m_calendar->clearSelection();
        showStatusMessage(QString("已清空当前选择"));
    });
    addAction(clearSelectionAction);

    QWidget* leftWidget = new QWidget();
    leftWidget->setLayout(leftLayout);
    leftWidget->setMinimumWidth(680);
    leftWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    workspaceLayout->addWidget(leftWidget);

    m_workScheduleSettingsPage = new WorkScheduleSettingsPage();
    m_workScheduleSettingsPage->setWorkSchedule(AttendanceStorage::loadWorkSchedule());
    connect(m_workScheduleSettingsPage, &WorkScheduleSettingsPage::workScheduleSaved,
        this, &AttendanceMainWindow::onWorkScheduleChanged);

    QString attendanceNavigationKey;
    addExpanderNode(QStringLiteral("考勤管理"), attendanceNavigationKey, ElaIconType::Calendar);
    addPageNode(QStringLiteral("日历记录"), calendarPage, attendanceNavigationKey,
        ElaIconType::CalendarDays);
    expandNavigationNode(attendanceNavigationKey);
    QString settingsPageKey;
    addFooterNode(QStringLiteral("设置"), m_workScheduleSettingsPage, settingsPageKey, 0,
        ElaIconType::Gear);
    navigation(calendarPage->property("ElaPageKey").toString());

    // 连接信号
    connect(m_calendar, &CustomCalendarWidget::dateDoubleClicked, this, &AttendanceMainWindow::onDateDoubleClicked);
    connect(m_calendar, &CustomCalendarWidget::selectionChanged, this, &AttendanceMainWindow::onSelectionChanged);
    connect(m_calendar, &QCalendarWidget::currentPageChanged,
        this, &AttendanceMainWindow::onMonthChanged);
    connect(m_calendar, &CustomCalendarWidget::deleteRequested,
        this, &AttendanceMainWindow::onDeleteRequested);
    connect(m_calendar, &CustomCalendarWidget::copyRequested,
        this, &AttendanceMainWindow::onCopyRequested);

    refreshMonthlyView();
    updateBatchActionState();
    updateUndoRedoActionState();
}

void AttendanceMainWindow::deleteAttendanceRecord(const QDate& date) {
    AttendanceStorage::deleteRecord(date);
    m_calendar->clearCustomData(date);
}

void AttendanceMainWindow::deleteAttendanceRecords(const QList<QDate>& dates) {
    QList<QDate> deletableDates;
    QList<AttendanceChange> changes;
    for (const QDate& date : dates) {
        if (AttendanceStorage::hasArrivalRecord(date) && !deletableDates.contains(date)) {
            deletableDates.append(date);

            AttendanceChange change;
            change.date = date;
            change.before = captureRecordState(date);
            change.after.exists = false;
            changes.append(change);
        }
    }

    std::sort(deletableDates.begin(), deletableDates.end());

    if (deletableDates.isEmpty()) {
        showStatusMessage(QString("当前没有可删除的考勤记录"));
        return;
    }

    if (deletableDates.size() > 1) {
        if (QMessageBox::question(this,
            QString("确认批量删除"),
            QString("确定要删除选中的 %1 条考勤记录吗？").arg(deletableDates.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    for (const QDate& date : deletableDates) {
        deleteAttendanceRecord(date);
    }

    pushHistoryEntry(QString("删除记录"), changes);

    m_calendar->clearSelection();

    refreshMonthlyView();
    if (deletableDates.size() == 1) {
        showStatusMessage(QString("已删除 %1 的考勤记录").arg(deletableDates.first().toString("yyyy-MM-dd")));
    }
    else {
        showStatusMessage(QString("已删除 %1 个日期的考勤记录").arg(deletableDates.size()));
    }
}

void AttendanceMainWindow::refreshMonthlyView() {
    const MonthlyAttendanceSnapshot snapshot =
        AttendanceStatsService::buildMonthlySnapshot(m_calendar->yearShown(), m_calendar->monthShown());

    updateCalendarAppearance(snapshot);
    updateMonthlyStatistics(snapshot);
    updateBatchActionState();
}

void AttendanceMainWindow::onSelectionChanged() {
    updateBatchActionState();
}

void AttendanceMainWindow::onCopySelectedClicked() {
    const QList<QDate> dates = m_calendar->selectedDates();
    if (dates.size() != 1) {
        showStatusMessage(QString("请先单独选中一个日期再复制记录"));
        return;
    }

    const QDate sourceDate = dates.first();
    if (!AttendanceStorage::hasArrivalRecord(sourceDate)) {
        showStatusMessage(QString("%1 还没有已保存的考勤记录")
            .arg(sourceDate.toString("yyyy-MM-dd")));
        return;
    }

    copyRecord(sourceDate);
}

void AttendanceMainWindow::onCopyRequested(const QDate& date)
{
    copyRecord(date);
}

void AttendanceMainWindow::copyRecord(const QDate& sourceDate)
{
    if (!AttendanceStorage::hasArrivalRecord(sourceDate)) {
        return;
    }

    m_copiedRecord = AttendanceStorage::loadRecord(sourceDate);
    m_copiedFromDate = sourceDate;
    m_hasCopiedRecord = true;

    updateBatchActionState();
    showStatusMessage(QStringLiteral("已复制 %1 的记录").arg(sourceDate.toString(QStringLiteral("yyyy-MM-dd"))));
}

void AttendanceMainWindow::onApplyCopiedClicked() {
    if (!m_hasCopiedRecord) {
        showStatusMessage(QString("请先复制一个日期的记录"));
        return;
    }

    QList<QDate> targetDates;
    QList<QDate> existingRecordDates;
    QList<AttendanceChange> changes;
    const QList<QDate> selectedDates = m_calendar->selectedDates();
    for (const QDate& date : selectedDates) {
        if (date != m_copiedFromDate) {
            const AttendanceRecordState beforeState = captureRecordState(date);
            if (beforeState.exists && recordsEqual(beforeState.record, m_copiedRecord)) {
                continue;
            }

            targetDates.append(date);
            if (beforeState.exists) {
                existingRecordDates.append(date);
            }

            AttendanceChange change;
            change.date = date;
            change.before = beforeState;
            change.after.exists = true;
            change.after.record = m_copiedRecord;
            changes.append(change);
        }
    }

    if (targetDates.isEmpty()) {
        showStatusMessage(QString("请重新选择至少一个目标日期"));
        return;
    }

    if (!existingRecordDates.isEmpty()) {
        const int emptyDateCount = targetDates.size() - existingRecordDates.size();
        QString prompt;

        if (existingRecordDates.size() == 1 && emptyDateCount == 0) {
            prompt = QString("确定要用 %1 的记录覆盖 %2 吗？")
                .arg(m_copiedFromDate.toString("yyyy-MM-dd"))
                .arg(existingRecordDates.first().toString("yyyy-MM-dd"));
        }
        else if (emptyDateCount == 0) {
            prompt = QString("确定要用 %1 的记录覆盖选中的 %2 个已有记录日期吗？")
                .arg(m_copiedFromDate.toString("yyyy-MM-dd"))
                .arg(existingRecordDates.size());
        }
        else {
            prompt = QString("选中的 %1 个日期里，有 %2 个已有记录会被覆盖，另外 %3 个空白日期将直接写入。确定继续吗？")
                .arg(targetDates.size())
                .arg(existingRecordDates.size())
                .arg(emptyDateCount);
        }

        if (QMessageBox::question(this,
            QString("确认批量覆盖"),
            prompt,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    for (const QDate& date : targetDates) {
        AttendanceStorage::saveRecord(date, m_copiedRecord);
    }

    pushHistoryEntry(QString("批量覆盖记录"), changes);

    refreshMonthlyView();
    showStatusMessage(QString("已将 %1 的记录应用到 %2 个日期")
        .arg(m_copiedFromDate.toString("yyyy-MM-dd"))
        .arg(targetDates.size()));
}

void AttendanceMainWindow::onSelectAllCurrentMonthRequested() {
    QList<QDate> monthDates;
    const QStringList recordedDateKeys = AttendanceStorage::recordedDates();
    for (const QString& dateKey : recordedDateKeys) {
        const QDate date = QDate::fromString(dateKey, "yyyy-MM-dd");
        if (date.isValid()
            && date.year() == m_calendar->yearShown()
            && date.month() == m_calendar->monthShown()) {
            monthDates.append(date);
        }
    }

    if (monthDates.isEmpty()) {
        showStatusMessage(QString("当前月份没有可选中的已记录日期"));
        return;
    }

    m_calendar->setSelectedDates(monthDates);
    showStatusMessage(QString("已选中当前月份的 %1 个记录日期").arg(monthDates.size()));
}

void AttendanceMainWindow::onShowCurrentMonthRequested()
{
    const QDate today = QDate::currentDate();
    m_calendar->setCurrentPage(today.year(), today.month());
}

void AttendanceMainWindow::showStatusMessage(const QString& message, int timeoutMs) {
    ElaMessageBar::information(ElaMessageBarType::Top, QStringLiteral("考勤"),
        message, timeoutMs, this, 24);
}

AttendanceMainWindow::AttendanceRecordState AttendanceMainWindow::captureRecordState(const QDate& date) const {
    AttendanceRecordState state;
    state.exists = AttendanceStorage::hasArrivalRecord(date);
    if (state.exists) {
        state.record = AttendanceStorage::loadRecord(date);
    }
    return state;
}

void AttendanceMainWindow::applyRecordState(const QDate& date, const AttendanceRecordState& state) {
    if (state.exists) {
        AttendanceStorage::saveRecord(date, state.record);
    }
    else {
        AttendanceStorage::deleteRecord(date);
        m_calendar->clearCustomData(date);
    }
}

void AttendanceMainWindow::pushHistoryEntry(const QString& actionText, const QList<AttendanceChange>& changes) {
    if (changes.isEmpty()) {
        return;
    }

    AttendanceHistoryEntry entry;
    entry.actionText = actionText;
    entry.changes = changes;
    m_undoStack.append(entry);
    m_redoStack.clear();
    updateUndoRedoActionState();
}

bool AttendanceMainWindow::applyHistoryEntry(const AttendanceHistoryEntry& entry, bool useAfterState) {
    if (entry.changes.isEmpty()) {
        return false;
    }

    for (const AttendanceChange& change : entry.changes) {
        applyRecordState(change.date, useAfterState ? change.after : change.before);
    }

    m_calendar->clearSelection();
    refreshMonthlyView();
    return true;
}

void AttendanceMainWindow::updateUndoRedoActionState() {
    if (m_undoAction) {
        m_undoAction->setEnabled(!m_undoStack.isEmpty());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(!m_redoStack.isEmpty());
    }
}

void AttendanceMainWindow::updateBatchActionState() {
    const QList<QDate> dates = m_calendar->selectedDates();
    const bool canCopy = dates.size() == 1 && AttendanceStorage::hasArrivalRecord(dates.first());
    bool canApply = false;
    int targetCount = 0;
    int deletableCount = 0;
    int monthRecordCount = 0;
    for (const QDate& date : dates) {
        if (AttendanceStorage::hasArrivalRecord(date)) {
            ++deletableCount;
        }
    }
    if (m_hasCopiedRecord) {
        for (const QDate& date : dates) {
            if (date != m_copiedFromDate) {
                canApply = true;
                ++targetCount;
            }
        }
    }

    for (const QString& dateKey : AttendanceStorage::recordedDates()) {
        const QDate date = QDate::fromString(dateKey, QStringLiteral("yyyy-MM-dd"));
        if (date.isValid() && date.year() == m_calendar->yearShown()
            && date.month() == m_calendar->monthShown()) {
            ++monthRecordCount;
        }
    }

    m_copySelectedButton->setEnabled(canCopy);
    m_applyCopiedButton->setEnabled(canApply);
    m_deleteSelectedButton->setEnabled(deletableCount > 0);
    m_selectMonthButton->setEnabled(monthRecordCount > 0);
    const QDate today = QDate::currentDate();
    m_showCurrentMonthButton->setVisible(m_calendar->yearShown() != today.year()
        || m_calendar->monthShown() != today.month());

    if (canCopy) {
        m_copySelectedButton->setToolTip(
            QStringLiteral("复制 %1 的记录").arg(dates.first().toString(QStringLiteral("yyyy年M月d日"))));
    }
    else {
        m_copySelectedButton->setToolTip(QStringLiteral("单选一条已有记录后可复制"));
    }

    if (canApply) {
        m_applyCopiedButton->setToolTip(
            QStringLiteral("粘贴到 %1 个已选日期").arg(targetCount));
    }
    else if (m_hasCopiedRecord) {
        m_applyCopiedButton->setToolTip(QStringLiteral("选择至少一个非来源日期后可粘贴"));
    }
    else {
        m_applyCopiedButton->setToolTip(QStringLiteral("请先复制一条记录"));
    }

    if (deletableCount > 0) {
        m_deleteSelectedButton->setToolTip(
            QStringLiteral("删除当前选择中的 %1 条记录").arg(deletableCount));
    }
    else {
        m_deleteSelectedButton->setToolTip(QStringLiteral("选择已有记录后可删除"));
    }

    m_selectMonthButton->setToolTip(monthRecordCount > 0
        ? QStringLiteral("选中本月的 %1 条记录").arg(monthRecordCount)
        : QStringLiteral("本月没有记录"));

    updateContextTips(dates);
}

void AttendanceMainWindow::updateContextTips(const QList<QDate>& dates) {
    const bool hasSingleRecord = dates.size() == 1
        && AttendanceStorage::hasArrivalRecord(dates.first());

    // 相邻工具按钮无法同时承载两块持久提示，复制来源优先展示。
    if (hasSingleRecord && !m_hasCopiedRecord) {
        const QDate date = dates.first();
        const AttendanceRecord record = AttendanceStorage::loadRecord(date);
        if (!m_copyContextTip) {
            m_copyContextTip = new ElaTeachingTip(this);
            m_copyContextTip->setTailPosition(ElaTeachingTip::Bottom);
            m_copyContextTip->setTarget(m_copySelectedButton);
            m_copyContextTip->setIsLightDismiss(false);
            m_copyContextTip->setCloseButtonVisible(false);
            m_copyContextTip->setStyleSheet(QStringLiteral(
                "ElaTeachingTip QLabel { color: #1f2937; }"));
        }
        m_copyContextTip->setTitle(recordTipSummary(date, record));
        m_copyContextTip->setContent(QString());
        m_copyContextTip->showTip();
    } else if (m_copyContextTip) {
        m_copyContextTip->closeTip();
    }

    if (m_hasCopiedRecord) {
        if (!m_applyContextTip) {
            m_applyContextTip = new ElaTeachingTip(this);
            m_applyContextTip->setTailPosition(ElaTeachingTip::Bottom);
            m_applyContextTip->setTarget(m_applyCopiedButton);
            m_applyContextTip->setIsLightDismiss(false);
            m_applyContextTip->setCloseButtonVisible(false);
            m_applyContextTip->setStyleSheet(QStringLiteral(
                "ElaTeachingTip QLabel { color: #1f2937; }"));
        }
        m_applyContextTip->setTitle(recordTipSummary(m_copiedFromDate, m_copiedRecord));
        m_applyContextTip->setContent(QString());
        m_applyContextTip->showTip();
    } else if (m_applyContextTip) {
        m_applyContextTip->closeTip();
    }
}

void AttendanceMainWindow::refreshContextTipPositions() {
    const auto refreshTip = [](ElaTeachingTip* tip) {
        if (!tip || !tip->isVisible()) {
            return;
        }
        tip->closeTip();
        tip->showTip();
    };
    refreshTip(m_copyContextTip);
    refreshTip(m_applyContextTip);
    refreshTip(m_statsContextTip);
}

void AttendanceMainWindow::updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot) {
    // Date formats and custom cell text are cached by absolute date. Clear the
    // previously rendered month first so spill-over dates do not retain stale styling.
    for (const QDate& date : m_renderedCalendarDates) {
        m_calendar->setDateTextFormat(date, QTextCharFormat());
        m_calendar->clearCustomData(date);
    }
    m_renderedCalendarDates.clear();

    for (auto it = snapshot.dayViews.constBegin(); it != snapshot.dayViews.constEnd(); ++it) {
        const QDate date = it.key();
        const AttendanceDayView& dayView = it.value();
        m_renderedCalendarDates.append(date);

        if (dayView.hasRecord) {
            // 有打卡记录，显示绿色背景
            QTextCharFormat format;
            QColor defaultCol(144, 238, 144); // 浅绿色
            if (!dayView.needAverageCal) {
                defaultCol = QColor("#acfdea");
            }
            format.setBackground(defaultCol);
            
            m_calendar->setDateTextFormat(date, format);
        }
        else {
            // 清除格式
            m_calendar->setDateTextFormat(date, QTextCharFormat());
            m_calendar->clearCustomData(date);
        }
    }
}

void AttendanceMainWindow::updateMonthlyStatistics(const MonthlyAttendanceSnapshot& snapshot) {
    for (auto it = snapshot.dayViews.constBegin(); it != snapshot.dayViews.constEnd(); ++it) {
        const QDate date = it.key();
        const AttendanceDayView& dayView = it.value();

        if (dayView.hasRecord) {
            QVariantMap info;
            info["arrivalTime"] = dayView.arrivalText;
            info["departureTime"] = dayView.departureText;
            info["hasNote"] = dayView.hasNote;
            m_calendar->setCustomData(date, info);
        }
    }

    QString stats = QString("统计月份: %1年%2月\n")
        .arg(snapshot.year)
        .arg(snapshot.month);
    stats += QString("工作天数: %1天\n").arg(snapshot.workDays);
    stats += QString("总加班时间: %1小时%2分钟\n")
        .arg(snapshot.totalOvertimeMinutes / 60)
        .arg(snapshot.totalOvertimeMinutes % 60);
    stats += QString("总迟到时间: %1小时%2分钟\n")
        .arg(snapshot.totalLateMinutes / 60)
        .arg(snapshot.totalLateMinutes % 60);
    stats += QString("总早退时间: %1小时%2分钟\n")
        .arg(snapshot.totalEarlyLeaveMinutes / 60)
        .arg(snapshot.totalEarlyLeaveMinutes % 60);
    const QString averageOvertime = snapshot.workDays > 0
        ? QString::number(snapshot.totalOvertimeMinutes / (60.0 * snapshot.workDays), 'f', 3)
        : QStringLiteral("--");
    if (snapshot.workDays > 0) {
        stats += QString("平均加班时间: %1小时").arg(averageOvertime);
    }

    m_monthlyStatsText = stats;
    m_statsLabel->setText(QString("平均加班 %1 小时").arg(averageOvertime));
}
