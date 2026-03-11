#include "AttendanceMainWindow.h"
#include "Utils/CustomCalendarWidget.h"
#include "Utils/TimeSettingDialog.h"
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
#include <QStatusBar>
#include <QAction>
#include <QKeySequence>
#include <algorithm>

namespace {
bool recordsEqual(const AttendanceRecord& lhs, const AttendanceRecord& rhs) {
    return lhs.needAverageCal == rhs.needAverageCal
        && lhs.arrivalTime == rhs.arrivalTime
        && lhs.departureTime == rhs.departureTime
        && lhs.workStartTime == rhs.workStartTime
        && lhs.workEndTime == rhs.workEndTime
        && lhs.lunchBreakStart == rhs.lunchBreakStart
        && lhs.lunchBreakEnd == rhs.lunchBreakEnd
        && lhs.dinnerBreakStart == rhs.dinnerBreakStart
        && lhs.dinnerBreakEnd == rhs.dinnerBreakEnd;
}
}

AttendanceMainWindow::AttendanceMainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("打卡管理系统"));
    setMinimumSize(800, 600);
    resize(900, 660);

    setupUI();
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

    QMainWindow::mousePressEvent(event);
}

void AttendanceMainWindow::onDateDoubleClicked(const QDate& date) {
    const AttendanceRecordState beforeState = captureRecordState(date);
    TimeSettingDialog dialog(date, this);
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
            pushHistoryEntry(QString("编辑日期设置"), QList<AttendanceChange>{ change });
            showStatusMessage(QString("已保存 %1 的设置").arg(date.toString("yyyy-MM-dd")));
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

    showStatusMessage(QString("已导出 %1 条记录到 %2").arg(result.exportedCount).arg(filePath), 5000);
}


void AttendanceMainWindow::setupUI() {
    QWidget* centralWidget = new QWidget();
    setCentralWidget(centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

    // 左侧：日历
    QVBoxLayout* leftLayout = new QVBoxLayout();

    QHBoxLayout* headerLayout = new QHBoxLayout();

    QLabel* titleLabel = new QLabel(QString("考勤日历"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; padding: 10px;");
    headerLayout->addWidget(titleLabel);

    // [导入按钮]
    QPushButton* importBtn = new QPushButton("导入数据");
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; border: none; border-radius: 4px; padding: 6px 12px; }"
        "QPushButton:hover { background-color: #45a049; }"
    );
    connect(importBtn, &QPushButton::clicked, this, &AttendanceMainWindow::onImportJsonClicked);
    headerLayout->addWidget(importBtn);

    // [导出按钮]
    QPushButton* exportBtn = new QPushButton("导出数据");
    exportBtn->setCursor(Qt::PointingHandCursor);
    // 给它一个不同的颜色 (比如蓝色 #2196F3) 以示区分
    exportBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; border: none; border-radius: 4px; padding: 6px 12px; margin-left: 10px; }"
        "QPushButton:hover { background-color: #0b7dda; }"
    );
    // 连接信号
    connect(exportBtn, &QPushButton::clicked, this, &AttendanceMainWindow::onExportJsonClicked);
    headerLayout->addWidget(exportBtn);

    headerLayout->addStretch();

    leftLayout->addLayout(headerLayout);

    // 使用自定义日历控件
    m_calendar = new CustomCalendarWidget();
    m_calendar->setLocale(QLocale::Chinese);
    m_calendar->setFirstDayOfWeek(Qt::Monday);
    m_calendar->setGridVisible(true);
    leftLayout->addWidget(m_calendar);

    // 添加使用说明
    QLabel* helpLabel = new QLabel(QString("使用说明：\n• 单击日期仅选中，双击日期打开编辑弹窗\n• 按住 Ctrl 可多选，按住 Shift 可连续选中日期\n• 选中单个日期后可按 Ctrl+C 复制设置\n• 选中目标日期后可按 Ctrl+V 批量覆盖\n• 按 Ctrl+A 选中当前月份全部有记录日期\n• 按 Ctrl+Z 撤销，按 Ctrl+Y 重做\n• 按 Delete 删除当前选中记录，按 Esc 清空选择"));
    helpLabel->setStyleSheet("color: #666; font-size: 12px; padding: 10px; background-color: #f5f5f5; border-radius: 5px;");
    helpLabel->setWordWrap(true);
    leftLayout->addWidget(helpLabel);

    // 右侧：统计和管理
    QVBoxLayout* rightLayout = new QVBoxLayout();

    QGroupBox* batchGroup = new QGroupBox(QString("批量设置"));
    QVBoxLayout* batchLayout = new QVBoxLayout(batchGroup);

    m_selectionLabel = new QLabel(QString("当前未选中日期"));
    m_selectionLabel->setWordWrap(true);
    m_selectionLabel->setStyleSheet("padding: 8px; background-color: #f9f9f9; border-radius: 5px;");
    batchLayout->addWidget(m_selectionLabel);

    m_copyStatusLabel = new QLabel(QString("未复制任何设置"));
    m_copyStatusLabel->setWordWrap(true);
    m_copyStatusLabel->setStyleSheet("padding: 8px; background-color: #f9f9f9; border-radius: 5px;");
    batchLayout->addWidget(m_copyStatusLabel);

    m_copySelectedButton = new QPushButton(QString("复制选中设置"));
    m_copySelectedButton->setCursor(Qt::PointingHandCursor);
    connect(m_copySelectedButton, &QPushButton::clicked, this, &AttendanceMainWindow::onCopySelectedClicked);
    batchLayout->addWidget(m_copySelectedButton);

    m_applyCopiedButton = new QPushButton(QString("覆盖到选中日期"));
    m_applyCopiedButton->setCursor(Qt::PointingHandCursor);
    connect(m_applyCopiedButton, &QPushButton::clicked, this, &AttendanceMainWindow::onApplyCopiedClicked);
    batchLayout->addWidget(m_applyCopiedButton);

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

    rightLayout->addWidget(batchGroup);

    // 月度统计
    QGroupBox* statsGroup = new QGroupBox(QString("月度统计"));
    QVBoxLayout* statsLayout = new QVBoxLayout(statsGroup);
    m_statsLabel = new QLabel(QString("请选择月份查看统计"));
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("padding: 10px; background-color: #f9f9f9; border-radius: 5px;");
    statsLayout->addWidget(m_statsLabel);
    rightLayout->addWidget(statsGroup);
    rightLayout->addStretch();

    // 使用分割器
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    QWidget* leftWidget = new QWidget();
    leftWidget->setLayout(leftLayout);

    QWidget* rightWidget = new QWidget();
    rightWidget->setLayout(rightLayout);
    rightWidget->setMaximumWidth(350);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    // 连接信号
    connect(m_calendar, &CustomCalendarWidget::dateDoubleClicked, this, &AttendanceMainWindow::onDateDoubleClicked);
    connect(m_calendar, &CustomCalendarWidget::selectionChanged, this, &AttendanceMainWindow::onSelectionChanged);
    connect(m_calendar, &QCalendarWidget::currentPageChanged,
        this, &AttendanceMainWindow::onMonthChanged);
    connect(m_calendar, &CustomCalendarWidget::deleteRequested,
        this, &AttendanceMainWindow::onDeleteRequested);

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
        showStatusMessage(QString("请先单独选中一个日期再复制设置"));
        return;
    }

    const QDate sourceDate = dates.first();
    if (!AttendanceStorage::hasArrivalRecord(sourceDate)) {
        showStatusMessage(QString("%1 还没有已保存的考勤记录")
            .arg(sourceDate.toString("yyyy-MM-dd")));
        return;
    }

    m_copiedRecord = AttendanceStorage::loadRecord(sourceDate);
    m_copiedFromDate = sourceDate;
    m_hasCopiedRecord = true;

    updateBatchActionState();
    showStatusMessage(QString("已复制 %1 的设置").arg(sourceDate.toString("yyyy-MM-dd")));
}

