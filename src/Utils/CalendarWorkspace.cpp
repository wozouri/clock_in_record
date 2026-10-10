#include "CalendarWorkspace.h"
#include "CustomCalendarWidget.h"

#include <QResizeEvent>

CalendarWorkspace::CalendarWorkspace(CustomCalendarWidget* calendar, QWidget* parent)
    : QWidget(parent), m_calendar(calendar)
{
    setObjectName(QStringLiteral("calendarWorkspace"));
    setMinimumSize(680, calendar->minimumHeight() + 24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_calendar->setParent(this);
    updateCalendarGeometry();
}

void CalendarWorkspace::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCalendarGeometry();
}

void CalendarWorkspace::updateCalendarGeometry()
{
    const int horizontalMargin = qBound(16, (width() - 960) / 6, 32);
    const int verticalMargin = qBound(12, (height() - 560) / 6, 24);
    const int calendarWidth = qMin(1400, qMax(0, width() - horizontalMargin * 2));
    // Limit both large screens and tall, narrow windows; keep cells in a useful
    // proportion instead of stretching them to the entire workspace height.
    const int heightLimit = qMin(800, qMax(m_calendar->minimumHeight(), qRound(calendarWidth * 0.64)));
    const int calendarHeight = qMin(heightLimit, qMax(0, height() - verticalMargin * 2));
    m_calendar->setGeometry((width() - calendarWidth) / 2, (height() - calendarHeight) / 2,
        calendarWidth, calendarHeight);
}
