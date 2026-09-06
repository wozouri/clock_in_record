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
#include <QApplication>
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include <ElaContentDialog.h>
#include <ElaIconButton.h>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QScreen>
#include <ElaToolButton.h>
#include <ElaIcon.h>
#include <ElaAppBar.h>
#include <ElaMessageBar.h>
#include <ElaNavigationBar.h>
#include <ElaPushButton.h>
#include <ElaTeachingTip.h>
#include "Update/UpdateChecker.h"
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

// 叠加在 ElaTeachingTip 上的呼吸灯描边层：蓝色圆角边框周期性明暗，让悬浮提示更醒目。
class TipGlowBorder final : public QWidget {
public:
    explicit TipGlowBorder(ElaTeachingTip* tip)
        : QWidget(tip), m_tip(tip) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(tip->rect());
        tip->installEventFilter(this);
        raise();
        auto* breath = new QVariantAnimation(this);
        breath->setDuration(1600);
        breath->setLoopCount(-1);
        breath->setKeyValueAt(0.0, 0.0);
        breath->setKeyValueAt(0.5, 1.0);
        breath->setKeyValueAt(1.0, 0.0);
        connect(breath, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_breath = value.toDouble();
            update();
        });
        breath->start();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == parentWidget() && event->type() == QEvent::Resize) {
            setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        // 与 ElaTeachingTip 绘制保持一致：本体 = 窗口内缩 8px 的 8px 圆角矩形，
        // 尾巴 = 从本体边缘中心伸出的 8px 三角。
        const QRect bodyRect = rect().adjusted(8, 8, -8, -8);
        const QColor glowColor(23, 105, 170);
        const double factor = 0.5 + 0.5 * m_breath;  // 呼吸系数 0.5..1.0

        const QPainterPath shape = silhouettePath(bodyRect);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        // 三层外晕：宽度递减、浓度递增，叠出柔和光晕
        const struct HaloPass {
            double width;
            int alpha;
        } passes[] = {{7.0, 26}, {5.0, 48}, {3.0, 85}};
        for (const HaloPass& pass : passes) {
            QColor color = glowColor;
            color.setAlpha(int(pass.alpha * factor));
            painter.setPen(QPen(color, pass.width));
            painter.drawPath(shape);
        }
        // 主描边贴住本体轮廓
        QColor mainColor = glowColor;
        mainColor.setAlpha(int(200 * factor));
        painter.setPen(QPen(mainColor, 2));
        painter.drawPath(shape);
    }

private:
    QPainterPath silhouettePath(const QRect& bodyRect) const {
        QPainterPath path;
        path.addRoundedRect(bodyRect, 8, 8);
        const int tailSize = 8;
        QPainterPath tail;
        switch (resolveTail()) {
            case ElaTeachingTip::Bottom: {
                const int cx = width() / 2;
                const int by = bodyRect.bottom();
                tail.moveTo(cx - tailSize, by);
                tail.lineTo(cx, by + tailSize);
                tail.lineTo(cx + tailSize, by);
                tail.closeSubpath();
                break;
            }
            case ElaTeachingTip::Top: {
                const int cx = width() / 2;
                const int ty = bodyRect.top();
                tail.moveTo(cx - tailSize, ty);
                tail.lineTo(cx, ty - tailSize);
                tail.lineTo(cx + tailSize, ty);
                tail.closeSubpath();
                break;
            }
            case ElaTeachingTip::Left: {
                const int lx = bodyRect.left();
                const int cy = height() / 2;
                tail.moveTo(lx, cy - tailSize);
                tail.lineTo(lx - tailSize, cy);
                tail.lineTo(lx, cy + tailSize);
                tail.closeSubpath();
                break;
            }
            case ElaTeachingTip::Right: {
                const int rx = bodyRect.right();
                const int cy = height() / 2;
                tail.moveTo(rx, cy - tailSize);
                tail.lineTo(rx + tailSize, cy);
                tail.lineTo(rx, cy + tailSize);
                tail.closeSubpath();
                break;
            }
            default:
                break;
        }
        return tail.isEmpty() ? path : path.united(tail);
    }

    // 复刻 ElaTeachingTip 的 Auto 尾向解析，保证描边和提示自身尾巴同向。
    ElaTeachingTip::TailPosition resolveTail() const {
        if (m_tip->getTailPosition() != ElaTeachingTip::Auto) {
            return m_tip->getTailPosition();
        }
        QWidget* target = m_tip->getTarget();
        if (!target) {
            return ElaTeachingTip::Bottom;
        }
        const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
        QScreen* screen = QApplication::screenAt(topLeft);
        if (!screen) {
            return ElaTeachingTip::Bottom;
        }
        const QRect screenGeo = screen->availableGeometry();
        const QRect targetRect(topLeft, target->size());
        const QSize tipSize = size();
        const int margin = 12;
        const int spaceAbove = targetRect.top() - screenGeo.top() - margin - tipSize.height();
        const int spaceBelow = screenGeo.bottom() - targetRect.bottom() - margin - tipSize.height();
        const int spaceLeft = targetRect.left() - screenGeo.left() - margin - tipSize.width();
        const int spaceRight = screenGeo.right() - targetRect.right() - margin - tipSize.width();
        const int centerY = targetRect.center().y();
        const bool horizontalFits = (centerY - tipSize.height() / 2 >= screenGeo.top())
            && (centerY + tipSize.height() / 2 <= screenGeo.bottom());
        const int unavailable = -1000000000;
        struct Candidate {
            ElaTeachingTip::TailPosition position;
            int space;
        };
        const Candidate candidates[] = {
            {ElaTeachingTip::Bottom, spaceAbove},
            {ElaTeachingTip::Top, spaceBelow},
            {ElaTeachingTip::Right, horizontalFits ? spaceLeft : unavailable},
            {ElaTeachingTip::Left, horizontalFits ? spaceRight : unavailable},
        };
        const Candidate* best = &candidates[0];
        for (const Candidate& candidate : candidates) {
            if (candidate.space > best->space) {
                best = &candidate;
            }
        }
        return best->position;
    }

    ElaTeachingTip* m_tip = nullptr;
    double m_breath = 0.0;
};

