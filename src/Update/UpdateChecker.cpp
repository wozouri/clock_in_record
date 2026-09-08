#include "UpdateChecker.h"

#include "ClientVersion.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

namespace {

constexpr int kCheckRequestTimeoutMs = 8000;
constexpr int kDownloadInactivityTimeoutMs = 60000;
constexpr char kDefaultServiceHost[] = "192.168.3.35";
constexpr quint16 kDefaultServicePort = 47980;

QString updateServiceConfigPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("updateservice.ini"));
}

bool isValidSha256Digest(const QString& digest)
{
    const QString normalized = digest.trimmed();
    if (normalized.size() != 64) {
        return false;
    }
    for (const QChar character : normalized) {
        const bool isDecimal = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool isLowerHex = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        const bool isUpperHex = character >= QLatin1Char('A') && character <= QLatin1Char('Z');
        if (!isDecimal && !isLowerHex && !isUpperHex) {
            return false;
        }
    }
    return true;
}

QString pendingUpdateMarkerPath()
{
    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dataDirectory).filePath(QStringLiteral("pending-update-version"));
}

QString tempUpdateDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("AttendanceApp/updates"));
}

QString quotedPath(const QString& path)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
}

void writeProbeLog(const QString& message)
{
    const QString logPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("update-probe.log"));
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
            + QLatin1Char(' ') + message;
        logFile.write(line.toUtf8());
        logFile.write("\n");
    }
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), m_packageFile(QString())
{
    m_network = new QNetworkAccessManager(this);
    m_baseUrl = QUrl(updateServiceBaseUrl());
}

void UpdateChecker::setServiceBaseUrl(const QUrl& url)
{
    if (url.isValid() && url.scheme() == QStringLiteral("http")) {
        m_baseUrl = url;
    }
}

QUrl UpdateChecker::serviceBaseUrl() const
{
    return m_baseUrl;
}

QString UpdateChecker::updateServiceBaseUrl()
{
    return QStringLiteral("http://%1:%2")
        .arg(updateServiceHost())
        .arg(updateServicePort());
}

