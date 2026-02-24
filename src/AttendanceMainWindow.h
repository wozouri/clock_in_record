#ifndef ATTENDANCEMAINWINDOW_H
#define ATTENDANCEMAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QDate>
#include <QMouseEvent>

class CustomCalendarWidget;
struct MonthlyAttendanceSnapshot;

// 主窗口
class AttendanceMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AttendanceMainWindow(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onDateClicked(const QDate& date);
    void onMonthChanged();
    void onDeleteRequested(const QDate& date);

    void onImportJsonClicked();
    void onExportJsonClicked();

private:
    void setupUI();
    void refreshMonthlyView();

    void deleteAttendanceRecord(const QDate& date);
    void updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot);
    void updateMonthlyStatistics(const MonthlyAttendanceSnapshot& snapshot);

    void processImportFile(const QString& filePath);
    void processExportFile(const QString& filePath);

    CustomCalendarWidget* m_calendar;
    QLabel* m_statsLabel;
};

#endif // ATTENDANCEMAINWINDOW_H