void attachTipGlow(ElaTeachingTip* tip) {
    new TipGlowBorder(tip);
}

// 工具栏锚定的提示优先上/下展开（不遮挡同行按钮），仅按屏幕上下空间取舍；
// 实在放不下时由 ElaTeachingTip 自身的边界钳制兜底。
ElaTeachingTip::TailPosition preferredTipTail(ElaTeachingTip* tip) {
    QWidget* target = tip->getTarget();
    if (!target) {
        return ElaTeachingTip::Bottom;
    }
    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    QScreen* screen = QApplication::screenAt(topLeft);
    if (!screen) {
        return ElaTeachingTip::Bottom;
    }
    const QRect screenGeo = screen->availableGeometry();
    const QRect targetRect(topLeft, target->size());
    const int tipHeight = tip->height() > 40 ? tip->height() : 180;
    const int needed = tipHeight + 12;
    const int spaceAbove = targetRect.top() - screenGeo.top();
    const int spaceBelow = screenGeo.bottom() - targetRect.bottom();
    if (spaceAbove >= needed) {
        return ElaTeachingTip::Bottom;
    }
    if (spaceBelow >= needed) {
        return ElaTeachingTip::Top;
    }
    return spaceAbove >= spaceBelow ? ElaTeachingTip::Bottom : ElaTeachingTip::Top;
}

}  // namespace

// 自绘下载进度条（参考 LUBAN ClientUpdateProgressBar）：8px 高，无文字，方形填充。
// 定义在全局命名空间，与 AttendanceMainWindow.h 中的前置声明匹配。
class AttendanceUpdateBar final : public QWidget {
public:
    explicit AttendanceUpdateBar(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(8);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setPercent(int percent) {
        const int boundedPercent = qBound(0, percent, 100);
        if (m_percent != boundedPercent) {
            m_percent = boundedPercent;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(QStringLiteral("#DCE5F0")));
        if (m_percent > 0) {
            const int filledWidth = qRound(width() * m_percent / 100.0);
            painter.fillRect(QRect(0, 0, filledWidth, height()),
                QColor(QStringLiteral("#2E6FD8")));
        }
    }

private:
    int m_percent = 0;
};

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