QString UpdateChecker::updateServiceHost()
{
    QSettings settings(updateServiceConfigPath(), QSettings::IniFormat);
    const QString configured = settings.value(QStringLiteral("update/host")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QUrl legacyUrl(settings.value(QStringLiteral("update/baseUrl")).toString().trimmed());
    if (legacyUrl.isValid() && !legacyUrl.host().isEmpty()) {
        return legacyUrl.host();
    }
    return QString::fromLatin1(kDefaultServiceHost);
}

quint16 UpdateChecker::updateServicePort()
{
    QSettings settings(updateServiceConfigPath(), QSettings::IniFormat);
    bool valid = false;
    const uint configured = settings.value(QStringLiteral("update/port")).toUInt(&valid);
    if (valid && configured > 0 && configured <= 65535) {
        return static_cast<quint16>(configured);
    }

    const QUrl legacyUrl(settings.value(QStringLiteral("update/baseUrl")).toString().trimmed());
    if (legacyUrl.isValid() && legacyUrl.port() > 0) {
        return static_cast<quint16>(legacyUrl.port());
    }
    return kDefaultServicePort;
}

void UpdateChecker::saveUpdateServiceEndpoint(const QString& host, quint16 port)
{
    QSettings settings(updateServiceConfigPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("update/host"), host.trimmed());
    settings.setValue(QStringLiteral("update/port"), port);
    settings.remove(QStringLiteral("update/baseUrl"));
    settings.sync();
}

QString UpdateChecker::takePendingUpdateVersion()
{
    QFile markerFile(pendingUpdateMarkerPath());
    if (!markerFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString version = QString::fromUtf8(markerFile.readAll()).trimmed();
    markerFile.close();
    QFile::remove(markerFile.fileName());
    return version;
}

bool UpdateChecker::isDownloadInProgress() const
{
    return m_downloadInProgress;
}

QNetworkReply* UpdateChecker::createGetReply(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Cache-Control", "no-cache");
    return m_network->get(request);
}

void UpdateChecker::checkForUpdates(bool userInitiated)
{
    if (m_downloadInProgress || m_checkReply != nullptr) {
        return;
    }
    m_userInitiated = userInitiated;
    QUrl requestUrl = m_baseUrl.resolved(QUrl(QStringLiteral("/api/client/release")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("target"), QStringLiteral("windows-x86_64"));
    requestUrl.setQuery(query);

    m_checkReply = createGetReply(requestUrl);
    connect(m_checkReply, &QNetworkReply::finished, this,
        [this, reply = m_checkReply] { handleCheckReply(reply, m_userInitiated); });
    QTimer::singleShot(kCheckRequestTimeoutMs, this, [this, reply = m_checkReply] {
        if (m_checkReply == reply) {
            m_checkReply->setProperty("attendanceCheckTimedOut", true);
            m_checkReply->abort();
        }
    });
}

void UpdateChecker::handleCheckReply(QNetworkReply* reply, bool userInitiated)
{
    if (m_checkReply != reply) {
        reply->deleteLater();
        return;
    }
    m_checkReply = nullptr;
    reply->deleteLater();

    UpdateReleaseInfo info;
    const bool timedOut = reply->property("attendanceCheckTimedOut").toBool();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QJsonObject payload = QJsonDocument::fromJson(body).object();

    info.available = reply->error() == QNetworkReply::NoError && statusCode == 200
        && payload.value(QStringLiteral("available")).toBool();
    info.version = payload.value(QStringLiteral("version")).toString().trimmed();
    info.notes = payload.value(QStringLiteral("notes")).toString().trimmed();
    const QString downloadPath = payload.value(QStringLiteral("downloadUrl")).toString().trimmed();
    info.sha256 = payload.value(QStringLiteral("sha256")).toString().trimmed();

    const bool validDownloadPath = downloadPath.startsWith(QLatin1Char('/'))
        && !downloadPath.startsWith(QStringLiteral("//"));
    if (info.available && (!attendance::isValidClientVersion(info.version) || !validDownloadPath
            || !isValidSha256Digest(info.sha256))) {
        info.available = false;
        if (userInitiated) {
            info.errorMessage = QStringLiteral("更新清单格式无效。");
        }
    }
    else if (reply->error() != QNetworkReply::NoError && userInitiated) {
        info.errorMessage = timedOut
            ? QStringLiteral("检查更新超时。")
            : QStringLiteral("无法连接更新服务：%1").arg(reply->errorString());
    }

    if (info.available) {
        info.downloadUrl = m_baseUrl.resolved(QUrl(downloadPath));
        info.isNewer = attendance::isClientVersionNewer(
            info.version, QCoreApplication::applicationVersion());
    }
    writeProbeLog(QStringLiteral("check finished available=%1 version=%2 newer=%3 error=%4 http=%5")
                      .arg(info.available ? 1 : 0)
                      .arg(info.version)
                      .arg(info.isNewer ? 1 : 0)
                      .arg(info.errorMessage.isEmpty() ? QStringLiteral("-") : info.errorMessage)
                      .arg(statusCode));
    m_releaseInfo = info;
    emit checkFinished(info, userInitiated);
}

void UpdateChecker::cancelDownload()
{
    if (m_downloadReply != nullptr) {
        m_downloadReply->setProperty("attendanceDownloadCanceled", true);
        m_downloadReply->abort();
    }
}

void UpdateChecker::startDownload()
{
    m_tempDirectory = tempUpdateDirectory();
    if (m_tempDirectory.isEmpty() || !QDir().mkpath(m_tempDirectory)) {
        abortWithError(QStringLiteral("无法创建更新临时目录。"));
        return;
    }

    m_packagePath = QDir(m_tempDirectory)
                        .filePath(QStringLiteral("AttendanceApp-%1.zip")
                                      .arg(m_releaseInfo.version));
    m_packageFile.setFileName(m_packagePath);
    writeProbeLog(QStringLiteral("download open file=%1 ok=%2")
                      .arg(QDir::toNativeSeparators(m_packagePath))
                      .arg(m_packageFile.open(QIODevice::WriteOnly) ? 1 : 0));
    if (!m_packageFile.isOpen()) {
        abortWithError(QStringLiteral("无法写入更新包：%1").arg(m_packageFile.errorString()));
        return;
    }

    m_downloadInProgress = true;
    m_failureReported = false;
    m_bytesWritten = 0;
    writeProbeLog(QStringLiteral("download start url=%1").arg(m_releaseInfo.downloadUrl.toString()));
    m_downloadReply = createGetReply(m_releaseInfo.downloadUrl);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_downloadReply->readAll();
        if (data.isEmpty()) {
            return;
        }
        if (m_packageFile.write(data) != data.size()) {
            m_downloadReply->setProperty("attendanceWriteFailed", true);
            m_downloadReply->abort();
        }
        else {
            m_bytesWritten += data.size();
        }
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
        [this](qint64 received, qint64 total) {
            m_inactivityTimer->start(kDownloadInactivityTimeoutMs);
            if (total > 0) {
                emit downloadProgress(static_cast<int>(
                    qBound<qint64>(0, received * 100 / total, 99)));
            }
        });
    connect(m_downloadReply, &QNetworkReply::finished, this,
        [this, reply = m_downloadReply] { handleDownloadFinished(reply); });

    m_inactivityTimer = new QTimer(this);
    m_inactivityTimer->setSingleShot(true);
    connect(m_inactivityTimer, &QTimer::timeout, this, [this] {
        if (m_downloadReply != nullptr) {
            m_downloadReply->setProperty("attendanceDownloadTimedOut", true);
            m_downloadReply->abort();
        }
    });
    m_inactivityTimer->start(kDownloadInactivityTimeoutMs);
    emit downloadProgress(0);
}

void UpdateChecker::handleDownloadFinished(QNetworkReply* reply)
{
    if (m_downloadReply != reply) {
        reply->deleteLater();
        return;
    }
    m_downloadReply = nullptr;
    m_inactivityTimer->stop();
    m_inactivityTimer->deleteLater();
    m_inactivityTimer = nullptr;
    reply->deleteLater();

    if (m_packageFile.isOpen() && !m_packageFile.commit()) {
        abortWithError(QStringLiteral("保存更新包失败：%1").arg(m_packageFile.errorString()));
        return;
    }

    const bool canceled = reply->property("attendanceDownloadCanceled").toBool();
    const bool timedOut = reply->property("attendanceDownloadTimedOut").toBool();
    const bool writeFailed = reply->property("attendanceWriteFailed").toBool();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (canceled) {
        QFile::remove(m_packagePath);
        m_downloadInProgress = false;
        emit failed(QStringLiteral("已取消下载更新。"));
        return;
    }
    if (writeFailed || reply->error() != QNetworkReply::NoError || statusCode != 200) {
        m_packageFile.cancelWriting();
        QFile::remove(m_packagePath);
        m_downloadInProgress = false;
        abortWithError(timedOut ? QStringLiteral("下载更新包超时，请稍后重试。")
                                : QStringLiteral("下载更新包失败：%1").arg(reply->errorString()));
        return;
    }
    verifyPackage();
}

void UpdateChecker::verifyPackage()
{
    QFile packageFile(m_packagePath);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        abortWithError(QStringLiteral("无法读取更新包进行校验。"));
        return;
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!packageFile.atEnd()) {
        const QByteArray block = packageFile.read(1024 * 1024);
        if (block.isEmpty() && packageFile.error() != QFileDevice::NoError) {
            abortWithError(QStringLiteral("读取更新包失败，更新已取消。"));
            return;
        }
        hasher.addData(block);
    }
    const QString actualDigest = QString::fromLatin1(hasher.result().toHex());
    if (actualDigest.compare(m_releaseInfo.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(m_packagePath);
        m_downloadInProgress = false;
        abortWithError(QStringLiteral("更新包校验失败，更新已取消。"));
        return;
    }
    extractPackage();
}

void UpdateChecker::extractPackage()
{
    const QString extractDir = QDir(m_tempDirectory).filePath(QStringLiteral("extract-%1")
                                                                     .arg(m_releaseInfo.version));
    QDir(extractDir).removeRecursively();
    if (!QDir().mkpath(extractDir)) {
        abortWithError(QStringLiteral("无法创建更新解压目录。"));
        return;
    }

    const QString command = QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                                .arg(quotedPath(m_packagePath), quotedPath(extractDir));
    m_extractProcess = new QProcess(this);
    connect(m_extractProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_extractProcess != nullptr && m_extractProcess->state() != QProcess::NotRunning) {
            abortWithError(QStringLiteral("解压更新包失败，更新已取消。"));
        }
    });
    connect(m_extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, extractDir](int exitCode, QProcess::ExitStatus exitStatus) {
            m_extractProcess->deleteLater();
            m_extractProcess = nullptr;
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                abortWithError(QStringLiteral("解压更新包失败，更新已取消。"));
                return;
            }
            launchApplyScript(extractDir);
        });
    m_extractProcess->start(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-Command"), command});
}

