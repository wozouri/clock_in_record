#include "AttendanceMainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QIcon>
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
    app.setApplicationVersion("1.0.0");

    eApp->init();
    eTheme->setThemeMode(ElaThemeType::Light);

    QFont font = app.font();
    font.setFamily("Microsoft YaHei");
    font.setPointSize(10);
    app.setFont(font);

    AttendanceMainWindow window;
    window.show();

    return app.exec();
}
