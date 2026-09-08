#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {

constexpr wchar_t kDefaultServiceName[] = L"AttendanceUpdateService";
constexpr wchar_t kServiceDisplayName[] = L"工时簿更新服务";
constexpr wchar_t kServiceDescription[] = L"为工时簿提供本地更新分发服务。";
constexpr int kDailyDownloadLimit = 50;

QEventLoop* g_serviceLoop = nullptr;
SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS g_serviceStatus{};
std::wstring g_serviceName = kDefaultServiceName;

QString exeDirPath()
{
    return QCoreApplication::applicationDirPath();
}

QString logFilePath()
{
    return QDir(exeDirPath()).filePath(QStringLiteral("logs/updateservice.log"));
}

void writeLog(const QString& message)
{
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
        + QLatin1Char(' ') + message;
    QFile logFile(logFilePath());
    QDir().mkpath(QFileInfo(logFilePath()).absolutePath());
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile.write((line + QLatin1Char('\n')).toUtf8());
    }
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
}

QString resolveUpdateRoot()
{
    QSettings settings(QDir(exeDirPath()).filePath(QStringLiteral("updateservice.ini")),
        QSettings::IniFormat);
    const QString configured = settings.value(QStringLiteral("service/root")).toString().trimmed();
    if (!configured.isEmpty()) {
        return QDir::fromNativeSeparators(configured);
    }
    return QDir(exeDirPath()).filePath(QStringLiteral("updates"));
}

struct ServiceConfig {
    QString host;
    quint16 port = 47980;
    QString root;
};

bool isSafePackageName(const QString& packageName)
{
    if (packageName.isEmpty()) {
        return false;
    }
    for (const QChar character : packageName) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('.')
            && character != QLatin1Char('-') && character != QLatin1Char('_')) {
            return false;
        }
    }
    return true;
}

bool isReleaseVersion(const QString& version)
{
    static const QRegularExpression expression(
        QStringLiteral("^v(\\d{4})\\.(\\d{2})\\.(\\d{2})$"));
    const QRegularExpressionMatch match = expression.match(version);
    if (!match.hasMatch()) {
        return false;
    }
    return QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt())
        .isValid();
}

class DailyDownloadQuota {
public:
    void setStoragePath(const QString& storagePath)
    {
        m_storagePath = storagePath;
        QSettings settings(m_storagePath, QSettings::IniFormat);
        m_day = settings.value(QStringLiteral("downloads/date")).toString();
        m_count = qMax(0, settings.value(QStringLiteral("downloads/count"), 0).toInt());
        resetIfNeeded();
    }

    bool tryConsume()
    {
        resetIfNeeded();
        if (m_count >= kDailyDownloadLimit) {
            return false;
        }
        ++m_count;
        QSettings settings(m_storagePath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("downloads/date"), m_day);
        settings.setValue(QStringLiteral("downloads/count"), m_count);
        settings.sync();
        return true;
    }

private:
    void resetIfNeeded()
    {
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        if (m_day != today) {
            m_day = today;
            m_count = 0;
        }
    }

    QString m_storagePath;
    QString m_day;
    int m_count = 0;
};

ServiceConfig loadConfig()
{
    ServiceConfig config;
    QSettings settings(QDir(exeDirPath()).filePath(QStringLiteral("updateservice.ini")),
        QSettings::IniFormat);
    config.host = settings.value(QStringLiteral("service/host"), QStringLiteral("127.0.0.1"))
                      .toString()
                      .trimmed();
    config.port = static_cast<quint16>(
        settings.value(QStringLiteral("service/port"), 47980).toUInt());
    config.root = resolveUpdateRoot();
    return config;
}

class UpdateHttpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit UpdateHttpServer(QObject* parent = nullptr) : QTcpServer(parent) {}

    void setUpdateRoot(const QString& root)
    {
        m_updateRoot = root;
        m_downloadQuota.setStoragePath(
            QDir(m_updateRoot).filePath(QStringLiteral("download-quota.ini")));
    }

signals:
    void requestHandled(const QString& summary);

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            m_buffers[socket] += socket->readAll();
            if (m_buffers.value(socket).contains("\r\n\r\n")) {
                handleRequest(socket);
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }

