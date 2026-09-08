#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSettings>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <cstdio>
#include <mutex>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {

constexpr wchar_t kDefaultServiceName[] = L"AttendanceUpdateService";
constexpr wchar_t kServiceDisplayName[] = L"工时簿更新服务";
constexpr wchar_t kServiceDescription[] = L"为工时簿提供本地更新分发服务。";
constexpr int kDailyDownloadLimit = 50;

#ifdef Q_OS_WIN
SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS g_serviceStatus{};
std::wstring g_serviceName = kDefaultServiceName;

void updateServiceStatus(DWORD currentState, DWORD exitCode = NO_ERROR, DWORD waitHint = 0);
#endif

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

struct ServiceConfig {
    QString host;
    quint16 port = 47980;
    QString root;
};

QString resolveUpdateRoot()
{
    QSettings settings(QDir(exeDirPath()).filePath(QStringLiteral("updateservice.ini")),
        QSettings::IniFormat);
    const QString configured = settings.value(QStringLiteral("service/root")).toString().trimmed();
    return configured.isEmpty() ? QDir(exeDirPath()).filePath(QStringLiteral("updates"))
                               : QDir::fromNativeSeparators(configured);
}

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

bool isSafeArtifactName(const QString& name)
{
    if (name.isEmpty()) {
        return false;
    }
    for (const QChar character : name) {
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
    return match.hasMatch()
        && QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt())
               .isValid();
}

class DailyDownloadQuota {
public:
    void setStoragePath(const QString& storagePath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_storagePath = storagePath;
        QSettings settings(m_storagePath, QSettings::IniFormat);
        m_day = settings.value(QStringLiteral("downloads/date")).toString();
        m_count = qMax(0, settings.value(QStringLiteral("downloads/count"), 0).toInt());
        resetIfNeeded();
    }

    bool tryConsume()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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

    std::mutex m_mutex;
    QString m_storagePath;
    QString m_day;
    int m_count = 0;
};