    setupUpdateUi();

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
                m_statsContextTip->setTailPosition(ElaTeachingTip::Auto);
                m_statsContextTip->setTarget(m_statsLabel);
                m_statsContextTip->setIsLightDismiss(false);
                m_statsContextTip->setCloseButtonVisible(false);
                attachTipGlow(m_statsContextTip);
            }
            m_statsContextTip->setTitle(QStringLiteral("月度统计"));
            m_statsContextTip->setContent(m_monthlyStatsText);
            m_statsContextTip->setTailPosition(preferredTipTail(m_statsContextTip));
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
        // 新版 ElaPushButton 在样式生效前后 sizeHint 会变化，布局可能缓存旧值；
        // 按图标+间距+文字+内边距显式给定最小宽度，避免个别按钮被压窄裁字。
        button->setMinimumWidth(button->iconSize().width() + 6
            + button->fontMetrics().horizontalAdvance(text) + 26);
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
            m_copyContextTip->setTailPosition(ElaTeachingTip::Auto);
            m_copyContextTip->setTarget(m_copySelectedButton);
            m_copyContextTip->setIsLightDismiss(false);
            m_copyContextTip->setCloseButtonVisible(false);
            m_copyContextTip->setStyleSheet(QStringLiteral(
                "ElaTeachingTip QLabel { color: #1f2937; }"));
            attachTipGlow(m_copyContextTip);
        }
        m_copyContextTip->setTitle(recordTipSummary(date, record));
        m_copyContextTip->setContent(QString());
        m_copyContextTip->setTailPosition(preferredTipTail(m_copyContextTip));
        m_copyContextTip->showTip();
    } else if (m_copyContextTip) {
        m_copyContextTip->closeTip();
    }

    if (m_hasCopiedRecord) {
        if (!m_applyContextTip) {
            m_applyContextTip = new ElaTeachingTip(this);
            m_applyContextTip->setTailPosition(ElaTeachingTip::Auto);
            m_applyContextTip->setTarget(m_applyCopiedButton);
            m_applyContextTip->setIsLightDismiss(false);
            m_applyContextTip->setCloseButtonVisible(false);
            m_applyContextTip->setStyleSheet(QStringLiteral(
                "ElaTeachingTip QLabel { color: #1f2937; }"));
            attachTipGlow(m_applyContextTip);
        }
        m_applyContextTip->setTitle(recordTipSummary(m_copiedFromDate, m_copiedRecord));
        m_applyContextTip->setContent(QString());
        m_applyContextTip->setTailPosition(preferredTipTail(m_applyContextTip));
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

void AttendanceMainWindow::setupUpdateUi() {
    m_updateChecker = new UpdateChecker(this);

    m_updateToolsHost = new QWidget(this);
    m_updateToolsHost->setFixedHeight(48);
    auto* toolsLayout = new QHBoxLayout(m_updateToolsHost);
    toolsLayout->setContentsMargins(4, 0, 4, 0);
    toolsLayout->setSpacing(2);

    const auto makeAppBarButton = [this](ElaIconType::IconName icon) {
        auto* button = new ElaToolButton(m_updateToolsHost);
        button->setElaIcon(icon);
        button->setFixedSize(40, 48);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_updateCheckButton = makeAppBarButton(ElaIconType::ArrowsRotate);
    m_updateCheckButton->setToolTip(QStringLiteral("检查更新"));
    // 角标用 ElaIconButton 以支持醒目的独立配色；更新就绪时常亮绿色。
    m_updateIndicatorButton =
        new ElaIconButton(ElaIconType::CloudArrowDown, 18, 40, 48, m_updateToolsHost);
    m_updateIndicatorButton->setCursor(Qt::PointingHandCursor);
    m_updateIndicatorButton->setLightIconColor(QColor(QStringLiteral("#1AA229")));
    m_updateIndicatorButton->setLightHoverIconColor(QColor(QStringLiteral("#128020")));
    m_updateIndicatorButton->setDarkIconColor(QColor(QStringLiteral("#3DD05C")));
    m_updateIndicatorButton->setDarkHoverIconColor(QColor(QStringLiteral("#6BE384")));
    m_updateIndicatorButton->setVisible(false);
    toolsLayout->addWidget(m_updateCheckButton);
    toolsLayout->addWidget(m_updateIndicatorButton);
    setCustomWidget(ElaAppBarType::RightArea, m_updateToolsHost);
    // 在自定义区域前插入弹性空隙，使其紧贴最小化/关闭按钮。
    if (auto* hostAppBar = qobject_cast<ElaAppBar*>(m_updateToolsHost->parentWidget())) {
        if (auto* boxLayout = qobject_cast<QBoxLayout*>(hostAppBar->layout())) {
            const int hostIndex = boxLayout->indexOf(m_updateToolsHost);
            if (hostIndex >= 0) {
                boxLayout->insertStretch(hostIndex);
            }
        }
    }

    connect(m_updateCheckButton, &ElaToolButton::clicked,
        this, &AttendanceMainWindow::onCheckForUpdatesClicked);
    connect(m_updateIndicatorButton, &ElaIconButton::clicked,
        this, &AttendanceMainWindow::showUpdateConfirmDialog);
    connect(m_updateChecker, &UpdateChecker::checkFinished,
        this, &AttendanceMainWindow::onUpdateCheckFinished);
    connect(m_updateChecker, &UpdateChecker::downloadProgress,
        this, &AttendanceMainWindow::onUpdateDownloadProgress);
    connect(m_updateChecker, &UpdateChecker::applyReady,
        this, &AttendanceMainWindow::onUpdateApplyReady);
    connect(m_updateChecker, &UpdateChecker::failed,
        this, &AttendanceMainWindow::onUpdateFailed);

    // 重启后读取"待更新"标记，提示本次更新已完成。
    const QString pendingVersion = UpdateChecker::takePendingUpdateVersion();
    if (!pendingVersion.isEmpty()) {
        QTimer::singleShot(600, this, [this, pendingVersion] {
            ElaMessageBar::success(ElaMessageBarType::Top, QStringLiteral("更新"),
                QStringLiteral("已更新到版本 %1").arg(pendingVersion), 4000, this);
        });
    }
    // 启动后静默检查一次更新；有新版本只亮角标，不打扰。
    QTimer::singleShot(3000, this, [this] {
        if (!m_updateChecker->isDownloadInProgress()) {
            m_updateChecker->checkForUpdates(false);
        }
    });
}

void AttendanceMainWindow::onCheckForUpdatesClicked() {
    if (m_updateChecker->isDownloadInProgress()) {
        return;
    }
    m_updateCheckButton->setEnabled(false);
    m_updateChecker->checkForUpdates(true);
}

void AttendanceMainWindow::onUpdateCheckFinished(const UpdateReleaseInfo& info, bool userInitiated) {
    m_updateCheckButton->setEnabled(true);
    m_availableUpdate = info;
    m_hasAvailableUpdate = info.available && info.isNewer;
    m_updateIndicatorButton->setVisible(m_hasAvailableUpdate);
    if (m_hasAvailableUpdate) {
        m_updateIndicatorButton->setToolTip(
            QStringLiteral("发现新版本 %1，点击更新").arg(info.version));
        if (userInitiated) {
            ElaMessageBar::success(ElaMessageBarType::Top, QStringLiteral("检查更新"),
                QStringLiteral("发现新版本 %1，请点击标题栏的下载图标。").arg(info.version),
                4000, this);
        }
    }
    else if (userInitiated) {
        if (!info.errorMessage.isEmpty()) {
            ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("检查更新"),
                info.errorMessage, 4000, this);
        }
        else {
            ElaMessageBar::information(ElaMessageBarType::Top, QStringLiteral("检查更新"),
                QStringLiteral("当前已是最新版本。"), 3000, this);
        }
    }
}

void AttendanceMainWindow::showUpdateConfirmDialog() {
    if (!m_hasAvailableUpdate || m_updateChecker->isDownloadInProgress()) {
        return;
    }
    auto* dialog = new ElaContentDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setTitleText(QStringLiteral("发现新版本 %1").arg(m_availableUpdate.version));
    dialog->setSubTitleText(m_availableUpdate.notes.isEmpty()
        ? QStringLiteral("是否下载并安装更新？更新完成后程序将自动重启。")
        : m_availableUpdate.notes);
    dialog->setLeftButtonText(QStringLiteral("取消"));
    dialog->setMiddleButtonVisible(false);
    dialog->setRightButtonText(QStringLiteral("更新并重启"));
    connect(dialog, &ElaContentDialog::rightButtonClicked, this, [this, dialog] {
        dialog->close();
        startUpdateDownload();
    });
    dialog->exec();
}

void AttendanceMainWindow::startUpdateDownload() {
    // 无边框置顶下载弹窗，样式与 LUBAN 客户端更新一致。
    auto* progressDialog = new QDialog(nullptr);
    progressDialog->setWindowTitle(QStringLiteral("客户端更新"));
    progressDialog->setWindowModality(Qt::ApplicationModal);
    progressDialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint
        | Qt::WindowStaysOnTopHint);
    progressDialog->setFixedSize(440, 198);
    progressDialog->setObjectName(QStringLiteral("clientUpdateProgress"));
    progressDialog->setAttribute(Qt::WA_DeleteOnClose);
    progressDialog->setStyleSheet(QStringLiteral(
        "QDialog#clientUpdateProgress { background: #F7FAFC; border: 1px solid #D6DFEA; }"
        "QLabel#updateTitle { color: #13213A; font-size: 17px; font-weight: 600; }"
        "QLabel#updateStatus { color: #53647A; font-size: 13px; }"
        "QPushButton { color: #244B82; background: transparent; border: 1px solid #C5D3E4;"
        " padding: 6px 18px; }"
        "QPushButton:hover { background: #E8F0FA; }"));

    auto* layout = new QVBoxLayout(progressDialog);
    layout->setContentsMargins(26, 24, 26, 20);
    layout->setSpacing(10);
    auto* titleLabel = new QLabel(QStringLiteral("客户端更新"), progressDialog);
    titleLabel->setObjectName(QStringLiteral("updateTitle"));
    auto* statusLabel = new QLabel(QStringLiteral("正在准备下载最新版本..."), progressDialog);
    statusLabel->setObjectName(QStringLiteral("updateStatus"));
    auto* progressBar = new AttendanceUpdateBar(progressDialog);
    auto* progressRow = new QHBoxLayout;
    progressRow->addWidget(progressBar, 1);
    auto* actionRow = new QHBoxLayout;
    actionRow->addStretch();
    auto* cancelButton = new QPushButton(QStringLiteral("取消下载"), progressDialog);
    cancelButton->setCursor(Qt::PointingHandCursor);
    actionRow->addWidget(cancelButton);
    layout->addWidget(titleLabel);
    layout->addWidget(statusLabel);
    layout->addLayout(progressRow);
    layout->addStretch();
    layout->addLayout(actionRow);

    m_updateProgressDialog = progressDialog;
    m_updateProgressBar = progressBar;
    m_updateStatusLabel = statusLabel;

    // 背景遮罩：半透明深色盖住主窗口，参考 LUBAN clientUpdateOverlay。
    m_updateOverlay = new QWidget(this);
    m_updateOverlay->setObjectName(QStringLiteral("clientUpdateOverlay"));
    m_updateOverlay->setAttribute(Qt::WA_StyledBackground);
    m_updateOverlay->setGeometry(rect());
    m_updateOverlay->setStyleSheet(QStringLiteral(
        "QWidget#clientUpdateOverlay { background: rgba(24, 39, 62, 72); }"));
    m_updateOverlay->show();
    m_updateOverlay->raise();

    connect(cancelButton, &QPushButton::clicked, this, [this] {
        m_updateChecker->cancelDownload();
    });
    progressDialog->show();
    // 弹窗居中到主窗口。
    progressDialog->move(geometry().center().x() - progressDialog->width() / 2,
        geometry().center().y() - progressDialog->height() / 2);
    progressDialog->raise();
    progressDialog->activateWindow();

    m_updateChecker->startDownload();
}

