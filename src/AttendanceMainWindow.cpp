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
            // 将选择重置为看不见的日期，并将页面调回当前页面，这样看起来像失去焦点
            m_calendar->setSelectedDate(QDate::currentDate().addDays(365));
            m_calendar->setCurrentPage(QDate::currentDate().year(), QDate::currentDate().month());
        }
    }

    QMainWindow::mousePressEvent(event);
}

void AttendanceMainWindow::onDateClicked(const QDate& date) {
    TimeSettingDialog dialog(date, this);
    if (dialog.exec() == QDialog::Accepted) {
        refreshMonthlyView();
    }
}

void AttendanceMainWindow::onMonthChanged() {
    refreshMonthlyView();
}

void AttendanceMainWindow::onDeleteRequested(const QDate& date) {
    // 确认删除
    int ret = QMessageBox::question(this,
        QString("确认删除"),
        QString("确定要删除 %1 的考勤记录吗？").arg(date.toString("yyyy-MM-dd")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        deleteAttendanceRecord(date);
    }
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

    QMessageBox::information(this, "导入成功", QString("已成功导入 %1 条考勤记录！").arg(result.importedCount));
}

void AttendanceMainWindow::processExportFile(const QString& filePath) {
    const AttendanceExportResult result = AttendanceJsonService::exportToJson(filePath);
    if (!result.success) {
        QMessageBox::warning(this, "导出失败", result.errorMessage);
        return;
    }

    if (!result.hasData) {
        QMessageBox::information(this, "提示", "当前没有任何考勤记录可导出。");
        return;
    }

    QMessageBox::information(this, "导出成功",
        QString("已成功导出 %1 条记录到:\n%2").arg(result.exportedCount).arg(filePath));
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
    QLabel* helpLabel = new QLabel(QString("使用说明：\n• 左键点击日期设置考勤时间\n• 右键点击有记录的日期可删除记录\n• 点击日历外区域可重置选择状态"));
    helpLabel->setStyleSheet("color: #666; font-size: 12px; padding: 10px; background-color: #f5f5f5; border-radius: 5px;");
    helpLabel->setWordWrap(true);
    leftLayout->addWidget(helpLabel);

    // 右侧：统计和管理
    QVBoxLayout* rightLayout = new QVBoxLayout();

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
    connect(m_calendar, &QCalendarWidget::clicked, this, &AttendanceMainWindow::onDateClicked);
    connect(m_calendar, &QCalendarWidget::currentPageChanged,
        this, &AttendanceMainWindow::onMonthChanged);
    connect(m_calendar, &CustomCalendarWidget::deleteRequested,
        this, &AttendanceMainWindow::onDeleteRequested);

    refreshMonthlyView();
}

void AttendanceMainWindow::deleteAttendanceRecord(const QDate& date) {
    AttendanceStorage::deleteRecord(date);
    m_calendar->clearCustomData(date);

    refreshMonthlyView();

    // 显示删除成功消息
    QMessageBox::information(this, QString("删除成功"),
        QString("已成功删除 %1 的考勤记录").arg(date.toString("yyyy-MM-dd")));
}

void AttendanceMainWindow::refreshMonthlyView() {
    const MonthlyAttendanceSnapshot snapshot =
        AttendanceStatsService::buildMonthlySnapshot(m_calendar->yearShown(), m_calendar->monthShown());

    updateCalendarAppearance(snapshot);
    updateMonthlyStatistics(snapshot);
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
