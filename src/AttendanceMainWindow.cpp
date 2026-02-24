#include "AttendanceMainWindow.h"
#include "Utils/CustomCalendarWidget.h"
#include "Utils/TimeSettingDialog.h"
#include "Cal/WorkTimeCalculator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLocale>
#include <QTextCharFormat>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

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
        updateCalendarAppearance();
        updateMonthlyStatistics();
    }
}

void AttendanceMainWindow::onMonthChanged() {
    updateCalendarAppearance();
    updateMonthlyStatistics();
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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法读取文件");
        return;
    }

    QByteArray data = file.readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        QMessageBox::warning(this, "格式错误", "JSON 文件格式无效，请确保是由 Lark-OCR-Sync 生成的。");
        return;
    }

    QJsonArray array = doc.array();
    QSettings settings;
    int count = 0;

    for (const QJsonValue& val : array) {
        QJsonObject obj = val.toObject();

        // 1. 解析日期
        // Python 生成的可能是 "2025-12-1" (单日) 或 "2025-12-01"
        // 使用 yyyy-MM-d 格式可以兼容两者
        QString dateStr = obj["date"].toString();
        QDate date = QDate::fromString(dateStr, "yyyy-MM-d");

        if (!date.isValid()) continue;

        // 2. 获取打卡时间
        QString checkIn = obj["check_in"].toString();
        QString checkOut = obj["check_out"].toString();

        // 3. 如果这一天没有任何打卡记录，跳过（不覆盖已有设置，或者你可以选择覆盖）
        if (checkIn.isEmpty() && checkOut.isEmpty()) {
            continue;
        }

        QString key = date.toString("yyyy-MM-dd");

        // 4. 写入 QSettings
        // 注意：这里需要写入 TimeSettingDialog.cpp 中 loadRecord 读取的那些 key

        // 写入打卡时间
        settings.setValue(key + "/arrival", checkIn);
        settings.setValue(key + "/departure", checkOut);

        // 写入默认配置 (如果不存在)
        // 这样可以避免 WorkTimeCalculator 计算时出现除零错误或逻辑错误
        if (!settings.contains(key + "/needAverageCal"))
            settings.setValue(key + "/needAverageCal", true);

        if (!settings.contains(key + "/workStart"))
            settings.setValue(key + "/workStart", "09:00");

        if (!settings.contains(key + "/workEnd"))
            settings.setValue(key + "/workEnd", "18:00");

        // 午休时间默认值
        if (!settings.contains(key + "/lunchStart"))
            settings.setValue(key + "/lunchStart", "12:30");
        if (!settings.contains(key + "/lunchEnd"))
            settings.setValue(key + "/lunchEnd", "13:30");
        if (!settings.contains(key + "/dinnerStart"))
            settings.setValue(key + "/dinnerStart", "18:00");
        if (!settings.contains(key + "/dinnerEnd"))
            settings.setValue(key + "/dinnerEnd", "18:30");

        count++;
    }

    // 5. 刷新界面
    updateCalendarAppearance(); // 重新绘制日历颜色
    updateMonthlyStatistics();  // 重新计算右侧统计面板

    QMessageBox::information(this, "导入成功", QString("已成功导入 %1 条考勤记录！").arg(count));
}

void AttendanceMainWindow::processExportFile(const QString& filePath) {
    QSettings settings;
    QStringList allKeys = settings.allKeys();
    QSet<QString> validDates;

    // 1. 扫描 QSettings，找出所有存过数据的日期
    // QSettings 的 key 格式通常是 "2025-12-01/arrival"
    for (const QString& key : allKeys) {
        // 截取第一部分作为日期
        QString dateStr = key.section('/', 0, 0);
        // 验证一下是不是合法的日期格式，过滤掉系统配置
        if (QDate::fromString(dateStr, "yyyy-MM-dd").isValid()) {
            validDates.insert(dateStr);
        }
    }

    if (validDates.isEmpty()) {
        QMessageBox::information(this, "提示", "当前没有任何考勤记录可导出。");
        return;
    }

    // 2. 排序日期 (从小到大)
    QStringList sortedDates = validDates.values();
    // 使用 std::sort 进行排序
    std::sort(sortedDates.begin(), sortedDates.end());

    // 3. 组装 JSON 数组
    QJsonArray jsonArray;

    for (const QString& dateStr : sortedDates) {
        QJsonObject obj;
        obj["date"] = dateStr;

        // 核心数据 (兼容 Python 脚本格式)
        obj["check_in"] = settings.value(dateStr + "/arrival").toString();
        obj["check_out"] = settings.value(dateStr + "/departure").toString();

        // [增强] 导出详细配置 (作为备份，防止丢失你的个性化修改)
        // 只有当值存在时才写入，保持 JSON 整洁
        if (settings.contains(dateStr + "/workStart"))
            obj["workStart"] = settings.value(dateStr + "/workStart").toString();

        if (settings.contains(dateStr + "/workEnd"))
            obj["workEnd"] = settings.value(dateStr + "/workEnd").toString();

        if (settings.contains(dateStr + "/needAverageCal"))
            obj["needAverageCal"] = settings.value(dateStr + "/needAverageCal").toBool();

        // 也可以把午休时间导出来，看你需要多详细
        // obj["lunchStart"] = settings.value(dateStr + "/lunchStart").toString();
        // obj["lunchEnd"] = settings.value(dateStr + "/lunchEnd").toString();

        jsonArray.append(obj);
    }

    // 4. 写文件
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "无法保存文件，请检查权限或路径。");
        return;
    }

    QJsonDocument doc(jsonArray);
    file.write(doc.toJson());
    file.close();

    QMessageBox::information(this, "导出成功",
        QString("已成功导出 %1 条记录到:\n%2").arg(jsonArray.size()).arg(filePath));
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

    updateCalendarAppearance();
    updateMonthlyStatistics();
}