private:
    void handleRequest(QTcpSocket* socket) {
        const QByteArray raw = m_buffers.take(socket);
        const QString requestText = QString::fromLatin1(raw.left(raw.indexOf("\r\n\r\n")));
        const QStringList lines = requestText.split(QStringLiteral("\r\n"));
        if (lines.isEmpty()) {
            sendSimpleResponse(socket, 400, "Bad Request", "text/plain", "bad request");
            return;
        }
        const QStringList requestParts = lines.first().split(QLatin1Char(' '));
        if (requestParts.size() < 2 || requestParts.first() != QStringLiteral("GET")) {
            sendSimpleResponse(socket, 405, "Method Not Allowed", "text/plain", "GET only");
            return;
        }
        QString path = requestParts.at(1);
        const int queryIndex = path.indexOf(QLatin1Char('?'));
        if (queryIndex >= 0) {
            path.truncate(queryIndex);
        }
        if (!path.startsWith(QLatin1Char('/')) || path.contains(QStringLiteral(".."))
            || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char('%'))) {
            sendSimpleResponse(socket, 400, "Bad Request", "text/plain", "invalid path");
            return;
        }

        if (path == QStringLiteral("/health")) {
            sendSimpleResponse(socket, 200, "OK", "text/plain", "ok");
            return;
        }
        if (path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
            serveDownloadPage(socket);
            return;
        }
        if (path == QStringLiteral("/assets/logo.svg")) {
            serveFile(socket, QStringLiteral(":/download/logo.svg"), QStringLiteral("image/svg+xml"));
            return;
        }
        if (path == QStringLiteral("/api/client/release")) {
            serveFile(socket, QDir(m_updateRoot).filePath(QStringLiteral("manifest.json")),
                QStringLiteral("application/json; charset=utf-8"));
            return;
        }
        if (path.startsWith(QStringLiteral("/packages/"))) {
            const QString packageName = path.mid(QStringLiteral("/packages/").size());
            if (!isSafePackageName(packageName)) {
                sendSimpleResponse(socket, 400, "Bad Request", "text/plain", "invalid package");
                return;
            }
            const QString packagePath =
                QDir(QDir(m_updateRoot).filePath(QStringLiteral("packages"))).filePath(packageName);
            if (!QFileInfo(packagePath).isFile()) {
                serveFile(socket, packagePath, QStringLiteral("application/octet-stream"), true);
                return;
            }
            if (!m_downloadQuota.tryConsume()) {
                sendSimpleResponse(socket, 429, "Too Many Requests", "text/plain; charset=utf-8",
                    QStringLiteral("今天的客户端下载次数已达上限，请明天再试。"));
                return;
            }
            serveFile(socket, packagePath, QStringLiteral("application/octet-stream"), true);
            return;
        }
        sendSimpleResponse(socket, 404, "Not Found", "text/plain", "not found");
    }

    void serveDownloadPage(QTcpSocket* socket) {
        QJsonObject manifest;
        QFile manifestFile(QDir(m_updateRoot).filePath(QStringLiteral("manifest.json")));
        if (manifestFile.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                manifest = document.object();
            }
        }

        const QString version = manifest.value(QStringLiteral("version")).toString().trimmed();
        const QString downloadPath = manifest.value(QStringLiteral("downloadUrl")).toString().trimmed();
        const QString packageName = downloadPath.mid(QStringLiteral("/packages/").size());
        const QFileInfo packageInfo(QDir(QDir(m_updateRoot).filePath(QStringLiteral("packages")))
            .filePath(packageName));
        const bool isAvailable = manifest.value(QStringLiteral("available")).toBool()
            && isReleaseVersion(version)
            && downloadPath.startsWith(QStringLiteral("/packages/"))
            && isSafePackageName(packageName)
            && packageInfo.isFile();

        const QString escapedVersion = version.toHtmlEscaped();
        const QString releaseSection = isAvailable
            ? QStringLiteral(
                "<span class=\"version\">%1</span>"
                "<a class=\"download\" href=\"%2\">下载 Windows 客户端</a>")
                  .arg(escapedVersion, downloadPath.toHtmlEscaped())
            : QStringLiteral("<span class=\"unavailable\">暂无可下载版本</span>");

        const QString page = QStringLiteral(R"(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>工时簿 - Windows 客户端下载</title>
<style>
:root { color: #182b43; background: #f6f8fb; font-family: "Microsoft YaHei", "Segoe UI", sans-serif; }
* { box-sizing: border-box; }
body { margin: 0; min-height: 100vh; display: grid; place-items: center; }
main { width: min(100%, 480px); padding: 28px; }
header { display: flex; align-items: center; gap: 11px; color: #1b4f80; font-size: 15px; font-weight: 700; }
header img { width: 34px; height: 34px; }
.content { display: flex; align-items: center; gap: 20px; margin-top: 44px; padding: 22px 0; border-top: 1px solid #dbe4ec; border-bottom: 1px solid #dbe4ec; }
.version { color: #203b57; font-size: 20px; font-weight: 700; }
.download { display: inline-flex; align-items: center; justify-content: center; min-height: 42px; margin-left: auto; padding: 0 18px; background: #1769aa; border: 1px solid #0f5c9b; border-radius: 5px; color: #fff; font-size: 14px; font-weight: 700; text-decoration: none; }
.download:hover { background: #0f5c9b; }
.unavailable { color: #65778a; font-size: 14px; }
@media (max-width: 420px) { main { padding: 22px 20px; } .content { align-items: stretch; flex-direction: column; gap: 16px; } .download { margin-left: 0; } }
</style>
</head>
<body><main>
<header><img src="/assets/logo.svg" alt="工时簿"><span>工时簿</span></header>
<section class="content">%1</section>
</main></body></html>)").arg(releaseSection);
        sendSimpleResponse(socket, 200, "OK", "text/html; charset=utf-8", page);
    }

    void serveFile(QTcpSocket* socket, const QString& filePath, const QString& contentType,
        bool asDownload = false) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            sendSimpleResponse(socket, 404, "Not Found", "application/json; charset=utf-8",
                QStringLiteral("{\"available\": false}"));
            return;
        }
        const QByteArray body = file.readAll();
        QByteArray response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: " + contentType.toLatin1() + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        if (asDownload) {
            response += "Content-Disposition: attachment; filename=\""
                + QFileInfo(filePath).fileName().toUtf8() + "\"\r\n";
        }
        response += "Cache-Control: no-cache\r\n";
        response += "Connection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
        emit requestHandled(QStringLiteral("GET %1 -> 200 (%2 bytes)")
                .arg(QFileInfo(filePath).fileName())
                .arg(body.size()));
    }

    void sendSimpleResponse(QTcpSocket* socket, int statusCode, const char* reasonPhrase,
        const QString& contentType, const QString& bodyText) {
        const QByteArray body = bodyText.toUtf8();
        QByteArray response;
        response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reasonPhrase + "\r\n";
        response += "Content-Type: " + contentType.toLatin1() + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
        emit requestHandled(QStringLiteral("-> %1 %2").arg(statusCode).arg(reasonPhrase));
    }

    QString m_updateRoot;
    DailyDownloadQuota m_downloadQuota;
    QMap<QTcpSocket*, QByteArray> m_buffers;
};

bool runHttpServer()
{
    const ServiceConfig config = loadConfig();
    QDir().mkpath(config.root);
    QDir().mkpath(QDir(config.root).filePath(QStringLiteral("packages")));

    UpdateHttpServer server;
    server.setUpdateRoot(config.root);
    QObject::connect(&server, &UpdateHttpServer::requestHandled, &server,
        [](const QString& summary) { writeLog(QStringLiteral("http %1").arg(summary)); });

    if (!server.listen(QHostAddress(config.host), config.port)) {
        writeLog(QStringLiteral("listen failed on %1:%2: %3")
                     .arg(config.host)
                     .arg(config.port)
                     .arg(server.errorString()));
        return false;
    }
    writeLog(QStringLiteral("update service listening on http://%1:%2, root=%3")
                 .arg(config.host)
                 .arg(config.port)
                 .arg(QDir::toNativeSeparators(config.root)));

    QEventLoop loop;
    g_serviceLoop = &loop;
    QTimer stopWatcher;
    QObject::connect(&stopWatcher, &QTimer::timeout, &loop, [&loop] {
        if (g_serviceLoop == nullptr) {
            loop.quit();
        }
    });
    stopWatcher.start(200);
    loop.exec();
    g_serviceLoop = nullptr;
    server.close();
    writeLog(QStringLiteral("update service stopped"));
    return true;
}

#ifdef Q_OS_WIN

void updateServiceStatus(DWORD currentState, DWORD exitCode = NO_ERROR, DWORD waitHint = 0)
{
    if (g_serviceStatusHandle == nullptr) {
        return;
    }
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwWaitHint = waitHint;
    g_serviceStatus.dwCheckPoint++;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

void WINAPI serviceControlHandler(DWORD control)
{
    switch (control) {
        case SERVICE_CONTROL_STOP:
            updateServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 2000);
            if (g_serviceLoop != nullptr) {
                QMetaObject::invokeMethod(g_serviceLoop, "quit", Qt::QueuedConnection);
            }
            break;
        case SERVICE_CONTROL_INTERROGATE:
        default:
            updateServiceStatus(g_serviceStatus.dwCurrentState);
            break;
    }
}

void WINAPI serviceMainThunk(DWORD argc, LPWSTR* argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(g_serviceName.c_str(), serviceControlHandler);
    if (g_serviceStatusHandle == nullptr) {
        return;
    }
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    updateServiceStatus(SERVICE_START_PENDING, NO_ERROR, 2000);

    if (runHttpServer()) {
        updateServiceStatus(SERVICE_STOPPED);
    }
    else {
        updateServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    }
}

bool runAsWindowsService()
{
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {g_serviceName.data(), &serviceMainThunk},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // 不是由服务控制管理器拉起时回退到控制台模式，便于直接运行调试。
            return runHttpServer();
        }
        writeLog(QStringLiteral("StartServiceCtrlDispatcherW failed: %1").arg(error));
        return false;
    }
    return true;
}

