#include "CustomCalendarWidget.h"

#include "Data/AttendanceStorage.h"

#include <ElaIcon.h>
#include <ElaMenu.h>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEventLoop>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QTextDocument>
#include <QtMath>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMonthHeaderHeight = 54;
constexpr int kWeekHeaderHeight = 30;
constexpr int kGridRows = 6;
constexpr int kGridColumns = 7;
constexpr int kHorizontalPadding = 6;
constexpr int kBottomPadding = 6;

const QStringList kWeekdayNames = {
    QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
    QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
    QStringLiteral("周日")
};

class CalendarContextMenu final : public ElaMenu
{
public:
    using ElaMenu::ElaMenu;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        ElaMenu::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            hide();
            return;
        }
        ElaMenu::mouseReleaseEvent(event);
    }
};

QColor softenedRecordColor(const QColor& color)
{
    if (!color.isValid()) {
        return QColor(QStringLiteral("#d8f6df"));
    }

    QColor result = color.lighter(108);
    result.setAlpha(205);
    return result;
}
}

class CalendarNoteTip final : public QWidget
{
public:
    explicit CalendarNoteTip(QWidget* parent)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                            | Qt::WindowDoesNotAcceptFocus)
        , m_content(new QLabel(this))
    {
        setObjectName(QStringLiteral("calendarNoteTip"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet(QStringLiteral("QWidget#calendarNoteTip { border: none; background: transparent; }"));
        m_content->setTextFormat(Qt::PlainText);
        m_content->setWordWrap(true);
        m_content->setStyleSheet(QStringLiteral("color: #31465d; font-size: 12px;"));
        m_glowTimer.setInterval(42);
        connect(&m_glowTimer, &QTimer::timeout, this, [this] {
            m_glowPhase = (m_glowPhase + 1) % 96;
            update();
        });
    }

    void showForCell(QWidget* anchor, const QRect& cell, const QString& note)
    {
        m_anchor = anchor;
        m_cell = cell;
        m_note = note;
        trackWindow(anchor ? anchor->window() : nullptr);
        updateGeometryForContent();
        reposition();
        show();
        raise();
        if (!m_glowTimer.isActive()) {
            m_glowTimer.start();
        }
    }

    void hideTip()
    {
        hide();
        m_glowTimer.stop();
        m_glowPhase = 0;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        constexpr double kTwoPi = 6.283185307179586;
        const double phase = static_cast<double>(m_glowPhase) * kTwoPi / 96.0;
        const int glowAlpha = 34 + qRound((std::sin(phase) + 1.0) * 18.0);
        painter.setPen(QPen(QColor(55, 131, 190, glowAlpha), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 7, 7);
        painter.setPen(QPen(QColor(QStringLiteral("#b9cad9"))));
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 6, 6);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_trackedWindow && isVisible()
            && (event->type() == QEvent::Move || event->type() == QEvent::Resize
                || event->type() == QEvent::WindowStateChange)) {
            reposition();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    QScreen* currentScreen() const
    {
        if (!m_anchor) {
            return nullptr;
        }
        if (QScreen* screen = QApplication::screenAt(m_anchor->mapToGlobal(m_cell.center()))) {
            return screen;
        }
        return m_anchor->windowHandle() ? m_anchor->windowHandle()->screen() : nullptr;
    }

    void trackWindow(QWidget* window)
    {
        if (m_trackedWindow == window) {
            return;
        }
        if (m_trackedWindow) {
            m_trackedWindow->removeEventFilter(this);
        }
        m_trackedWindow = window;
        if (m_trackedWindow) {
            m_trackedWindow->installEventFilter(this);
        }
    }

    void updateGeometryForContent()
    {
        const QRect available = currentScreen() ? currentScreen()->availableGeometry() : QRect(0, 0, 800, 600);
        const int horizontalMargin = 14;
        const int verticalMargin = 10;
        const int maxTextWidth = qMax(160, qMin(400, available.width() - 48));
        const int minimumTextWidth = qMin(180, maxTextWidth);
        QFontMetrics metrics(m_content->font());
        int naturalTextWidth = 0;
        for (const QString& line : m_note.split(QLatin1Char('\n'))) {
            naturalTextWidth = qMax(naturalTextWidth, metrics.horizontalAdvance(line));
        }
        const int textWidth = qBound(minimumTextWidth, naturalTextWidth + 2, maxTextWidth);
        m_content->setText(m_note);
        // setFixedHeight() constrains QLabel's subsequent heightForWidth() calls.
        // Reset it before measuring a shorter replacement note.
        m_content->setMinimumHeight(0);
        m_content->setMaximumHeight(QWIDGETSIZE_MAX);
        m_content->setFixedWidth(textWidth);

        QTextDocument textDocument;
        textDocument.setDocumentMargin(0);
        textDocument.setDefaultFont(m_content->font());
        textDocument.setPlainText(m_note);
        textDocument.setTextWidth(textWidth);
        const int maxTextHeight = qMax(metrics.lineSpacing(), available.height() - 48);
        const int textHeight = qMin(maxTextHeight,
            qMax(metrics.lineSpacing(), qCeil(textDocument.size().height())));
        m_content->setFixedHeight(textHeight);
        resize(textWidth + horizontalMargin * 2, textHeight + verticalMargin * 2);
        m_content->setGeometry(horizontalMargin, verticalMargin, textWidth, textHeight);
    }

    void reposition()
    {
        QScreen* screen = currentScreen();
        if (!screen || !m_anchor) {
            return;
        }
        const QRect available = screen->availableGeometry();
        const QRect cellGlobal(m_anchor->mapToGlobal(m_cell.topLeft()), m_cell.size());
        constexpr int kGap = 8;
        QPoint position(cellGlobal.center().x() - width() / 2, cellGlobal.top() - height() - kGap);
        if (position.y() < available.top() + kGap) {
            position.setY(cellGlobal.bottom() + kGap);
        }
        position.setX(qBound(available.left() + kGap, position.x(),
            available.right() - width() - kGap));
        position.setY(qBound(available.top() + kGap, position.y(),
            available.bottom() - height() - kGap));
        move(position);
    }

    QLabel* m_content = nullptr;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_trackedWindow;
    QRect m_cell;
    QString m_note;
    QTimer m_glowTimer;
    int m_glowPhase = 0;
};

CustomCalendarWidget::CustomCalendarWidget(QWidget* parent)
    : QWidget(parent)
    , m_pageDate(QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1))
{
    setMouseTracking(true);
    m_noteTip = new CalendarNoteTip(this);
    setMinimumHeight(440);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void CustomCalendarWidget::setFirstDayOfWeek(Qt::DayOfWeek dayOfWeek)
{
    if (m_firstDayOfWeek == dayOfWeek) {
        return;
    }
    m_firstDayOfWeek = dayOfWeek;
    update();
}

void CustomCalendarWidget::setGridVisible(bool visible)
{
    if (m_gridVisible == visible) {
        return;
    }
    m_gridVisible = visible;
    update();
}

int CustomCalendarWidget::yearShown() const
{
    return m_pageDate.year();
}

int CustomCalendarWidget::monthShown() const
{
    return m_pageDate.month();
}

bool CustomCalendarWidget::isYearOverviewVisible() const
{
    return m_yearOverviewVisible;
}

void CustomCalendarWidget::setCurrentPage(int year, int month)
{
    const QDate requestedPage(year, month, 1);
    if (!requestedPage.isValid()) {
        return;
    }

    const bool leavingYearOverview = m_yearOverviewVisible;
    if (requestedPage == m_pageDate && !leavingYearOverview) {
        return;
    }

    m_yearOverviewVisible = false;
    m_pageDate = requestedPage;
    m_hoveredDate = QDate();
    m_noteTip->hideTip();
    update();
    if (leavingYearOverview) {
        emit yearOverviewVisibilityChanged(false);
    }
    emit currentPageChanged(year, month);
}

void CustomCalendarWidget::setYearOverviewVisible(bool visible)
{
    if (m_yearOverviewVisible == visible) {
        return;
    }

    m_yearOverviewVisible = visible;
    m_hoveredDate = QDate();
    m_noteTip->hideTip();
    if (visible) {
        refreshYearRecordDates();
    }
    update();
    emit yearOverviewVisibilityChanged(visible);
    if (!visible) {
        emit currentPageChanged(m_pageDate.year(), m_pageDate.month());
    }
}

void CustomCalendarWidget::setDateTextFormat(const QDate& date, const QTextCharFormat& format)
{
    const QColor background = format.background().color();
    if (background.isValid() && format.background().style() != Qt::NoBrush) {
        m_dayBackgrounds.insert(date, background);
    } else {
        m_dayBackgrounds.remove(date);
    }
    update();
}

void CustomCalendarWidget::setCustomData(const QDate& date, const QVariantMap& value)
{
    m_data.insert(date, value);
    if (date == m_hoveredDate) {
        refreshHoveredNoteTip();
    }
    update();
}

void CustomCalendarWidget::clearCustomData(const QDate& date)
{
    if (m_data.remove(date) > 0) {
        if (date == m_hoveredDate) {
            refreshHoveredNoteTip();
        }
        update();
    }
}

QList<QDate> CustomCalendarWidget::selectedDates() const
{
    QList<QDate> dates = m_selectedDates;
    std::sort(dates.begin(), dates.end());
    return dates;
}

void CustomCalendarWidget::setSelectedDates(const QList<QDate>& dates)
{
    QList<QDate> normalizedDates;
    for (const QDate& date : dates) {
        if (date.isValid() && !normalizedDates.contains(date)) {
            normalizedDates.append(date);
        }
    }
    std::sort(normalizedDates.begin(), normalizedDates.end());

    if (normalizedDates == selectedDates()) {
        return;
    }

    m_selectedDates = normalizedDates;
    m_selectionAnchorDate = m_selectedDates.isEmpty() ? QDate() : m_selectedDates.last();
    update();
    emit selectionChanged();
}

void CustomCalendarWidget::clearSelection()
{
    if (m_selectedDates.isEmpty()) {
        return;
    }
    m_selectedDates.clear();
    m_selectionAnchorDate = QDate();
    update();
    emit selectionChanged();
}

void CustomCalendarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(QStringLiteral("#ffffff")));

    const QRect monthHeader(0, 0, width(), kMonthHeaderHeight);
    painter.setPen(QColor(QStringLiteral("#dfe7ef")));
    painter.drawLine(monthHeader.bottomLeft(), monthHeader.bottomRight());

    const auto drawNavigationButton = [&painter](const QRect& buttonRect, bool pointsLeft, bool hovered) {
        painter.save();
        painter.setPen(QPen(hovered ? QColor(QStringLiteral("#8eb9dc"))
                                    : QColor(QStringLiteral("#dbe6ef"))));
        painter.setBrush(hovered ? QColor(QStringLiteral("#dceefd"))
                                 : QColor(QStringLiteral("#f7fafc")));
        painter.drawRoundedRect(buttonRect, 5, 5);
        painter.setPen(QPen(hovered ? QColor(QStringLiteral("#1769aa"))
                                    : QColor(QStringLiteral("#52708e")), 2,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPoint center = buttonRect.center();
        const int horizontalOffset = 3;
        const int verticalOffset = 6;
        if (pointsLeft) {
            painter.drawLine(center + QPoint(horizontalOffset, -verticalOffset),
                center + QPoint(-horizontalOffset, 0));
            painter.drawLine(center + QPoint(-horizontalOffset, 0),
                center + QPoint(horizontalOffset, verticalOffset));
        } else {
            painter.drawLine(center + QPoint(-horizontalOffset, -verticalOffset),
                center + QPoint(horizontalOffset, 0));
            painter.drawLine(center + QPoint(horizontalOffset, 0),
                center + QPoint(-horizontalOffset, verticalOffset));
        }
        painter.restore();
    };
    drawNavigationButton(previousMonthButtonRect(), true, m_hoveredHeaderControl == 1);
    drawNavigationButton(nextMonthButtonRect(), false, m_hoveredHeaderControl == 2);

    painter.save();
    QFont titleFont = painter.font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#172b4d")));
    painter.drawText(monthHeader, Qt::AlignCenter, m_yearOverviewVisible
        ? QStringLiteral("%1 年").arg(m_pageDate.year())
        : QStringLiteral("%1 年 %2 月").arg(m_pageDate.year()).arg(m_pageDate.month()));
    painter.restore();

    painter.save();
    const QRect viewButton = yearOverviewButtonRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#eef5fb")));
    painter.drawRoundedRect(viewButton, 5, 5);
    painter.setPen(QColor(QStringLiteral("#28649a")));
    QFont viewButtonFont = painter.font();
    viewButtonFont.setPointSize(9);
    viewButtonFont.setBold(true);
    painter.setFont(viewButtonFont);
    painter.drawText(viewButton, Qt::AlignCenter,
        m_yearOverviewVisible ? QStringLiteral("月视图") : QStringLiteral("全年预览"));
    painter.restore();

    if (m_yearOverviewVisible) {
        paintYearOverview(painter);
        return;
    }

    const QRect weekdayHeader = weekHeaderRect();
    painter.fillRect(weekdayHeader, QColor(QStringLiteral("#f8fafc")));
    QFont weekdayFont = painter.font();
    weekdayFont.setPointSize(9);
    weekdayFont.setBold(true);
    painter.setFont(weekdayFont);
    for (int column = 0; column < kGridColumns; ++column) {
        const int weekday = (static_cast<int>(m_firstDayOfWeek) - 1 + column) % kGridColumns;
        const QRect weekdayRect(weekdayHeader.left() + column * weekdayHeader.width() / kGridColumns,
            weekdayHeader.top(), weekdayHeader.width() / kGridColumns, weekdayHeader.height());
        painter.setPen(weekday >= 5 ? QColor(QStringLiteral("#d44d44"))
                                    : QColor(QStringLiteral("#50657d")));
        painter.drawText(weekdayRect, Qt::AlignCenter, kWeekdayNames.at(weekday));
    }
    painter.setPen(QColor(QStringLiteral("#dfe7ef")));
    painter.drawLine(weekdayHeader.bottomLeft(), weekdayHeader.bottomRight());

    const QDate firstDate = firstVisibleDate();
    const QDate today = QDate::currentDate();
    for (int row = 0; row < kGridRows; ++row) {
        for (int column = 0; column < kGridColumns; ++column) {
            const QRect cell = cellRect(row, column);
            const QDate date = firstDate.addDays(row * kGridColumns + column);
            const bool isCurrentMonth = date.month() == m_pageDate.month()
                && date.year() == m_pageDate.year();
            const bool isWeekend = date.dayOfWeek() == Qt::Saturday || date.dayOfWeek() == Qt::Sunday;
            const bool selected = isDateSelected(date);
            const bool contextMenuTarget = m_contextMenuDates.contains(date);
            const bool hovered = m_hoveredDate == date;
            const QVariantMap dayData = m_data.value(date);
            const bool hasRecord = !dayData.isEmpty();

            if (m_gridVisible) {
                painter.setPen(QColor(QStringLiteral("#e5ebf1")));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
            }

            const QRect cardRect = cell.adjusted(4, 4, -4, -4);
            if (hasRecord) {
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(softenedRecordColor(m_dayBackgrounds.value(date)));
                painter.drawRoundedRect(cardRect, 6, 6);
                painter.restore();
            } else if (selected || (hovered && isCurrentMonth)) {
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(selected ? QColor(QStringLiteral("#d9ecfb"))
                                          : QColor(QStringLiteral("#f2f7fc")));
                painter.drawRoundedRect(cardRect, 6, 6);
                painter.restore();
            }

            if (selected) {
                painter.save();
                painter.setPen(QPen(QColor(QStringLiteral("#1f79bd")), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 5, 5);
                painter.restore();
            }
            if (contextMenuTarget) {
                painter.save();
                painter.setPen(QPen(QColor(QStringLiteral("#7867aa")), 1,
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(cardRect.adjusted(4, 4, -4, -4), 3, 3);
                painter.restore();
            } else if (!selected && date == today) {
                painter.save();
                painter.setPen(QPen(QColor(QStringLiteral("#5d9ed4")), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 5, 5);
                painter.restore();
            }

            painter.save();
            QFont dayFont = painter.font();
            dayFont.setPointSize(10);
            dayFont.setBold(selected || date == today);
            painter.setFont(dayFont);
            QColor dayColor = QColor(QStringLiteral("#263b53"));
            if (!isCurrentMonth) {
                dayColor = QColor(QStringLiteral("#b0bac5"));
            } else if (isWeekend) {
                dayColor = QColor(QStringLiteral("#d64d4d"));
            }
            painter.setPen(dayColor);
            painter.drawText(QRect(cardRect.left() + 8, cardRect.top() + 5,
                                 cardRect.width() - 16, 18),
                Qt::AlignLeft | Qt::AlignVCenter, QString::number(date.day()));
            painter.restore();

            if (hasRecord) {
                painter.save();
                QFont timeFont = painter.font();
                timeFont.setPointSize(cell.height() >= 72 ? 8 : 7);
                timeFont.setBold(true);
                painter.setFont(timeFont);
                painter.setPen(QColor(QStringLiteral("#1269b0")));
                const QRect arrivalRect(cardRect.left() + 6, cardRect.top() + cardRect.height() / 2 - 13,
                    cardRect.width() - 12, 14);
                const QRect departureRect(cardRect.left() + 6, cardRect.top() + cardRect.height() / 2 + 3,
                    cardRect.width() - 12, 14);
                painter.drawText(arrivalRect, Qt::AlignCenter,
                    dayData.value(QStringLiteral("arrivalTime")).toString());
                painter.setPen(QColor(QStringLiteral("#2359a6")));
                painter.drawText(departureRect, Qt::AlignCenter,
                    dayData.value(QStringLiteral("departureTime")).toString());
                painter.restore();
            }

            if (dayData.value(QStringLiteral("hasNote")).toBool()) {
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#e49b26")));
                painter.drawEllipse(QPoint(cardRect.right() - 9, cardRect.top() + 10), 3, 3);
                painter.restore();
            }

            if (dayData.value(QStringLiteral("hasMealAllowance")).toBool()) {
                painter.save();
                const QRect mealAllowanceRect(cardRect.left() + 7, cardRect.bottom() - 19, 18, 14);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#e07a28")));
                painter.drawRoundedRect(mealAllowanceRect, 3, 3);
                QFont mealAllowanceFont = painter.font();
                mealAllowanceFont.setPointSize(7);
                mealAllowanceFont.setBold(true);
                painter.setFont(mealAllowanceFont);
                painter.setPen(Qt::white);
                painter.drawText(mealAllowanceRect, Qt::AlignCenter, QStringLiteral("餐"));
                painter.restore();
            }
        }
    }

    if (m_dragSelectionActive && !m_dragSelectionRect.isNull()) {
        painter.save();
        painter.setPen(QPen(QColor(QStringLiteral("#3c8bc8")), 1, Qt::DashLine));
        painter.setBrush(QColor(64, 145, 206, 32));
        painter.drawRect(m_dragSelectionRect.normalized());
        painter.restore();
    }
}

void CustomCalendarWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_yearOverviewVisible) {
        if (previousMonthButtonRect().contains(event->pos())) {
            m_pageDate = m_pageDate.addYears(-1);
            refreshYearRecordDates();
            update();
            return;
        }
        if (nextMonthButtonRect().contains(event->pos())) {
            m_pageDate = m_pageDate.addYears(1);
            refreshYearRecordDates();
            update();
            return;
        }
        if (yearOverviewButtonRect().contains(event->pos())) {
            setYearOverviewVisible(false);
            return;
        }
        for (int month = 1; month <= 12; ++month) {
            if (monthPreviewRect(month).contains(event->pos())) {
                setCurrentPage(m_pageDate.year(), month);
                return;
            }
        }
        return;
    }

    if (yearOverviewButtonRect().contains(event->pos())) {
        setYearOverviewVisible(true);
        return;
    }
    if (previousMonthButtonRect().contains(event->pos())) {
        const QDate previous = m_pageDate.addMonths(-1);
        setCurrentPage(previous.year(), previous.month());
        return;
    }
    if (nextMonthButtonRect().contains(event->pos())) {
        const QDate next = m_pageDate.addMonths(1);
        setCurrentPage(next.year(), next.month());
        return;
    }

    m_dragStartDate = dateAt(event->pos());
    m_dragStartPosition = event->pos();
    m_dragModifiers = event->modifiers();
    m_dragSelectionActive = false;
    m_dragSelectionRect = QRect();
    if (m_dragStartDate.isValid() && isCurrentPageDate(m_dragStartDate)) {
        event->accept();
        return;
    }
    m_dragStartDate = QDate();
    QWidget::mousePressEvent(event);
}

void CustomCalendarWidget::mouseMoveEvent(QMouseEvent* event)
{
    updateHoveredDate(event->pos());
    if (!m_dragStartDate.isValid() || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragSelectionActive
        && (event->pos() - m_dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragSelectionActive = true;
    }
    if (m_dragSelectionActive) {
        m_dragSelectionRect = QRect(m_dragStartPosition, event->pos()).normalized();
        update();
    }
    event->accept();
}

void CustomCalendarWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_dragStartDate.isValid()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_dragSelectionActive) {
        finishRubberBandSelection();
    } else {
        selectDateFromClick(m_dragStartDate, m_dragModifiers);
    }
    m_dragStartDate = QDate();
    m_dragSelectionActive = false;
    m_dragSelectionRect = QRect();
    update();
    event->accept();
}

void CustomCalendarWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QDate date = dateAt(event->pos());
        if (date.isValid() && isCurrentPageDate(date)) {
            setSingleSelection(date);
            emit dateDoubleClicked(date);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CustomCalendarWidget::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    if (delta.y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    if (m_yearOverviewVisible) {
        m_pageDate = m_pageDate.addYears(delta.y() > 0 ? -1 : 1);
        refreshYearRecordDates();
        update();
    } else {
        const QDate nextPage = m_pageDate.addMonths(delta.y() > 0 ? -1 : 1);
        setCurrentPage(nextPage.year(), nextPage.month());
    }
    event->accept();
}

void CustomCalendarWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_yearOverviewVisible) {
        event->ignore();
        return;
    }
    showContextMenu(event->pos(), event->globalPos());
    event->accept();
}

void CustomCalendarWidget::enterEvent(QEvent* event)
{
    QWidget::enterEvent(event);
    emit pointerInsideChanged(true);
}

void CustomCalendarWidget::leaveEvent(QEvent* event)
{
    emit pointerInsideChanged(false);
    m_noteTip->hideTip();
    if (m_hoveredDate.isValid() || m_hoveredHeaderControl != 0) {
        m_hoveredDate = QDate();
        m_hoveredHeaderControl = 0;
        update();
    }
    QWidget::leaveEvent(event);
}

QRect CustomCalendarWidget::previousMonthButtonRect() const
{
    return QRect(14, 12, 30, 30);
}

QRect CustomCalendarWidget::nextMonthButtonRect() const
{
    return QRect(width() - 44, 12, 30, 30);
}

QRect CustomCalendarWidget::yearOverviewButtonRect() const
{
    return QRect(width() - 120, 12, 66, 30);
}

QRect CustomCalendarWidget::weekHeaderRect() const
{
    return QRect(0, kMonthHeaderHeight, width(), kWeekHeaderHeight);
}

QRect CustomCalendarWidget::gridRect() const
{
    return QRect(kHorizontalPadding, kMonthHeaderHeight + kWeekHeaderHeight,
        qMax(0, width() - kHorizontalPadding * 2),
        qMax(0, height() - kMonthHeaderHeight - kWeekHeaderHeight - kBottomPadding));
}

QRect CustomCalendarWidget::cellRect(int row, int column) const
{
    const QRect grid = gridRect();
    const int left = grid.left() + column * grid.width() / kGridColumns;
    const int right = grid.left() + (column + 1) * grid.width() / kGridColumns;
    const int top = grid.top() + row * grid.height() / kGridRows;
    const int bottom = grid.top() + (row + 1) * grid.height() / kGridRows;
    return QRect(left, top, right - left, bottom - top);
}

QRect CustomCalendarWidget::yearGridRect() const
{
    return QRect(12, kMonthHeaderHeight + 10, qMax(0, width() - 24),
        qMax(0, height() - kMonthHeaderHeight - 18));
}

QRect CustomCalendarWidget::monthPreviewRect(int month) const
{
    const QRect grid = yearGridRect();
    const int row = (month - 1) / 3;
    const int column = (month - 1) % 3;
    const int left = grid.left() + column * grid.width() / 3;
    const int right = grid.left() + (column + 1) * grid.width() / 3;
    const int top = grid.top() + row * grid.height() / 4;
    const int bottom = grid.top() + (row + 1) * grid.height() / 4;
    return QRect(left + 4, top + 4, right - left - 8, bottom - top - 8);
}

QDate CustomCalendarWidget::firstVisibleDate() const
{
    const int offset = (m_pageDate.dayOfWeek() - static_cast<int>(m_firstDayOfWeek) + kGridColumns)
        % kGridColumns;
    return m_pageDate.addDays(-offset);
}

QDate CustomCalendarWidget::dateAt(const QPoint& position) const
{
    const QRect grid = gridRect();
    if (!grid.contains(position) || grid.width() <= 0 || grid.height() <= 0) {
        return QDate();
    }
    const int column = qBound(0, (position.x() - grid.left()) * kGridColumns / grid.width(), kGridColumns - 1);
    const int row = qBound(0, (position.y() - grid.top()) * kGridRows / grid.height(), kGridRows - 1);
    return firstVisibleDate().addDays(row * kGridColumns + column);
}

QList<QDate> CustomCalendarWidget::datesInRect(const QRect& rect) const
{
    QList<QDate> dates;
    const QDate firstDate = firstVisibleDate();
    for (int row = 0; row < kGridRows; ++row) {
        for (int column = 0; column < kGridColumns; ++column) {
            const QDate date = firstDate.addDays(row * kGridColumns + column);
            if (rect.intersects(cellRect(row, column)) && isCurrentPageDate(date)) {
                dates.append(date);
            }
        }
    }
    return dates;
}

void CustomCalendarWidget::selectDateFromClick(const QDate& date, Qt::KeyboardModifiers modifiers)
{
    if ((modifiers & Qt::ShiftModifier) && m_selectionAnchorDate.isValid()) {
        selectDateRange(date, m_selectionAnchorDate, modifiers & Qt::ControlModifier);
    } else if (modifiers & Qt::ControlModifier) {
        toggleDateSelection(date);
    } else {
        setSingleSelection(date);
    }
}

void CustomCalendarWidget::selectDateRange(const QDate& start, const QDate& end, bool additive)
{
    if (!start.isValid() || !end.isValid()) {
        return;
    }
    QList<QDate> dates = additive ? m_selectedDates : QList<QDate>();
    const QDate firstDate = start <= end ? start : end;
    const QDate lastDate = start <= end ? end : start;
    for (QDate date = firstDate; date <= lastDate; date = date.addDays(1)) {
        if (!dates.contains(date)) {
            dates.append(date);
        }
    }
    setSelectedDates(dates);
    m_selectionAnchorDate = end;
}

void CustomCalendarWidget::setSingleSelection(const QDate& date)
{
    if (m_selectedDates.size() == 1 && m_selectedDates.first() == date) {
        return;
    }
    m_selectedDates = { date };
    m_selectionAnchorDate = date;
    update();
    emit selectionChanged();
}

void CustomCalendarWidget::toggleDateSelection(const QDate& date)
{
    if (m_selectedDates.contains(date)) {
        m_selectedDates.removeAll(date);
    } else {
        m_selectedDates.append(date);
    }
    m_selectionAnchorDate = m_selectedDates.isEmpty() ? QDate() : m_selectedDates.last();
    update();
    emit selectionChanged();
}

void CustomCalendarWidget::finishRubberBandSelection()
{
    QList<QDate> dates = (m_dragModifiers & Qt::ControlModifier) ? m_selectedDates : QList<QDate>();
    for (const QDate& date : datesInRect(m_dragSelectionRect)) {
        if (!dates.contains(date)) {
            dates.append(date);
        }
    }
    setSelectedDates(dates);
}

void CustomCalendarWidget::showContextMenu(const QPoint& position, const QPoint& globalPosition)
{
    const QDate clickedDate = dateAt(position);
    if (!clickedDate.isValid()) {
        return;
    }

    const QList<QDate> targetDates = m_selectedDates.size() > 1 && m_selectedDates.contains(clickedDate)
        ? selectedDates()
        : QList<QDate>{ clickedDate };
    QList<QDate> deletableDates;
    for (const QDate& date : targetDates) {
        if (AttendanceStorage::hasArrivalRecord(date)) {
            deletableDates.append(date);
        }
    }
    if (deletableDates.isEmpty()) {
        return;
    }

    // This menu only exposes deletion for a multi-selection. Mark the records
    // that will actually be deleted, while keeping blank dates as normal selections.
    m_contextMenuDates = deletableDates;
    repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    CalendarContextMenu contextMenu;
    contextMenu.setMenuItemHeight(34);
    QAction* copyAction = nullptr;
    if (targetDates.size() == 1) {
        copyAction = contextMenu.addElaIconAction(ElaIconType::Clipboard, QStringLiteral("复制"));
        contextMenu.addSeparator();
    }
    QAction* deleteAction = contextMenu.addElaIconAction(ElaIconType::TrashCan,
        deletableDates.size() == 1
            ? QStringLiteral("删除 %1 的记录").arg(deletableDates.first().toString(QStringLiteral("yyyy-MM-dd")))
            : QStringLiteral("删除选中的 %1 条记录").arg(deletableDates.size()));

    // ElaMenu may retain an active action when dismissed outside its bounds.
    // Accept the returned action only when the final cursor position is inside it.
    QAction* selectedAction = contextMenu.exec(globalPosition + QPoint(0, 6));
    m_contextMenuDates.clear();
    update();
    if (!selectedAction
        || !contextMenu.actionGeometry(selectedAction).contains(contextMenu.mapFromGlobal(QCursor::pos()))) {
        return;
    }
    if (selectedAction == copyAction) {
        emit copyRequested(targetDates.first());
    } else if (selectedAction == deleteAction) {
        emit deleteRequested(deletableDates);
    }
}

bool CustomCalendarWidget::isDateSelected(const QDate& date) const
{
    return m_selectedDates.contains(date);
}

bool CustomCalendarWidget::isCurrentPageDate(const QDate& date) const
{
    return date.year() == m_pageDate.year() && date.month() == m_pageDate.month();
}

void CustomCalendarWidget::refreshYearRecordDates()
{
    m_yearRecordDates.clear();
    const QStringList dateKeys = AttendanceStorage::recordedDates();
    for (const QString& dateKey : dateKeys) {
        const QDate date = QDate::fromString(dateKey, QStringLiteral("yyyy-MM-dd"));
        if (date.isValid() && date.year() == m_pageDate.year()) {
            m_yearRecordDates.insert(date);
        }
    }
}

void CustomCalendarWidget::paintYearOverview(QPainter& painter)
{
    const QDate today = QDate::currentDate();
    const int currentYear = m_pageDate.year();
    for (int month = 1; month <= 12; ++month) {
        const QRect monthRect = monthPreviewRect(month);
        const bool isActiveMonth = month == m_pageDate.month();
        painter.save();
        painter.setPen(QPen(isActiveMonth ? QColor(QStringLiteral("#8bbce3"))
                                          : QColor(QStringLiteral("#dfe7ef"))));
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        painter.drawRoundedRect(monthRect, 6, 6);

        const QRect titleRect(monthRect.left(), monthRect.top(), monthRect.width(), 24);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isActiveMonth ? QColor(QStringLiteral("#eaf4fd"))
                                       : QColor(QStringLiteral("#f7f9fc")));
        painter.drawRoundedRect(titleRect.adjusted(1, 1, -1, 0), 5, 5);
        painter.setPen(isActiveMonth ? QColor(QStringLiteral("#1769aa"))
                                     : QColor(QStringLiteral("#334e68")));
        QFont titleFont = painter.font();
        titleFont.setPointSize(9);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(titleRect, Qt::AlignCenter, QStringLiteral("%1 月").arg(month));

        const QRect miniGrid = monthRect.adjusted(6, 27, -6, -6);
        const QDate monthStart(currentYear, month, 1);
        const int offset = (monthStart.dayOfWeek() - static_cast<int>(m_firstDayOfWeek) + kGridColumns)
            % kGridColumns;
        const QDate firstDate = monthStart.addDays(-offset);
        const int miniFontSize = qBound(6, miniGrid.height() / 16, 8);
        QFont dayFont = painter.font();
        dayFont.setPointSize(miniFontSize);
        painter.setFont(dayFont);

        for (int row = 0; row < kGridRows; ++row) {
            for (int column = 0; column < kGridColumns; ++column) {
                const int left = miniGrid.left() + column * miniGrid.width() / kGridColumns;
                const int right = miniGrid.left() + (column + 1) * miniGrid.width() / kGridColumns;
                const int top = miniGrid.top() + row * miniGrid.height() / kGridRows;
                const int bottom = miniGrid.top() + (row + 1) * miniGrid.height() / kGridRows;
                const QRect dayRect(left, top, right - left, bottom - top);
                const QDate date = firstDate.addDays(row * kGridColumns + column);
                if (date.month() != month) {
                    continue;
                }

                if (m_yearRecordDates.contains(date)) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(QStringLiteral("#d8f4df")));
                    painter.drawRoundedRect(dayRect.adjusted(1, 1, -1, -1), 2, 2);
                }
                if (date == today) {
                    painter.setPen(QPen(QColor(QStringLiteral("#4d97cf")), 1));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRoundedRect(dayRect.adjusted(1, 1, -1, -1), 2, 2);
                }

                const bool isWeekend = date.dayOfWeek() == Qt::Saturday || date.dayOfWeek() == Qt::Sunday;
                painter.setPen(isWeekend ? QColor(QStringLiteral("#d64d4d"))
                                         : QColor(QStringLiteral("#52657a")));
                painter.drawText(dayRect, Qt::AlignCenter, QString::number(date.day()));
            }
        }
        painter.restore();
    }
}

