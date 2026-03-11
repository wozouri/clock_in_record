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
class QAction;
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
    void onSelectAllCurrentMonthRequested();

private:
    struct AttendanceRecordState {
        bool exists = false;
        AttendanceRecord record;
    };

    struct AttendanceChange {
        QDate date;
        AttendanceRecordState before;
        AttendanceRecordState after;
    };

    struct AttendanceHistoryEntry {
        QString actionText;
        QList<AttendanceChange> changes;
    };

    void setupUI();
    void refreshMonthlyView();
    void updateBatchActionState();
    void showStatusMessage(const QString& message, int timeoutMs = 3000);
    AttendanceRecordState captureRecordState(const QDate& date) const;
    void applyRecordState(const QDate& date, const AttendanceRecordState& state);
    void pushHistoryEntry(const QString& actionText, const QList<AttendanceChange>& changes);
    bool applyHistoryEntry(const AttendanceHistoryEntry& entry, bool useAfterState);
    void updateUndoRedoActionState();

    void deleteAttendanceRecord(const QDate& date);
    void deleteAttendanceRecords(const QList<QDate>& dates);
    void updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot);
    void updateMonthlyStatistics(const MonthlyAttendanceSnapshot& snapshot);

    void processImportFile(const QString& filePath);
    void processExportFile(const QString& filePath);

    CustomCalendarWidget* m_calendar = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QLabel* m_copyStatusLabel = nullptr;
    QPushButton* m_copySelectedButton = nullptr;
    QPushButton* m_applyCopiedButton = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    AttendanceRecord m_copiedRecord;
    QDate m_copiedFromDate;
    bool m_hasCopiedRecord = false;
    QList<AttendanceHistoryEntry> m_undoStack;
    QList<AttendanceHistoryEntry> m_redoStack;
};

#endif // ATTENDANCEMAINWINDOW_H
