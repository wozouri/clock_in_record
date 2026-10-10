#include "AttendanceMainWindow.h"
#include "Data/AttendanceStorage.h"
#include "Data/AttendanceStatsService.h"
#include "Data/WorkScheduleCodec.h"
#include "Utils/TimeSettingDialog.h"
#include "Utils/CustomCalendarWidget.h"
#include <ElaApplication.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaToggleSwitch.h>
#include <QFile>
#include <QAbstractButton>
#include <QMessageBox>
#include <QImage>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QScreen>
#include <QLabel>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeEdit>
#include <QTimer>
#include "Utils/AppFont.h"
#include <QFontInfo>
#include <QFontMetrics>
#include "Utils/ScreenLayout.h"
#include <QWindow>
#include <ElaIconButton.h>
#ifdef Q_OS_WIN
#include <Windows.h>
#endif

class LegacyUiTests : public QObject {
    Q_OBJECT
    AttendanceMainWindow* m_window = nullptr;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("AttendanceRegressionTests");
        QCoreApplication::setApplicationName("LegacyUiTests");
        QStandardPaths::setTestModeEnabled(true);
        UpdateChecker::saveUpdateServiceEndpoint("127.0.0.1", 9);
        auto db = QSqlDatabase::addDatabase("QSQLITE", "attendance-storage");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        QString error;
        QVERIFY2(AttendanceStorage::initialize(error), qPrintable(error));
        eApp->init();
        initializeAppFont();
        m_window = new AttendanceMainWindow;
        m_window->show();
        QTest::qWait(100);
    }
    void bundledFontSupportsCalendarMarkers() {
        const QFont font = QApplication::font();
        QVERIFY(QFontInfo(font).family().contains(QStringLiteral("HarmonyOS")));
        const QFontMetrics metrics(font);
        QVERIFY(metrics.inFont(QChar(0x9910))); // 餐
        QVERIFY(metrics.inFont(QChar(0x4f5c))); // 作
        QFont bold = font;
        bold.setBold(true);
        QVERIFY(QFontInfo(bold).bold());
        QFont marker = font;
        marker.setPixelSize(11);
        const QFontMetrics markerMetrics(marker);
        for (const auto& label : {QStringLiteral("餐"), QStringLiteral("作")}) {
            const QRect ink = markerMetrics.tightBoundingRect(label);
            QVERIFY(ink.width() <= 16 && ink.height() <= 14);
        }
    }
    void init() {
        QSqlQuery query(QSqlDatabase::database("attendance-storage"));
        QVERIFY(query.exec("DELETE FROM records"));
        AttendanceStorage::saveWorkSchedule(WorkSchedule{});
        m_window->m_undoStack.clear();
        m_window->m_redoStack.clear();
    }
    void geometryFitsNonPrimaryWorkAreas_data() {
        QTest::addColumn<QRect>("available");
        QTest::addColumn<QRect>("desired");
        QTest::newRow("left-monitor") << QRect(-1920,0,1920,1040) << QRect(-2100,900,1182,800);
        QTest::newRow("above-monitor") << QRect(0,-1440,2560,1400) << QRect(2200,-1600,1182,800);
        QTest::newRow("scaled-monitor-offset") << QRect(2560,180,1280,680) << QRect(3000,400,1182,800);
        QTest::newRow("disconnected-monitor") << QRect(0,0,1920,1040) << QRect(-1600,50,1182,800);
        QTest::newRow("fits-already") << QRect(-1920,0,1920,1040) << QRect(-1700,50,1182,800);
    }
    void geometryFitsNonPrimaryWorkAreas() {
        QFETCH(QRect, available);
        QFETCH(QRect, desired);
        const QRect result = ScreenLayout::fittedGeometry(desired,available,12);
        QVERIFY(available.adjusted(12,12,-12,-12).contains(result));
        QCOMPARE(result.size(),desired.size().boundedTo(available.size()-QSize(24,24)));
        if (available.adjusted(12,12,-12,-12).contains(desired)) QCOMPARE(result,desired);
        const QRect anchor(available.right()-24,available.bottom()-28,20,20);
        const QRect popup = ScreenLayout::popupGeometry(anchor,QSize(164,128),available);
        QVERIFY(available.contains(popup));
        QVERIFY(popup.bottom() < anchor.top());
    }
    void windowsFollowActualMonitors() {
        auto& window = *m_window;
        window.showNormal();
        const QRect original = window.geometry();
        auto* originalScreen = window.screen();
        TimeSettingDialog dialog(QDate(2026,10,10),WorkSchedule{},&window);
        auto* editor = dialog.findChild<QTimeEdit*>("arrivalTimeEditor");
        editor->setTime(QTime(8,47));
        int index = 0;
        for (auto* screen : QGuiApplication::screens()) {
            qInfo() << "Monitor" << screen->name() << screen->geometry()
                    << "work area" << screen->availableGeometry() << "DPR" << screen->devicePixelRatio();
            const QRect available = screen->availableGeometry();
            window.showNormal();
            window.windowHandle()->setScreen(screen);
            QTest::qWait(120);
            window.setGeometry(QRect(available.topLeft()+QPoint(12,12),QSize(1182,800)));
            QTest::qWait(250);
            QTRY_COMPARE(window.screen(),screen);
            QTRY_VERIFY(available.contains(window.geometry()));
            QCOMPARE(window.devicePixelRatioF(),screen->devicePixelRatio());
            QVERIFY(window.minimumHeight() <= available.height());
            window.showMaximized();
            QTest::qWait(200);
            QCOMPARE(window.screen(),screen);
#ifdef Q_OS_WIN
            const auto handle = reinterpret_cast<HWND>(window.winId());
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&info));
            RECT actual{};
            QVERIFY(GetClientRect(handle,&actual));
            POINT origin{};
            QVERIFY(ClientToScreen(handle,&origin));
            OffsetRect(&actual,origin.x,origin.y);
            qInfo() << "Maximized native geometry" << actual.left << actual.top << actual.right << actual.bottom
                    << "work area" << info.rcWork.left << info.rcWork.top << info.rcWork.right << info.rcWork.bottom
                    << "Qt" << window.geometry() << "DPR" << window.devicePixelRatioF();
            // Frameless Ela windows must use this monitor's taskbar-free work area.
            QCOMPARE(actual.left,info.rcWork.left);
            QCOMPARE(actual.top,info.rcWork.top);
            QCOMPARE(actual.right,info.rcWork.right);
            QCOMPARE(actual.bottom,info.rcWork.bottom);