void CustomCalendarWidget::updateHoveredDate(const QPoint& position)
{
    int headerControl = 0;
    if (previousMonthButtonRect().contains(position)) {
        headerControl = 1;
    } else if (nextMonthButtonRect().contains(position)) {
        headerControl = 2;
    } else if (yearOverviewButtonRect().contains(position)) {
        headerControl = 3;
    }
    if (headerControl != 0) {
        const bool changed = m_hoveredHeaderControl != headerControl || m_hoveredDate.isValid();
        m_hoveredHeaderControl = headerControl;
        m_hoveredDate = QDate();
        setCursor(Qt::PointingHandCursor);
        m_noteTip->hideTip();
        if (changed) {
            update();
        }
        return;
    }
    if (m_hoveredHeaderControl != 0) {
        m_hoveredHeaderControl = 0;
        update();
    }

    if (m_yearOverviewVisible) {
        bool overMonth = false;
        for (int month = 1; month <= 12; ++month) {
            if (monthPreviewRect(month).contains(position)) {
                overMonth = true;
                break;
            }
        }
        setCursor(overMonth ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_noteTip->hideTip();
        return;
    }

    const QDate hoveredDate = dateAt(position);
    if (hoveredDate == m_hoveredDate) {
        return;
    }
    m_hoveredDate = hoveredDate;
    setCursor(hoveredDate.isValid() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    refreshHoveredNoteTip();
    update();
}

void CustomCalendarWidget::refreshHoveredNoteTip()
{
    if (m_yearOverviewVisible || !m_hoveredDate.isValid()) {
        m_noteTip->hideTip();
        return;
    }

    const QString note = m_data.value(m_hoveredDate).value(QStringLiteral("note")).toString().trimmed();
    const int dayOffset = firstVisibleDate().daysTo(m_hoveredDate);
    if (note.isEmpty() || dayOffset < 0 || dayOffset >= kGridRows * kGridColumns) {
        m_noteTip->hideTip();
        return;
    }

    m_noteTip->showForCell(this, cellRect(dayOffset / kGridColumns, dayOffset % kGridColumns), note);
}
