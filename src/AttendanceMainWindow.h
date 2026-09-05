#ifndef ATTENDANCEMAINWINDOW_H
#define ATTENDANCEMAINWINDOW_H

#include "Types/AttendanceTypes.h"
#include <ElaWindow.h>
#include <QLabel>
#include <QDate>
#include <QList>
#include <QMouseEvent>

class CustomCalendarWidget;
class WorkScheduleSettingsPage;
struct MonthlyAttendanceSnapshot;
class QAction;
class QPushButton;
class ElaTeachingTip;

// 主窗口
class AttendanceMainWindow : public ElaWindow {
    Q_OBJECT

public:
    explicit AttendanceMainWindow(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    Q_TAKEOVER_NATIVEEVENT_H

private slots:
    void onDateDoubleClicked(const QDate& date);
    void onMonthChanged();
    void onDeleteRequested(const QList<QDate>& dates);
    void onDeleteSelectionRequested();

    void onImportJsonClicked();
    void onExportJsonClicked();
    void onWorkScheduleChanged(const WorkSchedule& schedule);
    void onSelectionChanged();
    void onCopySelectedClicked();
    void onCopyRequested(const QDate& date);
    void onApplyCopiedClicked();
    void onSelectAllCurrentMonthRequested();
    void onShowCurrentMonthRequested();

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
    void updateContextTips(const QList<QDate>& dates);
    void refreshContextTipPositions();
    void showStatusMessage(const QString& message, int timeoutMs = 2000);
    AttendanceRecordState captureRecordState(const QDate& date) const;
    void applyRecordState(const QDate& date, const AttendanceRecordState& state);
    void pushHistoryEntry(const QString& actionText, const QList<AttendanceChange>& changes);
    bool applyHistoryEntry(const AttendanceHistoryEntry& entry, bool useAfterState);
    void updateUndoRedoActionState();

    void deleteAttendanceRecord(const QDate& date);
    void deleteAttendanceRecords(const QList<QDate>& dates);
    void updateCalendarAppearance(const MonthlyAttendanceSnapshot& snapshot);
    void updateMonthlyStatistics(const MonthlyAttendanceSnapshot& snapshot);
    void copyRecord(const QDate& sourceDate);

    void processImportFile(const QString& filePath);
    void processExportFile(const QString& filePath);

    CustomCalendarWidget* m_calendar = nullptr;
    WorkScheduleSettingsPage* m_workScheduleSettingsPage = nullptr;
    QLabel* m_statsLabel = nullptr;
    QPushButton* m_copySelectedButton = nullptr;
    QPushButton* m_applyCopiedButton = nullptr;
    QPushButton* m_deleteSelectedButton = nullptr;
    QPushButton* m_selectMonthButton = nullptr;
    QPushButton* m_showCurrentMonthButton = nullptr;
    ElaTeachingTip* m_copyContextTip = nullptr;
    ElaTeachingTip* m_applyContextTip = nullptr;
    ElaTeachingTip* m_statsContextTip = nullptr;
    bool m_contextTipRefreshPending = false;
    QString m_monthlyStatsText;
    QList<QDate> m_renderedCalendarDates;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    AttendanceRecord m_copiedRecord;
    QDate m_copiedFromDate;
    bool m_hasCopiedRecord = false;
    QList<AttendanceHistoryEntry> m_undoStack;
    QList<AttendanceHistoryEntry> m_redoStack;
};

#endif // ATTENDANCEMAINWINDOW_H