class UpdateHttpService {
public:
    bool configure()
    {
        m_config = loadConfig();
        if (m_config.host.isEmpty() || m_config.port == 0) {
            writeLog(QStringLiteral("invalid listen configuration"));
            return false;
        }

        QDir().mkpath(m_config.root);
        QDir().mkpath(QDir(m_config.root).filePath(QStringLiteral("packages")));
        QDir().mkpath(QDir(m_config.root).filePath(QStringLiteral("installers")));
        m_downloadQuota.setStoragePath(
            QDir(m_config.root).filePath(QStringLiteral("download-quota.ini")));

        using namespace drogon;
        app().setLogPath(QDir(exeDirPath()).filePath(QStringLiteral("logs")).toStdString());
        app().setLogLevel(trantor::Logger::kInfo);
        app().setThreadNum(1);
        app().registerHandler("/health", [this](const HttpRequestPtr& request,
                                           std::function<void(const HttpResponsePtr&)>&& callback) {
            Q_UNUSED(request);
            callback(textResponse(k200OK, QStringLiteral("ok"), QStringLiteral("text/plain")));
        }, {Get});
        app().registerHandler("/", [this](const HttpRequestPtr& request,
                                     std::function<void(const HttpResponsePtr&)>&& callback) {
            Q_UNUSED(request);
            callback(downloadPageResponse());
        }, {Get});
        app().registerHandler("/index.html", [this](const HttpRequestPtr& request,
                                               std::function<void(const HttpResponsePtr&)>&& callback) {
            Q_UNUSED(request);
            callback(downloadPageResponse());
        }, {Get});
        app().registerHandler("/assets/logo.svg", [this](const HttpRequestPtr& request,
                                                   std::function<void(const HttpResponsePtr&)>&& callback) {
            Q_UNUSED(request);
            callback(resourceResponse(QStringLiteral(":/download/logo.svg"),
                QStringLiteral("image/svg+xml")));
        }, {Get});
        app().registerHandler("/api/client/release", [this](const HttpRequestPtr& request,
                                                       std::function<void(const HttpResponsePtr&)>&& callback) {
            Q_UNUSED(request);
            callback(fileBodyResponse(QDir(m_config.root).filePath(QStringLiteral("manifest.json")),
                QStringLiteral("application/json; charset=utf-8")));
        }, {Get});
        app().registerHandler("/packages/{1}", [this](const HttpRequestPtr& request,
                                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                                 const std::string& name) {
            serveArtifact(request, std::move(callback), QStringLiteral("packages"),
                QString::fromStdString(name));
        }, {Get});
        app().registerHandler("/installers/{1}", [this](const HttpRequestPtr& request,
                                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                                   const std::string& name) {
            serveArtifact(request, std::move(callback), QStringLiteral("installers"),
                QString::fromStdString(name));
        }, {Get});
        app().addListener(m_config.host.toStdString(), m_config.port);
        app().registerBeginningAdvice([this] {
            writeLog(QStringLiteral("Drogon listener started on http://%1:%2")
                         .arg(m_config.host)
                         .arg(m_config.port));
#ifdef Q_OS_WIN
            if (g_serviceStatusHandle != nullptr) {
                updateServiceStatus(SERVICE_RUNNING);
            }
#endif
        });

        writeLog(QStringLiteral("update service listening on http://%1:%2, root=%3")
                     .arg(m_config.host)
                     .arg(m_config.port)
                     .arg(QDir::toNativeSeparators(m_config.root)));
        return true;
    }

private:
    static drogon::HttpResponsePtr textResponse(drogon::HttpStatusCode status, const QString& body,
        const QString& contentType)
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(status);
        response->setContentTypeCode(drogon::CT_CUSTOM);
        response->setContentTypeString(contentType.toStdString());
        response->setBody(body.toUtf8().toStdString());
        return response;
    }

    static drogon::HttpResponsePtr resourceResponse(const QString& filePath, const QString& contentType)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return textResponse(drogon::k404NotFound, QStringLiteral("not found"),
                QStringLiteral("text/plain"));
        }
        return textResponse(drogon::k200OK, QString::fromUtf8(file.readAll()), contentType);
    }

    static drogon::HttpResponsePtr fileBodyResponse(const QString& filePath, const QString& contentType)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return textResponse(drogon::k404NotFound, QStringLiteral("{\"available\": false}"),
                QStringLiteral("application/json; charset=utf-8"));
        }
        return textResponse(drogon::k200OK, QString::fromUtf8(file.readAll()), contentType);
    }

    drogon::HttpResponsePtr downloadPageResponse() const
    {
        QJsonObject manifest;
        QFile manifestFile(QDir(m_config.root).filePath(QStringLiteral("manifest.json")));
        if (manifestFile.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                manifest = document.object();
            }
        }

        const QString version = manifest.value(QStringLiteral("version")).toString().trimmed();
        const QString packageUrl = manifest.value(QStringLiteral("downloadUrl")).toString().trimmed();
        const QString packageName = packageUrl.mid(QStringLiteral("/packages/").size());
        const bool hasPackage = manifest.value(QStringLiteral("available")).toBool()
            && isReleaseVersion(version) && packageUrl.startsWith(QStringLiteral("/packages/"))
            && isSafeArtifactName(packageName)
            && QFileInfo(QDir(m_config.root).filePath(QStringLiteral("packages/") + packageName)).isFile();

        const QString installerUrl = manifest.value(QStringLiteral("installerUrl")).toString().trimmed();
        const QString installerName = installerUrl.mid(QStringLiteral("/installers/").size());
        const bool hasInstaller = hasPackage && installerUrl.startsWith(QStringLiteral("/installers/"))
            && isSafeArtifactName(installerName)
            && QFileInfo(QDir(m_config.root).filePath(QStringLiteral("installers/") + installerName)).isFile();

        const QString releaseSection = hasPackage
            ? QStringLiteral("<span class=\"version\">%1</span><a class=\"download\" href=\"%2\">下载 Windows 客户端</a>")
                  .arg(version.toHtmlEscaped(), (hasInstaller ? installerUrl : packageUrl).toHtmlEscaped())
            : QStringLiteral("<span class=\"unavailable\">暂无可下载版本</span>");
        const QString page = QStringLiteral(R"(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>工时簿 - Windows 客户端下载</title><style>
:root{color:#182b43;background:#f6f8fb;font-family:"Microsoft YaHei","Segoe UI",sans-serif}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center}main{width:min(100%,480px);padding:28px}header{display:flex;align-items:center;gap:11px;color:#1b4f80;font-size:15px;font-weight:700}header img{width:34px;height:34px}.content{display:flex;align-items:center;gap:20px;margin-top:44px;padding:22px 0;border-top:1px solid #dbe4ec;border-bottom:1px solid #dbe4ec}.version{color:#203b57;font-size:20px;font-weight:700}.download{display:inline-flex;align-items:center;justify-content:center;min-height:42px;margin-left:auto;padding:0 18px;background:#1769aa;border:1px solid #0f5c9b;border-radius:5px;color:#fff;font-size:14px;font-weight:700;text-decoration:none}.download:hover{background:#0f5c9b}.unavailable{color:#65778a;font-size:14px}@media(max-width:420px){main{padding:22px 20px}.content{align-items:stretch;flex-direction:column;gap:16px}.download{margin-left:0}}
</style></head><body><main><header><img src="/assets/logo.svg" alt="工时簿"><span>工时簿</span></header><section class="content">%1</section></main></body></html>)").arg(releaseSection);
        return textResponse(drogon::k200OK, page, QStringLiteral("text/html; charset=utf-8"));
    }

    void serveArtifact(const drogon::HttpRequestPtr& request,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const QString& directory,
        const QString& name)
    {
        if (!isSafeArtifactName(name)) {
            callback(textResponse(drogon::k400BadRequest, QStringLiteral("invalid artifact"),
                QStringLiteral("text/plain")));
            return;
        }
        const QString path = QDir(m_config.root).filePath(directory + QLatin1Char('/') + name);
        if (!QFileInfo(path).isFile()) {
            callback(textResponse(drogon::k404NotFound, QStringLiteral("not found"),
                QStringLiteral("text/plain")));
            return;
        }
        if (!m_downloadQuota.tryConsume()) {
            callback(textResponse(drogon::k429TooManyRequests,
                QStringLiteral("今天的客户端下载次数已达上限，请明天再试。"),
                QStringLiteral("text/plain; charset=utf-8")));
            return;
        }
        writeLog(QStringLiteral("GET /%1/%2 -> 200").arg(directory, name));
        callback(drogon::HttpResponse::newFileResponse(path.toStdString(), name.toStdString(),
            drogon::CT_APPLICATION_OCTET_STREAM, "", request));
    }

    ServiceConfig m_config;
    DailyDownloadQuota m_downloadQuota;
};