#endif
            QTRY_COMPARE(window.geometry(),available);
            if (!dialog.isVisible()) dialog.show();
            else {
                dialog.windowHandle()->setScreen(screen);
                QTest::qWait(120);
                dialog.move(available.topLeft()+QPoint(24,24));
            }
            QTest::qWait(200);
            QTRY_COMPARE(dialog.screen(),screen);
            qInfo() << "Settled editor" << dialog.geometry() << "bounds" << available.adjusted(12,12,-12,-12);
            QTRY_VERIFY(available.adjusted(12,12,-12,-12).contains(dialog.geometry()));
            QCOMPARE(editor->time(),QTime(8,47));
            dialog.m_customScheduleToggle->setIsToggled(true);
            QTest::qWait(220);
            QVERIFY(available.adjusted(12,12,-12,-12).contains(dialog.geometry()));
            QTest::mouseClick(dialog.m_noteEmojiButton,Qt::LeftButton);
            QTest::qWait(100);
            bool pickerFound = false;
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() != "noteEmojiPicker" || !widget->isVisible()) continue;
                pickerFound = true;
                QCOMPARE(widget->screen(),screen);
                QVERIFY(available.contains(widget->geometry()));
                widget->close();
            }
            QVERIFY(pickerFound);
            const auto capture = qEnvironmentVariable("ATTENDANCE_MULTISCREEN_CAPTURE");
            if (!capture.isEmpty()) {
                window.grab().save(capture+QString("-monitor%1-main.png").arg(index));
                dialog.grab().save(capture+QString("-monitor%1-dialog.png").arg(index));
            }
            dialog.m_customScheduleToggle->setIsToggled(false);
            QTest::qWait(220);
            QVERIFY(available.adjusted(12,12,-12,-12).contains(dialog.geometry()));
            ++index;
        }
        dialog.close();
        window.showNormal();
        window.windowHandle()->setScreen(originalScreen);
        window.setGeometry(original);
        QTest::qWait(150);
    }
    void movingBetweenMonitorsPreservesLogicalSize() {
        auto& window = *m_window;
        window.showNormal();
        const QRect original = window.geometry();
        window.resize(1182,800);
        TimeSettingDialog dialog(QDate(2026,10,10),WorkSchedule{},&window);
        dialog.show();
        QTest::qWait(150);
        for (auto* screen : QGuiApplication::screens()) {
            const QRect available = screen->availableGeometry();
            window.move(available.topLeft()+QPoint(24,24));
            QTest::qWait(250);
            QTRY_COMPARE(window.screen(),screen);
            QTRY_COMPARE(window.size(),QSize(1182,800).boundedTo(available.size()-QSize(16,16)));
            QVERIFY(available.contains(window.geometry()));
            dialog.move(available.topLeft()+QPoint(48,48));
            QTest::qWait(200);
            QTRY_COMPARE(dialog.screen(),screen);
            QCOMPARE(dialog.width(),520);
            QTRY_COMPARE(dialog.size(),dialog.contentGeometry(available).size());
            QVERIFY(available.adjusted(12,12,-12,-12).contains(dialog.geometry()));
        }
        dialog.close();
        window.setGeometry(original);
        QTest::qWait(150);
    }
    void allThreeTargetsAppearInMonthlyTip() {
        auto& window = *m_window;
        MonthlyAttendanceSnapshot snapshot;
        snapshot.workDays = 18;
        snapshot.totalOvertimeMinutes = 2665;
        window.updateMonthlyStatistics(snapshot);
        QVERIFY(window.m_monthlyStatsText.contains(QStringLiteral("距离日均2.0小时: 已超出 8小时25分钟")));
        QVERIFY(window.m_monthlyStatsText.contains(QStringLiteral("距离日均2.5小时: 还差 35分钟")));
        QVERIFY(window.m_monthlyStatsText.contains(QStringLiteral("距离日均3.0小时: 还差 9小时35分钟")));
    }
    void dialogLoadsAndEditsLegacyFields() {
        const QDate date(2026,10,3);
        AttendanceRecord record;
        record.needAverageCal = false;
        record.excludeStandardOvertime = false;
        record.hasCustomSchedule = true;
        record.customSchedule.workStartTime = QTime(8,30);
        record.customSchedule.workEndTime = QTime(17,30);
        AttendanceStorage::saveRecord(date, record);
        TimeSettingDialog dialog(date, WorkSchedule{});
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        auto* custom = dialog.findChild<ElaToggleSwitch*>("customScheduleToggle");
        auto* exclusion = dialog.findChild<ElaToggleSwitch*>("excludeStandardOvertimeToggle");
        auto* workday = dialog.findChild<ElaToggleSwitch*>("includeWorkdayToggle");
        QVERIFY(custom && exclusion && workday);
        QVERIFY(custom->getIsToggled());
        QVERIFY(!exclusion->getIsToggled());
        QVERIFY(exclusion->isEnabled());
        QVERIFY(dialog.findChild<ElaPushButton*>("deleteRecordButton")->isVisible());
        auto* start = dialog.findChild<QTimeEdit*>("customScheduleTime0");
        QCOMPARE(start->time(), QTime(8,30));
        start->setTime(QTime(8,45));
        QCOMPARE(dialog.getRecord().customSchedule.workStartTime, QTime(8,45));
        workday->setIsToggled(true);
        QVERIFY(!exclusion->isEnabled());
        const auto capture = qEnvironmentVariable("ATTENDANCE_DIALOG_CAPTURE");
        if (!capture.isEmpty()) dialog.grab().save(capture);
    }
    void editorInitiallyCentersOnParent_data() {
        QTest::addColumn<int>("parentPosition");
        QTest::addColumn<bool>("expanded");
        for (int position = 0; position < 3; ++position) {
            for (bool expanded : {false,true}) {
                QTest::newRow(qPrintable(QString("position%1-expanded%2").arg(position).arg(expanded)))
                    << position << expanded;
            }
        }
    }
    void editorInitiallyCentersOnParent() {
        QFETCH(int, parentPosition);
        QFETCH(bool, expanded);
        auto& window = *m_window;
        window.showNormal();
        window.resize(1182,800);
        const QRect originalGeometry = window.geometry();
        const auto available = window.screen()->availableGeometry();
        window.move(available.topLeft() + QPoint(100,100));
        if (parentPosition == 1) window.showMaximized();
        if (parentPosition == 2) window.move(available.bottomRight() - QPoint(280,180));
        QTest::qWait(250);
        const QDate date(2026,10,10);
        AttendanceRecord record;
        record.hasCustomSchedule = expanded;
        AttendanceStorage::saveRecord(date,record);
        TimeSettingDialog dialog(date,WorkSchedule{},&window);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        // A stale position before show must not override centering on the owner.
        dialog.move(available.topLeft());
        dialog.show();
        QTest::qWait(150);
        const auto* screen = ScreenLayout::screenForWidget(&window);
        const QRect bounds = screen->availableGeometry().adjusted(12,12,-12,-12);
        QRect expected(QPoint(0,0),dialog.size());
        expected.moveCenter(window.frameGeometry().center());
        expected.moveLeft(qBound(bounds.left(),expected.left(),bounds.right()-expected.width()+1));
        expected.moveTop(qBound(bounds.top(),expected.top(),bounds.bottom()-expected.height()+1));
        QTRY_COMPARE(dialog.size(),expected.size());
        QTRY_VERIFY((dialog.pos()-expected.topLeft()).manhattanLength() <= 2);
        QVERIFY(bounds.contains(dialog.geometry()));
        const auto capture = qEnvironmentVariable("ATTENDANCE_EDITOR_POSITION_CAPTURE");
        if (!capture.isEmpty()) dialog.grab().save(capture + "-" + QTest::currentDataTag() + ".png");
        const QPoint anchor = dialog.pos();
        dialog.m_customScheduleToggle->setIsToggled(!expanded);
        QTest::qWait(20);
        QTRY_COMPARE(dialog.m_resizeAnimation->state(),QAbstractAnimation::Stopped);
        QVERIFY(bounds.contains(dialog.geometry()));
        if (anchor.y() + dialog.height() <= bounds.bottom() + 1) QCOMPARE(dialog.pos(),anchor);
        dialog.close();
        window.showNormal();
        window.setGeometry(originalGeometry);
    }
    void unparentedEditorCentersOnScreen() {
        TimeSettingDialog dialog(QDate(2026,10,10),WorkSchedule{});
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        QTest::qWait(30);
        const auto available = dialog.screen()->availableGeometry();
        QVERIFY(qAbs(dialog.geometry().center().x() - available.center().x()) <= 1);
        QVERIFY(qAbs(dialog.geometry().center().y() - available.center().y()) <= 1);
    }
    void calendarEditOpensModalCenteredOnMainWindow() {
        auto& window = *m_window;
        const QDate date(2026,10,10);
        AttendanceRecord record;
        record.arrivalTime = QTime(8,49);
        record.departureTime = QTime(21,11);
        AttendanceStorage::saveRecord(date,record);
        bool opened = false;
        QRect actual;
        QRect expected;
        QTimer::singleShot(250,[&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<TimeSettingDialog*>(widget);
                if (!dialog || !dialog->isVisible()) continue;
                opened = true;
                actual = dialog->geometry();
                auto* screen = ScreenLayout::screenForWidget(&window);
                const QRect bounds = screen->availableGeometry().adjusted(12,12,-12,-12);
                expected = QRect(QPoint(0,0),dialog->size());
                expected.moveCenter(window.frameGeometry().center());
                expected.moveLeft(qBound(bounds.left(),expected.left(),bounds.right()-expected.width()+1));
                expected.moveTop(qBound(bounds.top(),expected.top(),bounds.bottom()-expected.height()+1));
                dialog->reject();
                break;
            }
        });
        window.onDateDoubleClicked(date);
        QVERIFY(opened);
        QCOMPARE(actual.size(),expected.size());
        qInfo() << "Modal geometry" << actual << "expected" << expected;
        // Native pixel rounding at fractional DPI can move a logical origin by 1px.
        QVERIFY((actual.topLeft()-expected.topLeft()).manhattanLength() <= 2);
    }
    void customScheduleResizesAndRetainsValues() {
        const QDate date(2026,10,10);
        AttendanceRecord record;
        record.arrivalTime = QTime(8,48);
        record.departureTime = QTime(21,11);
        AttendanceStorage::saveRecord(date, record);
        TimeSettingDialog dialog(date, WorkSchedule{});
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        QTest::qWait(30);
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        dialog.move(available.topLeft() + QPoint(30,20));
        const auto collapsedSize = dialog.size();
        const int collapsedContentHeight = dialog.m_content->height();
        const auto anchor = dialog.pos();
        const auto capture = qEnvironmentVariable("ATTENDANCE_DIALOG_LAYOUT_CAPTURE");
        if (!capture.isEmpty()) dialog.grab().save(capture + "-collapsed.png");
        QVERIFY(!dialog.m_customScheduleBody->isVisible());
        dialog.m_customScheduleToggle->setIsToggled(true);
        QTest::qWait(20);
        QTRY_COMPARE(dialog.m_resizeAnimation->state(), QAbstractAnimation::Stopped);
        QVERIFY(dialog.m_customScheduleBody->isVisible());
        QCOMPARE(dialog.width(), collapsedSize.width());
        QVERIFY(dialog.m_content->height() > collapsedContentHeight + 100);
        QCOMPARE(dialog.size(), dialog.contentGeometry(available).size());
        if (anchor.y() + dialog.height() <= available.bottom() - 12) QCOMPARE(dialog.pos(), anchor);
        if (!capture.isEmpty()) dialog.grab().save(capture + "-expanded.png");
        const auto expandedSize = dialog.size();
        dialog.m_scheduleTimes[0]->setTime(QTime(8,30));
        dialog.m_customLunchToggle->setIsToggled(false);
        QVERIFY(!dialog.m_scheduleTimes[2]->isEnabled());
        QVERIFY(!dialog.m_scheduleTimes[3]->isEnabled());
        dialog.m_customLunchToggle->setIsToggled(true);
        QVERIFY(dialog.m_scheduleTimes[2]->isEnabled());
        dialog.m_customScheduleToggle->setIsToggled(false);
        QTest::qWait(20);
        QTRY_COMPARE(dialog.m_resizeAnimation->state(), QAbstractAnimation::Stopped);
        QCOMPARE(dialog.size(), collapsedSize);
        QVERIFY(!dialog.getRecord().hasCustomSchedule);
        if (dialog.m_scrollArea->viewport()->height() >= collapsedContentHeight)
            QCOMPARE(dialog.m_scrollArea->verticalScrollBar()->maximum(), 0);
        // 快速反复开关也应收敛到同一个尺寸，不能累积留白或丢弃未保存输入。
        dialog.m_customScheduleToggle->setIsToggled(true);
        QTest::qWait(30);
        dialog.m_customScheduleToggle->setIsToggled(false);
        QTest::qWait(30);
        dialog.m_customScheduleToggle->setIsToggled(true);
        QTest::qWait(20);
        QTRY_COMPARE(dialog.m_resizeAnimation->state(), QAbstractAnimation::Stopped);
        QCOMPARE(dialog.size(), expandedSize);
        QCOMPARE(dialog.getRecord().customSchedule.workStartTime, QTime(8,30));
        const auto beforeTimeChange = dialog.size();
        dialog.m_arrivalTimeEdit->setTime(QTime(10,0));
        dialog.m_departureTimeEdit->setTime(QTime(17,0));
        QCOMPARE(dialog.size(), beforeTimeChange);
        // 模拟较矮的工作区：限制窗口高度，内容使用 Ela 滚动，底部按钮仍可访问。
        const QRect smallWorkArea(available.topLeft(), QSize(800,600));
        dialog.setGeometry(dialog.contentGeometry(smallWorkArea));
        QTest::qWait(30);
        QVERIFY(smallWorkArea.contains(dialog.geometry()));
        QVERIFY(dialog.m_scrollArea->verticalScrollBar()->maximum() > 0);
        QVERIFY(dialog.rect().contains(dialog.m_actions->geometry()));
        QCOMPARE(dialog.m_scrollArea->horizontalScrollBar()->maximum(), 0);
        if (!capture.isEmpty()) dialog.grab().save(capture + "-small-screen.png");
    }
    void deletingFromDialogCanBeUndoneWithAllFields() {
        const QDate date(2026,10,3);
        AttendanceRecord record;
        record.hasCustomSchedule = true;
        record.customSchedule.workStartTime = QTime(8,30);
        record.needAverageCal = false;
        record.excludeStandardOvertime = false;
        AttendanceStorage::saveRecord(date, record);
        auto& window = *m_window;
        window.show();
        QTest::qWait(50);
        QTimer::singleShot(150, [&] {
            TimeSettingDialog* dialog = nullptr;
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* candidate = qobject_cast<TimeSettingDialog*>(widget)) dialog = candidate;
            }
            QVERIFY(dialog);
            QTimer::singleShot(150, [] {
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (auto* box = qobject_cast<QMessageBox*>(widget)) box->button(QMessageBox::Yes)->click();
                }
            });
            dialog->findChild<ElaPushButton*>("deleteRecordButton")->click();
        });
        window.onDateDoubleClicked(date);
        QVERIFY(!AttendanceStorage::hasArrivalRecord(date));
        QCOMPARE(window.m_undoStack.size(), 1);
        QVERIFY(window.applyHistoryEntry(window.m_undoStack.last(), false));
        const auto restored = AttendanceStorage::loadRecord(date);
        QVERIFY(restored.hasCustomSchedule);
        QVERIFY(!restored.excludeStandardOvertime);
        QVERIFY(workSchedulesEqual(restored.customSchedule, record.customSchedule));
    }
    void importPreviewAndUndoRestoreRecordsAndGlobalSchedule() {
        QTemporaryDir directory;
        QFile file(directory.filePath("import.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[{\"date\":\"2026-10-3\",\"check_in\":\"08:30\",\"check_out\":\"19:00\",\"workStart\":\"08:30\",\"workEnd\":\"17:30\",\"needAverageCal\":false,\"excludeStandardOvertime\":false},{\"date\":\"2026-10-4\",\"check_in\":\"09:00\",\"check_out\":\"18:00\"}]");
        file.close();
        AttendanceRecord original;
        original.note = QStringLiteral("导入前已有记录");
        original.hasCustomSchedule = true;
        original.customSchedule.workStartTime = QTime(8,15);
        AttendanceStorage::saveRecord(QDate(2026,10,3), original);
        auto& window = *m_window;
        window.show();
        QTest::qWait(50);
        const auto before = AttendanceStorage::loadWorkSchedule();
        QString previewText;
        QTimer::singleShot(150, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                    previewText = box->text();
                    box->button(QMessageBox::No)->click();
                }
            }
        });
        window.processImportFile(file.fileName());
        QVERIFY(previewText.contains(QStringLiteral("共 2 条记录（2026-10-03 ~ 2026-10-04）")));
        QVERIFY(previewText.contains(QStringLiteral("其中 1 个日期已有记录")));
        QVERIFY(previewText.contains(QStringLiteral("工作制度设置")));
        QCOMPARE(AttendanceStorage::loadRecord(QDate(2026,10,3)).note, original.note);
        QVERIFY(!AttendanceStorage::hasArrivalRecord(QDate(2026,10,4)));
        QVERIFY(window.m_undoStack.isEmpty());
        QTimer::singleShot(150, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget)) box->button(QMessageBox::Yes)->click();
            }
        });
        window.processImportFile(file.fileName());
        QVERIFY(AttendanceStorage::hasArrivalRecord(QDate(2026,10,3)));
        QCOMPARE(window.m_undoStack.size(), 1);
        const auto history = window.m_undoStack.last();
        QVERIFY(history.hasScheduleChange);
        QCOMPARE(AttendanceStorage::loadWorkSchedule().workStartTime, QTime(8,30));
        QVERIFY(window.applyHistoryEntry(history, false));
        const auto undone = AttendanceStorage::loadRecord(QDate(2026,10,3));
        QCOMPARE(undone.note, original.note);
        QVERIFY(workSchedulesEqual(undone.customSchedule, original.customSchedule));
        QVERIFY(!AttendanceStorage::hasArrivalRecord(QDate(2026,10,4)));
        QVERIFY(workSchedulesEqual(AttendanceStorage::loadWorkSchedule(), before));
        QVERIFY(window.applyHistoryEntry(history, true));
        const auto restored = AttendanceStorage::loadRecord(QDate(2026,10,3));
        QVERIFY(!restored.needAverageCal && !restored.excludeStandardOvertime);
    }
    void keyboardEditingEmitsSelectedDate() {
        CustomCalendarWidget calendar;
        const QDate date(2026,10,3);
        calendar.setCurrentPage(2026,10);
        calendar.setSelectedDates({date});
        QSignalSpy spy(&calendar, &CustomCalendarWidget::dateDoubleClicked);
        QTest::keyClick(&calendar, Qt::Key_Return);
        QTest::keyClick(&calendar, Qt::Key_F2);
        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.first().first().toDate(), date);
        calendar.setYearOverviewVisible(true);
        QTest::keyClick(&calendar, Qt::Key_F2);
        QCOMPARE(spy.size(), 2);
    }
    void calendarFitsWindowSizes_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("minimum") << QSize(1120,720);
        QTest::newRow("default") << QSize(1182,800);
        QTest::newRow("wide") << QSize(1920,720);
        QTest::newRow("tall") << QSize(1182,1040);
        QTest::newRow("large") << QSize(1920,1040);
    }
    void calendarFitsWindowSizes() {
        QFETCH(QSize, windowSize);
        auto& window = *m_window;
        window.showNormal();
        window.resize(windowSize);
        auto* calendar = window.m_calendar;
        calendar->setCurrentPage(2026,10);
        auto schedule = AttendanceStorage::loadWorkSchedule();
        schedule.showMealAllowanceMarker = true;
        AttendanceStorage::saveWorkSchedule(schedule);
        for (int day = 8; day <= 31; ++day) {
            const QDate date(2026,10,day);
            if (date.dayOfWeek() == Qt::Sunday || date == QDate(2026,10,17) || date == QDate(2026,10,24)) continue;
            AttendanceRecord record;
            record.arrivalTime = QTime(8,48);
            record.departureTime = QTime(day % 7 == 0 ? 18 : 21,11);
            record.needAverageCal = date.dayOfWeek() != Qt::Saturday;
            record.hasCustomSchedule = day == 13;
            record.note = day == 9 ? QStringLiteral("测试备注") : QStringLiteral("");
            AttendanceStorage::saveRecord(date,record);
        }
        window.refreshMonthlyView();
        QTest::qWait(150);
        QWidget* workspace = calendar->parentWidget();
        QVERIFY(workspace->rect().contains(calendar->geometry()));
        QVERIFY(calendar->width() <= 1400 && calendar->height() <= 800);
        QVERIFY(qAbs(calendar->geometry().center().x() - workspace->rect().center().x()) <= 1);
        QVERIFY(qAbs(calendar->geometry().center().y() - workspace->rect().center().y()) <= 1);
        QVERIFY(calendar->cellRect(0,0).height() >= 74);
        auto* toolbar = window.findChild<QWidget*>("attendanceToolbar");
        QVERIFY(toolbar);
        for (auto* button : toolbar->findChildren<QPushButton*>()) {
            if (button->isVisible()) QVERIFY(toolbar->rect().contains(button->geometry()));
        }
        QVERIFY(window.m_statsLabel->width() >= window.m_statsLabel->fontMetrics()
            .horizontalAdvance(window.m_statsLabel->text()) + 26);
        const QDate selected(2026,10,9);
        const int index = calendar->firstVisibleDate().daysTo(selected);
        const QRect cell = calendar->cellRect(index / 7,index % 7);
        QCOMPARE(calendar->dateAt(cell.center()), selected);
        QTest::mouseClick(calendar,Qt::LeftButton,Qt::NoModifier,cell.center());
        QCOMPARE(calendar->selectedDates(), QList<QDate>{selected});
        // Resize must preserve hit testing for both the month and year views.
        calendar->setYearOverviewVisible(true);
        QTest::mouseClick(calendar,Qt::LeftButton,Qt::NoModifier,calendar->monthPreviewRect(10).center());
        QVERIFY(!calendar->isYearOverviewVisible());
        QCOMPARE(calendar->monthShown(),10);
        calendar->clearSelection();
        const auto capture = qEnvironmentVariable("ATTENDANCE_WINDOW_LAYOUT_CAPTURE");
        if (!capture.isEmpty()) window.grab().save(capture + "-" + QTest::currentDataTag() + ".png");
        calendar->setCurrentPage(2026,11);
        MonthlyAttendanceSnapshot snapshot;
        snapshot.workDays = 18;
        snapshot.totalOvertimeMinutes = 2665;
        snapshot.mealAllowanceCount = 15;
        window.updateMonthlyStatistics(snapshot);
        QTest::qWait(30);
        QVERIFY(window.m_showCurrentMonthButton->isVisible());
        for (auto* button : toolbar->findChildren<QPushButton*>()) {
            if (button->isVisible()) QVERIFY(toolbar->rect().contains(button->geometry()));
        }
        QVERIFY(window.m_statsLabel->width() >= window.m_statsLabel->fontMetrics()
            .horizontalAdvance(window.m_statsLabel->text()) + 26);
        calendar->setCurrentPage(2026,10);
        window.resize(1182,800);
    }
    void maximizedCalendarStaysWithinContentLimits() {
        auto& window = *m_window;
        window.showMaximized();
        QTRY_VERIFY(window.isMaximized());
        QTest::qWait(100);
        auto* calendar = window.m_calendar;
        QVERIFY(calendar->width() <= 1400 && calendar->height() <= 800);
        QVERIFY(calendar->parentWidget()->rect().contains(calendar->geometry()));
        const auto capture = qEnvironmentVariable("ATTENDANCE_WINDOW_LAYOUT_CAPTURE");
        if (!capture.isEmpty()) window.grab().save(capture + "-maximized.png");
        window.showNormal();
        window.resize(1182,800);
    }
    void todayDateHasSolidBlueCircle_data() {
        QTest::addColumn<QDate>("today");
        QTest::addColumn<int>("state");
        QTest::addColumn<QSize>("calendarSize");
        for (const int day : {1, 10, 31}) {
            for (int state = 0; state < 4; ++state) {
                for (const QSize size : {QSize(1008, 680), QSize(1008, 600), QSize(616, 440), QSize(1400,800)}) {
                    QTest::newRow(qPrintable(QString("day%1-state%2-height%3").arg(day).arg(state).arg(size.height())))
                        << QDate(2026,10,day) << state << size;
                }
            }
        }
    }
    void todayDateHasSolidBlueCircle() {
        QFETCH(QDate, today);
        QFETCH(int, state);
        QFETCH(QSize, calendarSize);
        CustomCalendarWidget calendar(nullptr, [today] { return today; });
        calendar.setAttribute(Qt::WA_DontShowOnScreen);
        calendar.resize(calendarSize);
        if (state > 0) {
            calendar.setCustomData(today, {{"arrivalTime", "09:00"}, {"departureTime", "21:00"},
                {"hasNote", true}, {"note", "note"}, {"hasCustomSchedule", true}, {"hasMealAllowance", true}});
            QTextCharFormat format;
            format.setBackground(QColor(144, 238, 144));
            calendar.setDateTextFormat(today, format);
        }
        if (state == 2) calendar.setSelectedDates({today});
        calendar.show();
        const int index = calendar.firstVisibleDate().daysTo(today);
        const QRect cell = calendar.cellRect(index / 7, index % 7);
        if (state == 3) {
            QMouseEvent hover(QEvent::MouseMove, cell.center(), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&calendar, &hover);
            QCOMPARE(calendar.m_hoveredDate, today);
        }
        const QImage image = calendar.grab().toImage();
        if (today.day() == 10 && state == 1) {
            const auto capture = qEnvironmentVariable("ATTENDANCE_CALENDAR_CAPTURE");
            if (!capture.isEmpty()) {
                const auto scale = image.devicePixelRatio();
                image.copy(QRect(qRound(cell.left() * scale), qRound(cell.top() * scale),
                    qRound(cell.width() * scale), qRound(cell.height() * scale))).save(
                        capture + QString("-h%1.png").arg(calendarSize.height()));
                image.save(capture + QString("-calendar-h%1.png").arg(calendarSize.height()));
            }
        }
        const auto bluePixels = [&image](const QRect& area) {
            int count = 0;
            const qreal scale = image.devicePixelRatio();
            for (int y = qRound(area.top() * scale); y < qRound((area.bottom() + 1) * scale); ++y) {
                for (int x = qRound(area.left() * scale); x < qRound((area.right() + 1) * scale); ++x) {
                    if (image.pixelColor(x,y).rgb() == QColor("#1769aa").rgb()) ++count;
                }
            }
            return count;
        };
        const QRect dateArea(cell.left() + 4, cell.top() + 4, 44, 44);
        QVERIFY2(bluePixels(dateArea) >= 100, "Today's date must have a filled blue circular background");
        const auto pixel = [&image](QPoint position) {
            const auto scale = image.devicePixelRatio();
            return image.pixelColor(qRound(position.x() * scale), qRound(position.y() * scale));
        };
        const auto colorBounds = [&pixel, &cell](const QColor& color, const QRect& area) {
            QRect bounds;
            for (int y = area.top(); y <= area.bottom(); ++y) {
                for (int x = area.left(); x <= area.right(); ++x) {
                    const QColor rendered = pixel(cell.topLeft() + QPoint(x,y));
                    if (rendered.rgb() == color.rgb()) {
                        bounds = bounds.united(QRect(x,y,1,1));
                    }
                }
            }
            return bounds;
        };
        const QRect circle = colorBounds(QColor("#1769aa"), QRect(4,4,44,44));
        QVERIFY2(circle.left() >= 10 && circle.top() >= 10, "Date circle needs breathing room inside the card border");
        // Fractional DPI sampling and white number strokes can hide a rim pixel.
        QVERIFY2(qAbs(circle.width() - circle.height()) <= 2,
            qPrintable(QString("Rendered circle bounds: %1 x %2").arg(circle.width()).arg(circle.height())));
        QVERIFY(circle.width() >= 18 && circle.width() <= 28);
        QCOMPARE(pixel(cell.topLeft() + QPoint(circle.center().x(), circle.top() + 1)).rgb(), QColor("#1769aa").rgb());
        QVERIFY(pixel(cell.topLeft() + circle.topLeft()).rgb() != QColor("#1769aa").rgb());
        int whiteTextPixels = 0;
        const qreal scale = image.devicePixelRatio();
        const QRect numberArea = circle.adjusted(3,3,-3,-3).translated(cell.topLeft());
        const QPointF center = cell.topLeft() + circle.center();
        const qreal radius = circle.width() / 2.0 - 2;
        // Sample physical pixels so fractional scaling cannot skip a thin stem.
        for (int y = qRound(numberArea.top() * scale); y < qRound((numberArea.bottom() + 1) * scale); ++y) {
            for (int x = qRound(numberArea.left() * scale); x < qRound((numberArea.right() + 1) * scale); ++x) {
                const qreal dx = (x + 0.5) / scale - center.x();
                const qreal dy = (y + 0.5) / scale - center.y();
                if (dx * dx + dy * dy > radius * radius) continue;
                const QColor ink = image.pixelColor(x,y);
                if (ink.red() >= 245 && ink.green() >= 245 && ink.blue() >= 245) ++whiteTextPixels;
            }
        }
        QVERIFY2(whiteTextPixels >= 3, "Today's number must remain white over its blue circle");
        if (state > 0) {
            const QRect footer(0, cell.height() / 2, cell.width(), cell.height() - cell.height() / 2);
            const QRect meal = colorBounds(QColor("#fff0dc"), footer);
            const QRect schedule = colorBounds(QColor("#eee8f7"), footer);
            const QRect note = colorBounds(QColor("#c98a30"), QRect(cell.width() / 2, 0,
                cell.width() - cell.width() / 2, cell.height() / 2));
            QVERIFY(!meal.isEmpty() && !schedule.isEmpty() && !note.isEmpty());
            int mealInkPixels = 0;
            for (int y = meal.top(); y <= meal.bottom(); ++y) {
                for (int x = meal.left(); x <= meal.right(); ++x) {
                    const QColor ink = pixel(cell.topLeft() + QPoint(x,y));
                    if (ink.red() < 215 && ink.green() < 160 && ink.blue() < 110) ++mealInkPixels;
                }
            }
            QVERIFY2(mealInkPixels >= 12, "Meal marker must render readable Chinese strokes inside its badge");
            // ClearType changes individual text pixel colors. Find the two ink
            // rows instead of relying on exact foreground RGB values.
            QList<QRect> timeLines;
            for (int y = 8; y < cell.height() - 8; ++y) {
                QRect rowInk;
                for (int x = 8; x < cell.width() - 8; ++x) {
                    const QPoint point(x,y);
                    if (circle.adjusted(-2,-2,2,2).contains(point)
                        || meal.adjusted(-2,-2,2,2).contains(point)
                        || schedule.adjusted(-2,-2,2,2).contains(point)
                        || note.adjusted(-2,-2,2,2).contains(point)) continue;
                    const QColor ink = pixel(cell.topLeft() + point);
                    if (ink.red() < 160 && ink.green() < 180 && ink.blue() < 180) {
                        rowInk = rowInk.united(QRect(point,QSize(1,1)));
                    }
                }
                if (!rowInk.isEmpty()) {
                    if (timeLines.isEmpty() || rowInk.top() > timeLines.last().bottom() + 2)
                        timeLines.append(rowInk);
                    else timeLines.last() = timeLines.last().united(rowInk);
                }
            }
            QCOMPARE(timeLines.size(), 2);
            const QRect arrival = timeLines[0];
            const QRect departure = timeLines[1];
            QVERIFY(arrival.width() >= 18 && departure.width() >= 18);
            QVERIFY(arrival.bottom() < departure.top());
            QVERIFY(!circle.intersects(arrival) && !circle.intersects(departure));
            QVERIFY(departure.bottom() < meal.top() && departure.bottom() < schedule.top());
            QVERIFY(!note.intersects(arrival) && !note.intersects(departure));
            QVERIFY(meal.left() >= 10 && cell.height() - meal.bottom() >= 10);
            QVERIFY(cell.width() - schedule.right() >= 10 && cell.height() - schedule.bottom() >= 10);
        }
        const int nextIndex = index + 1;
        const QRect nextCell = calendar.cellRect(nextIndex / 7, nextIndex % 7);
        QCOMPARE(bluePixels(QRect(nextCell.left() + 4, nextCell.top() + 4, 44, 44)), 0);
    }
    void cleanupTestCase() {
        m_window->close();
        QTest::qWait(300);
        delete m_window;
        m_window = nullptr;
    }
};
int main(int argc, char** argv)
{
    ScreenLayout::enableHighDpiSupport();
    QApplication app(argc, argv);
    LegacyUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "LegacyUiTests.moc"
