#include "AttendanceMainWindow.h"
#include "LicenseCheck.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QTextCodec>
#include <QTextStream>
#include <qsslsocket.h>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {

QString formatActivationFailure(const QString& errorMessage, const license_public::LicenseCheckResult& response)
{
    QStringList lines;
    if (!response.result.isEmpty()) {
        lines << QStringLiteral("result: %1").arg(response.result);
    }
    if (!response.message.isEmpty()) {
        lines << QStringLiteral("message: %1").arg(response.message);
    }
    if (!response.licenseStatus.isEmpty()) {
        lines << QStringLiteral("license_status: %1").arg(response.licenseStatus);
    }
    if (!response.expiresAt.isEmpty()) {
        lines << QStringLiteral("expires_at: %1").arg(response.expiresAt);
    }
    if (response.httpStatus > 0) {
        lines << QStringLiteral("http_status: %1").arg(response.httpStatus);
    }
    if (!response.errorText.isEmpty()) {
        lines << QStringLiteral("error: %1").arg(response.errorText);
    }
    if (!response.boundDeviceFingerprint.isEmpty()) {
        lines << QStringLiteral("bound_device_fingerprint: %1").arg(response.boundDeviceFingerprint);
    }
    if (!errorMessage.isEmpty()) {
        lines << QStringLiteral("display_error: %1").arg(errorMessage);
    }
    return lines.join('\n');
}

bool ensureLicenseActivated()
{
    license_public::LicenseCheckRequest request;
    request.appVersion = QCoreApplication::applicationVersion();
    request.clientNote = QStringLiteral("clock_in_record startup check");

    QString errorMessage;
    license_public::LicenseCheckResult response;
    if (license_public::LicenseCheck::check(request, errorMessage, &response)) {
        return true;
    }

    QMessageBox messageBox;
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(QStringLiteral("授权校验失败"));
    messageBox.setText(QStringLiteral("当前 License 无法通过校验，应用将无法继续启动。"));
    messageBox.setInformativeText(QStringLiteral("请检查程序目录下的 license_public.ini 配置。"));
    messageBox.setDetailedText(formatActivationFailure(errorMessage, response));
    messageBox.exec();
    return false;
}

}  // namespace

void messageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    switch (type) {
    case QtDebugMsg:
        //msg.toStdString();
        break;
    case QtWarningMsg:
        //mylogger->warn(msg.toStdString());
        break;
    case QtCriticalMsg:
        //mylogger->critical(msg.toStdString());
        break;
    case QtFatalMsg:
        //mylogger->error(msg.toStdString());
        break;
    case QtInfoMsg:
        break;
    }

    Q_UNUSED(context);

    QTextStream console(stdout);
    console << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " " << msg << Qt::endl;
}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

#ifdef Q_OS_WIN
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONIN$", "r", stdin);
    freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

    // ��װ��־������
    qInstallMessageHandler(messageOutput);

    qDebug() << "SSL supported:" << QSslSocket::supportsSsl();
    qDebug() << "OpenSSL version:" << QSslSocket::sslLibraryVersionString();

    // ����Ӧ�ó���ͼ�꣨����еĻ���
     app.setWindowIcon(QIcon(":/Icons/logo.ico"));
      // ����Ӧ�ó�����Ϣ
      app.setApplicationName("AttendanceApp");
      app.setOrganizationName("MyCompany");
      app.setApplicationVersion("1.0.0");


     // 设置默认字体
    QFont font = app.font();
    font.setFamily("Microsoft YaHei");
    font.setPointSize(10);
    app.setFont(font);

     if (!ensureLicenseActivated()) {
         return 0;
     }

     AttendanceMainWindow window;
     window.show();

    return app.exec();
}
