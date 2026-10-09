#include "BackupStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
bool validIdentity(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("^[a-f0-9]{64}$"));
    return expression.match(text).hasMatch();
}

bool validDay(const QString& text)
{
    const QDate date = QDate::fromString(text, Qt::ISODate);
    return date.isValid() && date.toString(Qt::ISODate) == text;
}

bool saveAtomically(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
} // namespace

void BackupStore::setRoot(const QString& root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root = QDir(root).absolutePath();
}

QString BackupStore::devicePath(const QString& device) const
{
    return QDir(m_root).filePath(device);
}

BackupResult BackupStore::authorize(const QString& device, const QString& key, bool enroll)
{
    if (!validIdentity(device) || !validIdentity(key)) {
        return {400, QStringLiteral("备份身份格式无效。")};
    }
    const QString directory = devicePath(device);
    if (QFileInfo(directory).isSymLink()) {
        return {403, QStringLiteral("备份目录无效。")};
    }
    const QString credentialPath = QDir(directory).filePath(QStringLiteral("credential.sha256"));
    const QByteArray digest = QCryptographicHash::hash(key.toLatin1(), QCryptographicHash::Sha256).toHex();
    QFile credential(credentialPath);
    if (credential.exists()) {
        if (QFileInfo(credential).isSymLink() || !credential.open(QIODevice::ReadOnly)) {
            return {500, QStringLiteral("无法读取备份凭证。")};
        }
        const QByteArray expected = credential.readAll();
        unsigned int difference = unsigned(expected.size() ^ digest.size());
        for (int i = 0; i < qMin(expected.size(), digest.size()); ++i) {
            difference |= static_cast<unsigned char>(expected[i] ^ digest[i]);
        }
        if (difference != 0) {
            return {403, QStringLiteral("备份凭证不匹配，只能访问本客户端的备份。")};
        }
    } else {
        if (!enroll) {
            return {404, QStringLiteral("本客户端尚无备份。")};
        }
        if (!QDir().mkpath(directory) || !saveAtomically(credentialPath, digest)) {
            return {500, QStringLiteral("无法保存备份凭证。")};
        }
    }
    return {};
}

void BackupStore::pruneDevice(const QString& device, const QDate& today)
{
    QDir directory(devicePath(device));
    for (const QString& name : directory.entryList({QStringLiteral("*.db")}, QDir::Files)) {
        const QString day = name.left(name.size() - 3);
        if (validDay(day) && QDate::fromString(day, Qt::ISODate) < today.addDays(-2)) {
            directory.remove(name);
        }
    }
}

BackupResult BackupStore::upload(const QString& device, const QString& key, const QByteArray& data,
    const QDate& today)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (data.size() > maxBackupBytes) {
        return {413, QStringLiteral("备份文件超过 20 MB 上限。")};
    }
    if (data.size() < 100 || !data.startsWith(QByteArray("SQLite format 3\0", 16))) {
        return {400, QStringLiteral("备份不是有效的 SQLite 数据库文件。")};
    }
    BackupResult result = authorize(device, key, true);
    if (result.status != 200) {
        return result;
    }
    const QString path = QDir(devicePath(device)).filePath(today.toString(Qt::ISODate) + QStringLiteral(".db"));
    if (!saveAtomically(path, data)) {
        return {500, QStringLiteral("无法保存备份文件。")};
    }
    pruneDevice(device, today);
    return {};
}

BackupResult BackupStore::list(const QString& device, const QString& key, const QDate& today)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    BackupResult result = authorize(device, key, false);
    if (result.status != 200) {
        return result;
    }
    pruneDevice(device, today);
    QDir directory(devicePath(device));
    for (const QFileInfo& file : directory.entryInfoList({QStringLiteral("*.db")}, QDir::Files,
             QDir::Name | QDir::Reversed)) {
        const QString day = file.completeBaseName();
        if (!file.isSymLink() && validDay(day)
            && QDate::fromString(day, Qt::ISODate) <= today) {
            result.entries.append(QJsonObject{{QStringLiteral("date"), day},
                {QStringLiteral("size"), double(file.size())}});
        }
    }
    return result;
}

BackupResult BackupStore::download(const QString& device, const QString& key, const QString& date,
    const QDate& today)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!validDay(date)) {
        return {400, QStringLiteral("备份日期无效。")};
    }
    BackupResult result = authorize(device, key, false);
    if (result.status != 200) {
        return result;
    }
    pruneDevice(device, today);
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (day < today.addDays(-2) || day > today) {
        return {404, QStringLiteral("备份已过期或不存在。")};
    }
    QFile file(QDir(devicePath(device)).filePath(date + QStringLiteral(".db")));
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly)) {
        return {404, QStringLiteral("备份不存在。")};
    }
    result.data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return {500, QStringLiteral("读取备份失败。")};
    }
    return result;
}

void BackupStore::prune(const QDate& today)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const QFileInfo& directory : QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (validIdentity(directory.fileName()) && !directory.isSymLink()) {
            pruneDevice(directory.fileName(), today);
        }
    }
}