void AttendanceMainWindow::deleteAttendanceRecord(const QDate& date) {
    QSettings settings;
    QString key = date.toString("yyyy-MM-dd");

    // 删除所有相关的设置项
    QStringList keys = {
        key + "/needAverageCal",
        key + "/arrival",
        key + "/departure",
        key + "/workStart",
        key + "/workEnd",
        key + "/lunchStart",
        key + "/lunchEnd",
        key + "/dinnerStart",
        key + "/dinnerEnd"
    };

    for (const QString& k : keys) {
        settings.remove(k);
    }

    // 更新界面
    updateCalendarAppearance();
    updateMonthlyStatistics();

    // 显示删除成功消息
    QMessageBox::information(this, QString("删除成功"),
        QString("已成功删除 %1 的考勤记录").arg(date.toString("yyyy-MM-dd")));
}

void AttendanceMainWindow::updateCalendarAppearance() {
    int year = m_calendar->yearShown();
    int month = m_calendar->monthShown();
    QDate startDate(year, month, 1);
    QDate endDate = startDate.addMonths(1).addDays(-1);

    QSettings settings;

    QDate date = startDate;
    while (date <= endDate) {
        QString key = date.toString("yyyy-MM-dd");

        if (settings.contains(key + "/arrival")) {
            // 有打卡记录，显示绿色背景
            QTextCharFormat format;
            QColor defaultCol(144, 238, 144); // 浅绿色
            if (settings.contains(key + "/needAverageCal")) {
                if (!settings.value(key + "/needAverageCal").toBool()) {
                    defaultCol = QColor("#acfdea");
                }
            }
            format.setBackground(defaultCol);
            
            m_calendar->setDateTextFormat(date, format);
        }
        else {
            // 清除格式
            m_calendar->setDateTextFormat(date, QTextCharFormat());
        }
        date = date.addDays(1);
    }
}

void AttendanceMainWindow::updateMonthlyStatistics() {
    int year = m_calendar->yearShown();
    int month = m_calendar->monthShown();
    QDate startDate(year, month, 1);
    QDate endDate = startDate.addMonths(1).addDays(-1);

    QSettings settings;
    int workDays = 0;
    int totalOvertimeMinutes = 0;
    int totalLateMinutes = 0;
    int totalEarlyLeaveMinutes = 0;

    QDate date = startDate;
    while (date <= endDate) {
        QString key = date.toString("yyyy-MM-dd");

        if (settings.contains(key + "/arrival")) {
            workDays++;

            // 加载记录并计算
            AttendanceRecord record;
            record.needAverageCal = settings.value(key + "/needAverageCal").toBool();
            record.arrivalTime = QTime::fromString(settings.value(key + "/arrival").toString(), "hh:mm");
            record.departureTime = QTime::fromString(settings.value(key + "/departure").toString(), "hh:mm");
            record.workStartTime = QTime::fromString(settings.value(key + "/workStart", "09:00").toString(), "hh:mm");
            record.workEndTime = QTime::fromString(settings.value(key + "/workEnd", "18:00").toString(), "hh:mm");
            record.lunchBreakStart = QTime::fromString(settings.value(key + "/lunchStart", "12:30").toString(), "hh:mm");
            record.lunchBreakEnd = QTime::fromString(settings.value(key + "/lunchEnd", "13:30").toString(), "hh:mm");
            record.dinnerBreakStart = QTime::fromString(settings.value(key + "/dinnerStart", "18:00").toString(), "hh:mm");
            record.dinnerBreakEnd = QTime::fromString(settings.value(key + "/dinnerEnd", "18:30").toString(), "hh:mm");

            if (!record.needAverageCal) workDays--;

            // 计算工作时间数据
            WorkTimeResult result = WorkTimeCalculator::calculateWorkTimeResult(record);

            if (result.overtimeMinutes > 0) {
                totalOvertimeMinutes += result.overtimeMinutes;
            }
            totalLateMinutes += result.lateMinutes;
            totalEarlyLeaveMinutes += result.earlyLeaveMinutes;


            //record.print();

            // tableView model 数据映射
            QVariantMap info;
            info["arrivalTime"] = record.arrivalTime.toString("hh:mm");
            info["departureTime"] = record.departureTime.toString("hh:mm");


            m_calendar->setCustomData(date, info);

        }
        date = date.addDays(1);
    }

    QString stats = QString("统计月份: %1年%2月\n")
        .arg(year)
        .arg(month);
    stats += QString("工作天数: %1天\n").arg(workDays);
    stats += QString("总加班时间: %1小时%2分钟\n")
        .arg(totalOvertimeMinutes / 60)
        .arg(totalOvertimeMinutes % 60);
    stats += QString("总迟到时间: %1小时%2分钟\n")
        .arg(totalLateMinutes / 60)
        .arg(totalLateMinutes % 60);
    stats += QString("总早退时间: %1小时%2分钟\n")
        .arg(totalEarlyLeaveMinutes / 60)
        .arg(totalEarlyLeaveMinutes % 60);
    if (workDays > 0) {
        stats += QString("平均加班时间: %1小时")
            .arg(totalOvertimeMinutes / (60.0 * workDays), 0, 'f', 3);
    }

    m_statsLabel->setText(stats);
}