void AttendanceMainWindow::onUpdateDownloadProgress(int percent) {
    if (m_updateProgressBar != nullptr) {
        m_updateProgressBar->setPercent(percent);
    }
    if (m_updateStatusLabel != nullptr) {
        m_updateStatusLabel->setText(QStringLiteral("正在下载客户端更新... %1%").arg(percent));
    }
}

void AttendanceMainWindow::onUpdateApplyReady(const QString& version) {
    closeUpdateProgressDialog();
    ElaMessageBar::success(ElaMessageBarType::Top, QStringLiteral("更新"),
        QStringLiteral("版本 %1 已就绪，程序将自动重启。").arg(version), 3000, this);
    QTimer::singleShot(800, qApp, &QCoreApplication::quit);
}

void AttendanceMainWindow::onUpdateFailed(const QString& message) {
    closeUpdateProgressDialog();
    ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("更新"), message, 4000, this);
}

void AttendanceMainWindow::closeUpdateProgressDialog() {
    if (m_updateProgressDialog != nullptr) {
        m_updateProgressDialog->close();
        m_updateProgressDialog->deleteLater();
        m_updateProgressDialog = nullptr;
        m_updateProgressBar = nullptr;
        m_updateStatusLabel = nullptr;
    }
    if (m_updateOverlay != nullptr) {
        m_updateOverlay->hide();
        m_updateOverlay->deleteLater();
        m_updateOverlay = nullptr;
    }
}