void AttendanceMainWindow::onApplyCopiedClicked() {
    if (!m_hasCopiedRecord) {
        showStatusMessage(QString("请先复制一个日期的设置"));
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
            prompt = QString("确定要用 %1 的设置覆盖 %2 吗？")
                .arg(m_copiedFromDate.toString("yyyy-MM-dd"))
                .arg(existingRecordDates.first().toString("yyyy-MM-dd"));
        }
        else if (emptyDateCount == 0) {
            prompt = QString("确定要用 %1 的设置覆盖选中的 %2 个已有记录日期吗？")
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

    pushHistoryEntry(QString("批量覆盖设置"), changes);

    refreshMonthlyView();
    showStatusMessage(QString("已将 %1 的设置应用到 %2 个日期")
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

void AttendanceMainWindow::showStatusMessage(const QString& message, int timeoutMs) {
    statusBar()->showMessage(message, timeoutMs);
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
    if (dates.isEmpty()) {
        m_selectionLabel->setText(QString("当前未选中日期"));
    }
    else if (dates.size() == 1) {
        const QDate date = dates.first();
        const bool hasRecord = AttendanceStorage::hasArrivalRecord(date);
        m_selectionLabel->setText(QString("当前选中: %1%2")
            .arg(date.toString("yyyy-MM-dd"))
            .arg(hasRecord ? QString() : QString(" (无已保存记录)")));
    }
    else {
        m_selectionLabel->setText(QString("当前选中 %1 个日期（%2 ~ %3），可直接批量覆盖。")
            .arg(dates.size())
            .arg(dates.first().toString("yyyy-MM-dd"))
            .arg(dates.last().toString("yyyy-MM-dd")));
    }

    if (m_hasCopiedRecord) {
        m_copyStatusLabel->setText(QString("已复制: %1 的设置")
            .arg(m_copiedFromDate.toString("yyyy-MM-dd")));
    }
    else {
        m_copyStatusLabel->setText(QString("未复制任何设置"));
    }

    const bool canCopy = dates.size() == 1 && AttendanceStorage::hasArrivalRecord(dates.first());
    bool canApply = false;
    if (m_hasCopiedRecord) {
        for (const QDate& date : dates) {
            if (date != m_copiedFromDate) {
                canApply = true;
                break;
            }
        }
    }
    m_copySelectedButton->setEnabled(canCopy);
    m_applyCopiedButton->setEnabled(canApply);
}

void AttendanceMainWindow::updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot) {
    for (auto it = snapshot.dayViews.constBegin(); it != snapshot.dayViews.constEnd(); ++it) {
        const QDate date = it.key();
        const AttendanceDayView& dayView = it.value();

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
    if (snapshot.workDays > 0) {
        stats += QString("平均加班时间: %1小时")
            .arg(snapshot.totalOvertimeMinutes / (60.0 * snapshot.workDays), 0, 'f', 3);
    }

    m_statsLabel->setText(stats);
}
