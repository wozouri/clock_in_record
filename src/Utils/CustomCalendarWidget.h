#ifndef CUSTOMCALENDARWIDGET_H
#define CUSTOMCALENDARWIDGET_H

#include <QColor>
#include <QDate>
#include <QList>
#include <QMap>
#include <QSet>
#include <QTextCharFormat>
#include <QVariantMap>
#include <QWidget>

class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

// A purpose-built attendance month view. It owns the grid layout, rendering and
// selection model so attendance states are not constrained by QCalendarWidget.
class CustomCalendarWidget : public QWidget {
    Q_OBJECT

public:
    explicit CustomCalendarWidget(QWidget* parent = nullptr);

    void setFirstDayOfWeek(Qt::DayOfWeek dayOfWeek);
    void setGridVisible(bool visible);

    int yearShown() const;
    int monthShown() const;
    bool isYearOverviewVisible() const;
    void setCurrentPage(int year, int month);
    void setYearOverviewVisible(bool visible);

    void setDateTextFormat(const QDate& date, const QTextCharFormat& format);
    void setCustomData(const QDate& date, const QVariantMap& value);
    void clearCustomData(const QDate& date);

    QList<QDate> selectedDates() const;
    void setSelectedDates(const QList<QDate>& dates);
    void clearSelection();

signals:
    void selectionChanged();
    void currentPageChanged(int year, int month);
    void yearOverviewVisibilityChanged(bool visible);
    void dateDoubleClicked(const QDate& date);
    void copyRequested(const QDate& date);
    void deleteRequested(const QList<QDate>& dates);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect previousMonthButtonRect() const;
    QRect nextMonthButtonRect() const;
    QRect yearOverviewButtonRect() const;
    QRect weekHeaderRect() const;
    QRect gridRect() const;
    QRect cellRect(int row, int column) const;
    QRect yearGridRect() const;
    QRect monthPreviewRect(int month) const;
    QDate firstVisibleDate() const;
    QDate dateAt(const QPoint& position) const;
    QList<QDate> datesInRect(const QRect& rect) const;

    void selectDateFromClick(const QDate& date, Qt::KeyboardModifiers modifiers);
    void selectDateRange(const QDate& start, const QDate& end, bool additive);
    void setSingleSelection(const QDate& date);
    void toggleDateSelection(const QDate& date);
    void finishRubberBandSelection();
    void showContextMenu(const QPoint& position, const QPoint& globalPosition);
    bool isDateSelected(const QDate& date) const;
    bool isCurrentPageDate(const QDate& date) const;
    void updateHoveredDate(const QPoint& position);
    void refreshYearRecordDates();
    void paintYearOverview(QPainter& painter);

    QMap<QDate, QVariantMap> m_data;
    QMap<QDate, QColor> m_dayBackgrounds;
    QSet<QDate> m_yearRecordDates;
    QList<QDate> m_selectedDates;
    QDate m_selectionAnchorDate;
    QDate m_pageDate;
    QDate m_hoveredDate;
    QList<QDate> m_contextMenuDates;
    QDate m_dragStartDate;
    QPoint m_dragStartPosition;
    QRect m_dragSelectionRect;
    Qt::KeyboardModifiers m_dragModifiers = Qt::NoModifier;
    Qt::DayOfWeek m_firstDayOfWeek = Qt::Monday;
    int m_hoveredHeaderControl = 0;
    bool m_gridVisible = true;
    bool m_yearOverviewVisible = false;
    bool m_dragSelectionActive = false;
};

#endif // CUSTOMCALENDARWIDGET_H
