#include "AttendanceMainWindow.h"
#include "AppVersion.h"
#include "Data/AttendanceStorage.h"
#include "Utils/SingleInstanceGuard.h"
#include "Utils/AppFont.h"

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

#include <ElaApplication.h>
#include <ElaTheme.h>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

void messageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(type);
    Q_UNUSED(context);

    QTextStream console(stdout);
    console << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " " << msg << Qt::endl;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

#if defined(Q_OS_WIN) && defined(_DEBUG)
    // 仅调试构建挂接控制台查看日志输出，发布构建不再弹出命令行窗口。
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONIN$", "r", stdin);
    freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

    qInstallMessageHandler(messageOutput);

    app.setWindowIcon(QIcon(":/Icons/logo.ico"));
    app.setApplicationName("AttendanceApp"); // Keep the existing application data location.
    app.setApplicationDisplayName(QStringLiteral("工时簿"));
    app.setOrganizationName("MyCompany");
    app.setApplicationVersion(QStringLiteral(ATTENDANCE_APP_VERSION));

    SingleInstanceGuard instance(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const auto startResult = instance.start();
    if (startResult == SingleInstanceGuard::StartResult::ActivatedExisting) {
        return 0;
    }
    if (startResult == SingleInstanceGuard::StartResult::Failed) {
        QMessageBox::warning(nullptr, QStringLiteral("工时簿"), instance.errorString());
        return 1;
    }

    QString storageError;
    if (!AttendanceStorage::initialize(storageError)) {
        QMessageBox::critical(nullptr, QStringLiteral("考勤数据无法加载"), storageError);
        return 1;
    }

    eApp->init();
    eTheme->setThemeMode(ElaThemeType::Light);

    initializeAppFont();

    AttendanceMainWindow window;
    QObject::connect(&instance, &SingleInstanceGuard::activationRequested, &window, [&window] {
        if (window.isMinimized()) {
            window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
        }
        window.show();
        QWidget* target = QApplication::activeModalWidget();
        if (target == nullptr) {
            target = &window;
        }
        if (target->isMinimized()) {
            target->setWindowState(target->windowState() & ~Qt::WindowMinimized);
        }
        target->raise();
        target->activateWindow();
#ifdef Q_OS_WIN
        const HWND handle = reinterpret_cast<HWND>(target->winId());
        if (!SetForegroundWindow(handle)) {
            FLASHWINFO flash = {sizeof(FLASHWINFO), handle, FLASHW_TRAY, 3, 0};
            FlashWindowEx(&flash);
        }
#endif
    });
    window.show();

    return app.exec();
}