bool installService(QString& message)
{
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        message = QStringLiteral("无法获取服务程序路径。");
        return false;
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (manager == nullptr) {
        message = QStringLiteral("打开服务管理器失败（需要管理员权限）。");
        return false;
    }
    SC_HANDLE service = CreateServiceW(manager, g_serviceName.c_str(), kServiceDisplayName,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        exePath, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        message = error == ERROR_SERVICE_EXISTS
            ? QStringLiteral("服务已存在。")
            : QStringLiteral("创建服务失败（错误码 %1）。").arg(error);
        return false;
    }

    SERVICE_DESCRIPTIONW description{};
    description.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    message = QStringLiteral("服务安装成功。");
    return true;
}

bool uninstallService(QString& message)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (manager == nullptr) {
        message = QStringLiteral("打开服务管理器失败（需要管理员权限）。");
        return false;
    }
    SC_HANDLE service = OpenServiceW(manager, g_serviceName.c_str(), SERVICE_ALL_ACCESS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        message = QStringLiteral("服务不存在。");
        return false;
    }
    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    const bool deleted = DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    message = deleted ? QStringLiteral("服务卸载成功。") : QStringLiteral("删除服务失败。");
    return deleted;
}

bool startServiceProcess(QString& message)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SERVICE_ALL_ACCESS);
    if (manager == nullptr) {
        message = QStringLiteral("打开服务管理器失败（需要管理员权限）。");
        return false;
    }
    SC_HANDLE service = OpenServiceW(manager, g_serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        message = QStringLiteral("服务尚未安装。");
        return false;
    }
    const bool started = StartServiceW(service, 0, nullptr);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    message = started ? QStringLiteral("服务已启动。") : QStringLiteral("启动服务失败。");
    return started;
}