void UpdateChecker::launchApplyScript(const QString& extractDir)
{
    const QString installDir = QCoreApplication::applicationDirPath();
    const QString markerPath = pendingUpdateMarkerPath();
    QDir markerDirectory(QFileInfo(markerPath).absolutePath());
    if (!markerDirectory.exists() && !markerDirectory.mkpath(QStringLiteral("."))) {
        abortWithError(QStringLiteral("无法写入更新标记。"));
        return;
    }
    QFile markerFile(markerPath);
    if (!markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || markerFile.write(m_releaseInfo.version.toUtf8()) < 0) {
        abortWithError(QStringLiteral("无法写入更新标记。"));
        return;
    }

    const QString scriptPath = QDir(m_tempDirectory).filePath(QStringLiteral("apply_update.ps1"));
    const QString script = QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$deadline = (Get-Date).AddSeconds(30)\n"
        "while ((Get-Date) -lt $deadline) {\n"
        "    if (-not (Get-Process -Name 'AttendanceApp' -ErrorAction SilentlyContinue)) { break }\n"
        "    Start-Sleep -Milliseconds 500\n"
        "}\n"
        "$sourceRoot = %1\n"
        "$files = @(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse)\n"
        "foreach ($f in $files) {\n"
        "    $relativePath = $f.FullName.Substring($sourceRoot.Length).TrimStart('\\')\n"
        "    $destination = Join-Path %2 $relativePath\n"
        "    try {\n"
        "        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null\n"
        "        Copy-Item -LiteralPath $f.FullName -Destination $destination -Force -ErrorAction Stop\n"
        "    }\n"
        "    catch { }\n"
        "}\n"
        "Remove-Item -LiteralPath %3 -Force -ErrorAction SilentlyContinue\n"
        "Start-Process -FilePath %4 -WorkingDirectory %2\n"
        "Remove-Item -LiteralPath %5 -Recurse -Force -ErrorAction SilentlyContinue\n")
                           .arg(quotedPath(extractDir), quotedPath(installDir),
                               quotedPath(m_packagePath),
                               quotedPath(QDir(installDir).filePath(
                                   QCoreApplication::applicationName() + QStringLiteral(".exe"))),
                               quotedPath(extractDir));
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        abortWithError(QStringLiteral("无法生成更新脚本。"));
        return;
    }
    // 带 BOM 的 UTF-8，保证 Windows PowerShell 5.x 正确解析非 ASCII 路径。
    scriptFile.write("\xEF\xBB\xBF");
    scriptFile.write(script.toUtf8());
    scriptFile.close();

    const bool launched = QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), scriptPath});
    if (!launched) {
        abortWithError(QStringLiteral("无法启动更新脚本。"));
        return;
    }
    m_downloadInProgress = false;
    emit applyReady(m_releaseInfo.version);
}

void UpdateChecker::abortWithError(const QString& message)
{
    if (m_failureReported) {
        return;
    }
    m_failureReported = true;
    m_downloadInProgress = false;
    writeProbeLog(QStringLiteral("abort: %1").arg(message));
    emit failed(message);
}
