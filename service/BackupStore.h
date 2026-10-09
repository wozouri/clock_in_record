#pragma once

#include <QDate>
#include <QJsonArray>
#include <QString>
#include <mutex>

struct BackupResult {
    int status = 200;
    QString error;
    QByteArray data;
    QJsonArray entries;
};

class BackupStore {
public:
    void setRoot(const QString& root);
    BackupResult upload(const QString& device, const QString& key, const QByteArray& data,
        const QDate& today = QDate::currentDate());
    BackupResult list(const QString& device, const QString& key,
        const QDate& today = QDate::currentDate());
    BackupResult download(const QString& device, const QString& key, const QString& date,
        const QDate& today = QDate::currentDate());
    void prune(const QDate& today = QDate::currentDate());
    static constexpr int maxBackupBytes = 20 * 1024 * 1024;

private:
    BackupResult authorize(const QString& device, const QString& key, bool enroll);
    void pruneDevice(const QString& device, const QDate& today);
    QString devicePath(const QString& device) const;
    QString m_root;
    std::mutex m_mutex;
};
