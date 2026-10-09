#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

class QNetworkReply;
class QNetworkRequest;

class BackupClient : public QObject
{
    Q_OBJECT
public:
    explicit BackupClient(const QString& dataDirectory, QObject* parent = nullptr,
        std::function<QDateTime()> currentTime = {});
    bool isEnabled() const { return m_enabled; }
    QString deviceId() const { return m_device; }
    QString statusText() const;
    void setEndpoint(const QUrl& endpoint);
    void setEnabled(bool enabled);
    void backupNow();
    void listBackups();
    void downloadBackup(const QString& day, const QString& destination);

signals:
    void statusChanged(const QString& text);
    void enabledChanged(bool enabled);
    void backupsListed(const QJsonArray& entries);
    void backupDownloaded(const QString& destination);
    void operationFailed(const QString& message);

private:
    QNetworkRequest request(const QString& path) const;
    void initializeIdentity();
    void checkDue();
    void scheduleCheck();
    void armTimeout(QNetworkReply* reply);
    QString replyError(QNetworkReply* reply, const QByteArray& data) const;
    QString endpointSettingsKey() const;
    void setStatus(const QString& text);

    QString m_settingsPath;
    QString m_device;
    QString m_key;
    QString m_identityError;
    QString m_status;
    bool m_enabled = false;
    bool m_uploading = false;
    bool m_browsing = false;
    QUrl m_endpoint;
    QDateTime m_nextRetry;
    QTimer m_timer;
    QNetworkAccessManager m_network;
    std::function<QDateTime()> m_currentTime;
};