#endif

int run(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
#ifdef Q_OS_WIN
        if (arg == QStringLiteral("--install")) {
            QString message;
            installService(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return 0;
        }
        if (arg == QStringLiteral("--uninstall")) {
            QString message;
            uninstallService(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return 0;
        }
        if (arg == QStringLiteral("--start")) {
            QString message;
            startServiceProcess(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return 0;
        }
        if (arg == QStringLiteral("--service-name") && i + 1 < argc) {
            g_serviceName = QString::fromLocal8Bit(argv[++i]).toStdWString();
            continue;
        }
#endif
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            std::printf(
                "AttendanceUpdateService - 工时簿本地更新分发服务\n"
                "用法:\n"
                "  AttendanceUpdateService                以服务模式运行（非 SCM 环境自动回退控制台）\n"
                "  AttendanceUpdateService --console      前台控制台模式\n"
#ifdef Q_OS_WIN
                "  AttendanceUpdateService --install      安装 Windows 服务（需管理员权限）\n"
                "  AttendanceUpdateService --uninstall    卸载 Windows 服务\n"
                "  AttendanceUpdateService --start        启动服务\n"
#endif
                "\n配置文件: updateservice.ini（服务程序目录下）\n"
                "  [service]\n"
                "  host=127.0.0.1\n"
                "  port=47980\n"
                "  root=更新文件根目录（默认 updates）\n");
            return 0;
        }
    }

#ifdef Q_OS_WIN
    if (runAsWindowsService()) {
        return 0;
    }
    return 1;
#else
    return runHttpServer() ? 0 : 1;
#endif
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AttendanceUpdateService"));
    QCoreApplication::setOrganizationName(QStringLiteral("MyCompany"));
    return run(argc, argv);
}

#include "main.moc"
