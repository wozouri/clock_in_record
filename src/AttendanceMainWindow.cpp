#include "AttendanceMainWindow.h"
#include "Utils/CustomCalendarWidget.h"
#include "Utils/TimeSettingDialog.h"
#include "Utils/WorkScheduleDialog.h"
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
        && lhs.departureTime == rhs.departureTime;
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
}

AttendanceMainWindow::AttendanceMainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("打卡管理系统"));
    setMinimumSize(1040, 680);
    resize(1180, 760);

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

void AttendanceMainWindow::onWorkScheduleSettingsClicked()
{
    WorkScheduleDialog dialog(AttendanceStorage::loadWorkSchedule(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    AttendanceStorage::saveWorkSchedule(dialog.workSchedule());
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

    showStatusMessage(QString("已导出 %1 条记录到 %2").arg(result.exportedCount).arg(filePath), 5000);
}


void AttendanceMainWindow::setupUI() {
    QWidget* centralWidget = new QWidget();
    setCentralWidget(centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(16, 14, 16, 16);
    mainLayout->setSpacing(0);

    // 左侧：日历
    QVBoxLayout* leftLayout = new QVBoxLayout();

    QHBoxLayout* headerLayout = new QHBoxLayout();

    QLabel* titleLabel = new QLabel(QString("考勤日历"));
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; padding: 2px 0;");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    const QString headerButtonStyle =
        "QPushButton { border: none; border-radius: 4px; padding: 0 13px; min-height: 32px; }"
        "QPushButton:hover { background-color: #e8eef4; }";

    // [导入按钮]
    QPushButton* importBtn = new QPushButton("导入数据");
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; border: none; border-radius: 4px; padding: 0 13px; min-height: 32px; }"
        "QPushButton:hover { background-color: #45a049; }"
    );
    connect(importBtn, &QPushButton::clicked, this, &AttendanceMainWindow::onImportJsonClicked);
    headerLayout->addWidget(importBtn);

    // [导出按钮]
    QPushButton* exportBtn = new QPushButton("导出数据");
    exportBtn->setCursor(Qt::PointingHandCursor);
    exportBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; border: none; border-radius: 4px; padding: 0 13px; min-height: 32px; }"
        "QPushButton:hover { background-color: #0b7dda; }"
    );
    connect(exportBtn, &QPushButton::clicked, this, &AttendanceMainWindow::onExportJsonClicked);
    headerLayout->addWidget(exportBtn);

    QPushButton* scheduleBtn = new QPushButton(QStringLiteral("工作制度"));
    scheduleBtn->setCursor(Qt::PointingHandCursor);
    scheduleBtn->setToolTip(QStringLiteral("设置标准上下班与休息时间"));
    scheduleBtn->setStyleSheet(headerButtonStyle);
    connect(scheduleBtn, &QPushButton::clicked,
        this, &AttendanceMainWindow::onWorkScheduleSettingsClicked);
    headerLayout->addWidget(scheduleBtn);
    headerLayout->setSpacing(8);

    leftLayout->addLayout(headerLayout);

    // 使用自定义日历控件
    m_calendar = new CustomCalendarWidget();
    m_calendar->setLocale(QLocale::Chinese);
    m_calendar->setFirstDayOfWeek(Qt::Monday);
    m_calendar->setGridVisible(true);
    leftLayout->addWidget(m_calendar);
    leftLayout->setSpacing(10);

    // 右侧：统计和管理
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(12);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* batchGroup = new QGroupBox(QString("日历操作"));
    QVBoxLayout* batchLayout = new QVBoxLayout(batchGroup);
    batchLayout->setContentsMargins(12, 18, 12, 12);
    batchLayout->setSpacing(8);

    m_selectionLabel = new QLabel();
    m_selectionLabel->setWordWrap(true);
    m_selectionLabel->setTextFormat(Qt::RichText);

    m_copyStatusLabel = new QLabel();
    m_copyStatusLabel->setWordWrap(true);
    m_copyStatusLabel->setTextFormat(Qt::RichText);
    batchLayout->addWidget(m_copyStatusLabel);
    batchLayout->addWidget(m_selectionLabel);

    m_copySelectedButton = new QPushButton();
    m_copySelectedButton->setCursor(Qt::PointingHandCursor);
    m_copySelectedButton->setMinimumHeight(34);
    m_copySelectedButton->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #1769aa; border: 1px solid #8bbce5; border-radius: 4px; padding: 0 10px; }"
        "QPushButton:hover:enabled { background-color: #eaf4fd; }"
        "QPushButton:disabled { color: #8b98a5; border-color: #d6dde3; background-color: #f4f6f8; }"
    );
    connect(m_copySelectedButton, &QPushButton::clicked, this, &AttendanceMainWindow::onCopySelectedClicked);
    batchLayout->addWidget(m_copySelectedButton);

    m_applyCopiedButton = new QPushButton();
    m_applyCopiedButton->setCursor(Qt::PointingHandCursor);
    m_applyCopiedButton->setMinimumHeight(34);
    m_applyCopiedButton->setStyleSheet(
        "QPushButton { background-color: #1976d2; color: white; border: none; border-radius: 4px; padding: 0 10px; }"
        "QPushButton:hover:enabled { background-color: #1565c0; }"
        "QPushButton:disabled { color: #8b98a5; background-color: #e2e7eb; }"
    );
    connect(m_applyCopiedButton, &QPushButton::clicked, this, &AttendanceMainWindow::onApplyCopiedClicked);
    batchLayout->addWidget(m_applyCopiedButton);

    m_deleteSelectedButton = new QPushButton();
    m_deleteSelectedButton->setCursor(Qt::PointingHandCursor);
    m_deleteSelectedButton->setMinimumHeight(34);
    m_deleteSelectedButton->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #c53030; border: 1px solid #e3a2a2; border-radius: 4px; padding: 0 10px; }"
        "QPushButton:hover:enabled { background-color: #fff1f1; }"
        "QPushButton:disabled { color: #9aa5b1; border-color: #d6dde3; background-color: #f4f6f8; }"
    );
    connect(m_deleteSelectedButton, &QPushButton::clicked,
        this, &AttendanceMainWindow::onDeleteSelectionRequested);
    batchLayout->addWidget(m_deleteSelectedButton);

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
    statsLayout->setContentsMargins(12, 18, 12, 12);
    m_statsLabel = new QLabel(QString("请选择月份查看统计"));
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("padding: 4px 2px; color: #374151; line-height: 1.5;");
    statsLayout->addWidget(m_statsLabel);
    rightLayout->addWidget(statsGroup);
    rightLayout->addStretch();

    // 使用分割器
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    QWidget* leftWidget = new QWidget();
    leftWidget->setLayout(leftLayout);
    leftWidget->setMinimumWidth(680);

    QWidget* rightWidget = new QWidget();
    rightWidget->setLayout(rightLayout);
    rightWidget->setMinimumWidth(280);
    rightWidget->setMaximumWidth(340);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes(QList<int>{ 780, 300 });

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
        showStatusMessage(QString("请先单独选中一个日期再复制记录"));
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
    showStatusMessage(QString("已复制 %1 的记录").arg(sourceDate.toString("yyyy-MM-dd")));
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
        m_selectionLabel->setVisible(true);
        m_selectionLabel->setText(
            QString("<span style='font-size:15px; font-weight:600; color:#64748b;'>未选择日期</span>"));
        m_selectionLabel->setStyleSheet(
            "padding: 9px 10px; background-color: #f6f8fa; border: 1px solid #e2e8f0; border-radius: 4px;");
        m_selectionLabel->setToolTip(QString());
    }
    else if (dates.size() == 1) {
        m_selectionLabel->setVisible(true);
        const QDate date = dates.first();
        const bool hasRecord = AttendanceStorage::hasArrivalRecord(date);
        QString recordSummary;
        if (hasRecord) {
            const AttendanceRecord record = AttendanceStorage::loadRecord(date);
            recordSummary = QString("<br>%1").arg(recordSummaryHtml(record));
            m_selectionLabel->setToolTip(QStringLiteral("向下箭头：到岗时间；向上箭头：离岗时间；勾选：计入工作日统计；空心圆：不计入工作日统计"));
            m_selectionLabel->setStyleSheet(
                "padding: 9px 10px; background-color: #edf6ff; border: 1px solid #b8d9f4; border-radius: 4px;");
        }
        else {
            m_selectionLabel->setToolTip(QString());
            m_selectionLabel->setStyleSheet(
                "padding: 9px 10px; background-color: #f1f3f5; border: 1px solid #d6dce2; border-radius: 4px;");
        }
        m_selectionLabel->setText(QString(
            "<span style='font-size:15px; font-weight:600; color:%1;'>%2</span>%3")
            .arg(hasRecord ? QStringLiteral("#1e3a5f") : QStringLiteral("#64748b"))
            .arg(date.toString("yyyy年M月d日"))
            .arg(recordSummary));
    }
    else {
        m_selectionLabel->setVisible(true);
        m_selectionLabel->setText(QString(
            "<span style='font-size:15px; font-weight:600; color:#1e3a5f;'>已选择 %1 个日期</span>")
            .arg(dates.size()));
        m_selectionLabel->setStyleSheet(
            "padding: 9px 10px; background-color: #edf6ff; border: 1px solid #b8d9f4; border-radius: 4px;");
        m_selectionLabel->setToolTip(QString());
    }

    if (m_hasCopiedRecord) {
        m_copyStatusLabel->setText(QString(
            "<span style='font-size:15px; font-weight:600; color:#1f5c40;'>%1</span><br>%2")
            .arg(m_copiedFromDate.toString("yyyy年M月d日"))
            .arg(recordSummaryHtml(m_copiedRecord)));
        m_copyStatusLabel->setStyleSheet(
            "padding: 9px 10px; background-color: #eefaf3; border: 1px solid #b9e2c9; border-radius: 4px;");
        m_copyStatusLabel->setToolTip(QStringLiteral("已复制的记录：向下箭头为到岗时间，向上箭头为离岗时间，勾选表示计入工作日统计"));
        m_copyStatusLabel->setVisible(true);
    }
    else {
        m_copyStatusLabel->setText(
            QString("<span style='font-size:15px; font-weight:600; color:#64748b;'>未复制记录</span>"));
        m_copyStatusLabel->setStyleSheet(
            "padding: 9px 10px; background-color: #f6f8fa; border: 1px solid #e2e8f0; border-radius: 4px;");
        m_copyStatusLabel->setVisible(true);
        m_copyStatusLabel->setToolTip(QString());
    }

    const bool canCopy = dates.size() == 1 && AttendanceStorage::hasArrivalRecord(dates.first());
    bool canApply = false;
    int targetCount = 0;
    int deletableCount = 0;
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
    m_copySelectedButton->setEnabled(canCopy);
    m_applyCopiedButton->setEnabled(canApply);
    m_deleteSelectedButton->setEnabled(deletableCount > 0);

    if (canCopy) {
        m_copySelectedButton->setText(QString("复制 %1 的记录").arg(dates.first().toString("M月d日")));
        m_copySelectedButton->setToolTip(QString("将 %1 的记录设为来源").arg(dates.first().toString("yyyy年M月d日")));
    }
    else {
        m_copySelectedButton->setText(QString("复制记录"));
        m_copySelectedButton->setToolTip(QString());
    }

    if (canApply) {
        m_applyCopiedButton->setText(QString("应用至 %1 个已选日期").arg(targetCount));
        m_applyCopiedButton->setToolTip(QString("用已复制记录覆盖目标日期"));
    }
    else if (m_hasCopiedRecord) {
        m_applyCopiedButton->setText(QString("应用记录"));
        m_applyCopiedButton->setToolTip(QString());
    }
    else {
        m_applyCopiedButton->setText(QString("应用记录"));
        m_applyCopiedButton->setToolTip(QString());
    }

    if (deletableCount > 0) {
        m_deleteSelectedButton->setText(QString("删除 %1 条记录").arg(deletableCount));
        m_deleteSelectedButton->setToolTip(QString("删除当前选择中的已有记录"));
    }
    else {
        m_deleteSelectedButton->setText(QString("删除记录"));
        m_deleteSelectedButton->setToolTip(QString());
    }
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
