#ifndef ATTENDANCEMAINWINDOW_H
#define ATTENDANCEMAINWINDOW_H

#include "Types/AttendanceTypes.h"
#include <QMainWindow>
#include <QLabel>
#include <QDate>
#include <QList>
#include <QMouseEvent>

class CustomCalendarWidget;
struct MonthlyAttendanceSnapshot;
class QPushButton;

// 主窗口
class AttendanceMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AttendanceMainWindow(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onDateDoubleClicked(const QDate& date);
    void onMonthChanged();
    void onDeleteRequested(const QList<QDate>& dates);
    void onDeleteSelectionRequested();

    void onImportJsonClicked();
    void onExportJsonClicked();
    void onSelectionChanged();
    void onCopySelectedClicked();
    void onApplyCopiedClicked();

private:
    void setupUI();
    void refreshMonthlyView();
    void updateBatchActionState();
    void showStatusMessage(const QString& message, int timeoutMs = 3000);

    void deleteAttendanceRecord(const QDate& date);
    void deleteAttendanceRecords(const QList<QDate>& dates);
    void updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot);
    void updateMonthlyStatistics(const MonthlyAttendanceSnapshot& snapshot);

    void processImportFile(const QString& filePath);
    void processExportFile(const QString& filePath);

    CustomCalendarWidget* m_calendar;
    QLabel* m_statsLabel;
    QLabel* m_selectionLabel;
    QLabel* m_copyStatusLabel;
    QPushButton* m_copySelectedButton;
    QPushButton* m_applyCopiedButton;
    AttendanceRecord m_copiedRecord;
    QDate m_copiedFromDate;
    bool m_hasCopiedRecord = false;
};

#endif // ATTENDANCEMAINWINDOW_H