bool configureAndRunConsole()
{
    UpdateHttpService server;
    if (!server.configure()) {
        return false;
    }
    try {
        drogon::app().run();
        return true;
    } catch (const std::exception& exception) {
        writeLog(QStringLiteral("http server failed: %1").arg(QString::fromLocal8Bit(exception.what())));
        return false;
    }
}

#ifdef Q_OS_WIN

void updateServiceStatus(DWORD currentState, DWORD exitCode, DWORD waitHint)
{
    if (g_serviceStatusHandle == nullptr) {
        return;
    }
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwWaitHint = waitHint;
    g_serviceStatus.dwCheckPoint =
        (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) ? 0 : 1;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

void WINAPI serviceControlHandler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        updateServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        drogon::app().quit();
    }
}

void WINAPI serviceMainThunk(DWORD, LPWSTR*)
{
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(g_serviceName.c_str(), serviceControlHandler);
    if (g_serviceStatusHandle == nullptr) {
        return;
    }
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwControlsAccepted = 0;
    updateServiceStatus(SERVICE_START_PENDING, NO_ERROR, 5000);

    UpdateHttpService server;
    try {
        if (!server.configure()) {
            updateServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
            return;
        }
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        drogon::app().run();
        updateServiceStatus(SERVICE_STOPPED);
    } catch (const std::exception& exception) {
        writeLog(QStringLiteral("service failed: %1").arg(QString::fromLocal8Bit(exception.what())));
        updateServiceStatus(SERVICE_STOPPED, ERROR_EXCEPTION_IN_SERVICE);
    }
}

bool runAsWindowsService()
{
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {g_serviceName.data(), &serviceMainThunk},
        {nullptr, nullptr},
    };
    if (StartServiceCtrlDispatcherW(serviceTable)) {
        return true;
    }
    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        return configureAndRunConsole();
    }
    writeLog(QStringLiteral("StartServiceCtrlDispatcherW failed: %1").arg(GetLastError()));
    return false;
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
    const std::wstring quotedExePath = L"\"" + std::wstring(exePath) + L"\"";
    SC_HANDLE service = CreateServiceW(manager, g_serviceName.c_str(), kServiceDisplayName,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        quotedExePath.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        message = error == ERROR_SERVICE_EXISTS ? QStringLiteral("服务已存在。")
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
            const bool installed = installService(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return installed ? 0 : 1;
        }
        if (arg == QStringLiteral("--uninstall")) {
            QString message;
            const bool uninstalled = uninstallService(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return uninstalled ? 0 : 1;
        }
        if (arg == QStringLiteral("--start")) {
            QString message;
            const bool started = startServiceProcess(message);
            std::printf("%s\n", message.toLocal8Bit().constData());
            return started ? 0 : 1;
        }
#endif
        if (arg == QStringLiteral("--console")) {
            return configureAndRunConsole() ? 0 : 1;
        }
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            std::printf("AttendanceUpdateService - 工时簿本地更新分发服务\n"
                        "用法:\n"
                        "  AttendanceUpdateService                以服务模式运行（非 SCM 环境自动回退控制台）\n"
                        "  AttendanceUpdateService --console      前台控制台模式\n"
#ifdef Q_OS_WIN
                        "  AttendanceUpdateService --install      安装 Windows 服务（需管理员权限）\n"
                        "  AttendanceUpdateService --uninstall    卸载 Windows 服务\n"
                        "  AttendanceUpdateService --start        启动服务\n"
#endif
                        "\n配置文件: updateservice.ini（服务程序目录下）\n"
                        "  [service]\n  host=127.0.0.1\n  port=47980\n"
                        "  root=更新文件根目录（默认 updates）\n");
            return 0;
        }
    }

#ifdef Q_OS_WIN
    return runAsWindowsService() ? 0 : 1;
#else
    return configureAndRunConsole() ? 0 : 1;
#endif
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AttendanceUpdateService"));
    return run(argc, argv);
}
