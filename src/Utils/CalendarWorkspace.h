#pragma once

#include <QWidget>

class CustomCalendarWidget;

class CalendarWorkspace final : public QWidget {
public:
    explicit CalendarWorkspace(CustomCalendarWidget* calendar, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateCalendarGeometry();
    CustomCalendarWidget* m_calendar;
};
