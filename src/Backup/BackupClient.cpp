#include "BackupClient.h"

#include "Data/AttendanceStorage.h"
#include "Update/UpdateChecker.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>

namespace {
bool validCredential(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}$"));
    return pattern.match(value).hasMatch();
}

QString hash(const QByteArray& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
} // namespace

BackupClient::BackupClient(const QString& dataDirectory, QObject* parent,
    std::function<QDateTime()> currentTime)
    : QObject(parent)
    , m_settingsPath(QDir(dataDirectory).filePath(QStringLiteral("backup.ini")))
    , m_endpoint(UpdateChecker::updateServiceBaseUrl())
    , m_currentTime(currentTime ? currentTime : [] { return QDateTime::currentDateTime(); })
{
    // 局域网备份直接连接已配置服务器，凭证不经过系统代理。
    m_network.setProxy(QNetworkProxy::NoProxy);
    if (!QDir().mkpath(dataDirectory)) {
        m_identityError = QStringLiteral("无法创建备份配置目录。");
    } else {
        initializeIdentity();
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    m_enabled = settings.value(QStringLiteral("backup/enabled"), false).toBool();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { checkDue(); scheduleCheck(); });
    QTimer::singleShot(0, this, [this] { checkDue(); scheduleCheck(); });
}

void BackupClient::initializeIdentity()
{
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    m_device = settings.value(QStringLiteral("identity/device")).toString();
    m_key = settings.value(QStringLiteral("identity/key")).toString();
    if (m_device.isEmpty() && m_key.isEmpty()) {
        QByteArray machine = QSysInfo::machineUniqueId();
#ifdef Q_OS_WIN
        if (machine.isEmpty()) {
            QSettings machineSettings(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"),
                QSettings::NativeFormat);
            machine = machineSettings.value(QStringLiteral("MachineGuid")).toByteArray();
        }
#endif
        if (machine.isEmpty()) {
            machine = QUuid::createUuid().toByteArray();
        }
        // 机器码仅参与散列；按用户数据目录隔离同一机器上的不同用户。
        m_device = hash(QByteArray("AttendanceApp backup v1:") + machine
            + QDir::fromNativeSeparators(QFileInfo(m_settingsPath).absolutePath()).toUtf8());
        QByteArray secret;
        for (int i = 0; i < 4; ++i) {
            const quint64 value = QRandomGenerator::system()->generate64();
            secret.append(reinterpret_cast<const char*>(&value), sizeof(value));
        }
        m_key = QString::fromLatin1(secret.toHex());
        settings.setValue(QStringLiteral("identity/device"), m_device);
        settings.setValue(QStringLiteral("identity/key"), m_key);
        settings.sync();
    }
    if (!validCredential(m_device) || !validCredential(m_key)
        || settings.status() != QSettings::NoError) {
        m_identityError = QStringLiteral("备份身份配置缺失或无法保存，请保留并检查 backup.ini。");
    }
}

QString BackupClient::endpointSettingsKey() const
{
    return QStringLiteral("servers/") + hash(m_endpoint.toEncoded());
}

QString BackupClient::statusText() const
{
    if (!m_identityError.isEmpty()) {
        return m_identityError;
    }
    if (!m_status.isEmpty()) {
        return m_status;
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    const QString last = settings.value(endpointSettingsKey() + QStringLiteral("/lastSuccess")).toString();
    return last.isEmpty() ? QStringLiteral("尚未备份") : QStringLiteral("上次成功备份：%1").arg(last);
}

void BackupClient::setStatus(const QString& text)
{
    m_status = text;
    emit statusChanged(statusText());
}

void BackupClient::setEndpoint(const QUrl& endpoint)
{
    if (endpoint == m_endpoint) {
        return;
    }
    m_endpoint = endpoint;
    m_status.clear();
    m_nextRetry = {};
    emit statusChanged(statusText());
    checkDue();
}

void BackupClient::setEnabled(bool enabled)
{
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("backup/enabled"), enabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        emit operationFailed(QStringLiteral("无法保存自动备份开关。"));
        emit enabledChanged(m_enabled);
        return;
    }
    const bool newlyEnabled = enabled && !m_enabled;
    m_enabled = enabled;
    emit enabledChanged(enabled);
    if (newlyEnabled) {
        backupNow();
    }
}

void BackupClient::scheduleCheck()
{
    const QDateTime now = m_currentTime();
    const QDateTime midnight(now.date().addDays(1), QTime(0, 0));
    m_timer.start(int(qBound<qint64>(1, now.msecsTo(midnight), 60000)));
}

