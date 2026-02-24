#include "AttendanceMainWindow.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFont>
#include <QIcon>
#include <QTextCodec>
#include <QTextStream>
#include <qsslsocket.h>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

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
        //mylogger->info(msg.toStdString());
        break;
    }

    // ���������̨
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


    // ����Ĭ������
    QFont font = app.font();
    font.setFamily("Microsoft YaHei");
    font.setPointSize(9);
    app.setFont(font);

    AttendanceMainWindow window;
    window.show();

    return app.exec();
}