void BackupClient::checkDue()
{
    if (!m_enabled || m_uploading || (!m_nextRetry.isNull()
        && m_currentTime() < m_nextRetry)) {
        return;
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    if (settings.value(endpointSettingsKey() + QStringLiteral("/day")).toString()
        != m_currentTime().date().toString(Qt::ISODate)) {
        backupNow();
    }
}

QNetworkRequest BackupClient::request(const QString& path) const
{
    QNetworkRequest result(m_endpoint.resolved(QUrl(path)));
    result.setRawHeader("X-Backup-Device", m_device.toLatin1());
    result.setRawHeader("X-Backup-Key", m_key.toLatin1());
    result.setRawHeader("Cache-Control", "no-store");
    // 带凭证的请求不能跟随重定向，防止把本机密钥发送到其他主机。
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return result;
}

void BackupClient::armTimeout(QNetworkReply* reply)
{
    QTimer::singleShot(60000, reply, [reply] {
        if (!reply->isFinished()) {
            reply->setProperty("backupTimedOut", true);
            reply->abort();
        }
    });
}

QString BackupClient::replyError(QNetworkReply* reply, const QByteArray& data) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status == 200) {
        return {};
    }
    const QString detail = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toString();
    if (!detail.isEmpty()) {
        return detail;
    }
    return reply->property("backupTimedOut").toBool()
        ? QStringLiteral("备份请求超时，请检查服务器连接。")
        : QStringLiteral("备份服务请求失败（HTTP %1）：%2").arg(status).arg(reply->errorString());
}

void BackupClient::backupNow()
{
    if (!m_enabled || m_uploading) {
        return;
    }
    m_nextRetry = m_currentTime().addSecs(300);
    if (!m_identityError.isEmpty()) {
        setStatus(m_identityError);
        return;
    }
    QString error;
    const QByteArray data = AttendanceStorage::createBackup(error);
    if (data.isEmpty() || data.size() > 20 * 1024 * 1024) {
        setStatus(error.isEmpty() ? QStringLiteral("备份超过 20 MB，未上传。") : error);
        return;
    }
    m_uploading = true;
    setStatus(QStringLiteral("正在备份全部考勤数据…"));
    QNetworkRequest upload = request(QStringLiteral("/api/backups"));
    upload.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    QNetworkReply* reply = m_network.put(upload, data);
    armTimeout(reply);
    const QString settingsKey = endpointSettingsKey();
    const QString backupDay = m_currentTime().date().toString(Qt::ISODate);
    connect(reply, &QNetworkReply::finished, this, [this, reply, settingsKey, backupDay] {
        m_uploading = false;
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        reply->deleteLater();
        if (!error.isEmpty()) {
            setStatus(QStringLiteral("备份失败：%1（五分钟后自动重试）").arg(error));
            return;
        }
        const QJsonObject response = QJsonDocument::fromJson(body).object();
        if (!QDate::fromString(response.value(QStringLiteral("date")).toString(), Qt::ISODate).isValid()) {
            setStatus(QStringLiteral("备份服务返回了无效响应，将自动重试。"));
            return;
        }
        QSettings settings(m_settingsPath, QSettings::IniFormat);
        settings.setValue(settingsKey + QStringLiteral("/day"), backupDay);
        settings.setValue(settingsKey + QStringLiteral("/lastSuccess"),
            m_currentTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        settings.sync();
        m_status.clear();
        if (settings.status() != QSettings::NoError) {
            setStatus(QStringLiteral("备份已上传，但无法保存本地成功状态。"));
        } else {
            m_nextRetry = {};
            emit statusChanged(statusText());
        }
        checkDue();
    });
}

void BackupClient::listBackups()
{
    if (m_browsing) {
        return;
    }
    if (!m_identityError.isEmpty()) {
        emit operationFailed(m_identityError);
        return;
    }
    m_browsing = true;
    QNetworkReply* reply = m_network.get(request(QStringLiteral("/api/backups")));
    armTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_browsing = false;
        const QByteArray data = reply->readAll();
        const QString error = replyError(reply, data);
        reply->deleteLater();
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!error.isEmpty() || !document.isObject()
            || !document.object().value(QStringLiteral("backups")).isArray()) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("服务器备份列表格式无效。") : error);
            return;
        }
        emit backupsListed(document.object().value(QStringLiteral("backups")).toArray());
    });
}

void BackupClient::downloadBackup(const QString& day, const QString& destination)
{
    const QDate date = QDate::fromString(day, Qt::ISODate);
    if (!date.isValid() || date.toString(Qt::ISODate) != day || m_browsing) {
        return;
    }
    if (!m_identityError.isEmpty()) {
        emit operationFailed(m_identityError);
        return;
    }
    if (QSqlDatabase::contains(QStringLiteral("attendance-storage"))) {
        const QFileInfo live(QSqlDatabase::database(QStringLiteral("attendance-storage")).databaseName());
        const QFileInfo target(destination);
        if (!live.canonicalFilePath().isEmpty() && live.canonicalFilePath() == target.canonicalFilePath()) {
            emit operationFailed(QStringLiteral("不能覆盖正在使用的考勤数据库，请另选保存位置。"));
            return;
        }
    }
    m_browsing = true;
    QNetworkReply* reply = m_network.get(request(QStringLiteral("/api/backups/") + day));
    armTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, destination] {
        m_browsing = false;
        const QByteArray data = reply->readAll();
        QString error = replyError(reply, data);
        reply->deleteLater();
        if (error.isEmpty() && (data.size() < 100 || data.size() > 20 * 1024 * 1024
            || !data.startsWith(QByteArray("SQLite format 3\0", 16)))) {
            error = QStringLiteral("下载内容不是有效的数据库备份。请检查服务器。");
        }
        if (!error.isEmpty()) {
            emit operationFailed(error);
            return;
        }
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            emit operationFailed(QStringLiteral("无法保存备份文件，请检查目标路径。"));
            return;
        }
        emit backupDownloaded(destination);
    });
}
