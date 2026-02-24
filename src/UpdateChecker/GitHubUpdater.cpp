#include "GitHubUpdater.h"
#include <QMessageBox>
#include <QDebug>
#include <QUrl>
#include <QFileInfo>

GitHubUpdater::GitHubUpdater(const QString& repoOwner,
    const QString& repoName,
    const QString& currentVersion,
    QObject* parent)
    : QObject(parent)
    , m_repoOwner(repoOwner)
    , m_repoName(repoName)
    , m_currentVersion(currentVersion)
{
    m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (m_downloadDir.isEmpty()) {
        m_downloadDir = QDir::homePath();
    }
}

void GitHubUpdater::setDownloadDir(const QString& dir)
{
    m_downloadDir = dir;
}

void GitHubUpdater::checkForUpdates()
{
    QString apiUrl = QString("https://api.github.com/repos/%1/%2/releases/latest")
        .arg(m_repoOwner, m_repoName);
    QNetworkRequest request(apiUrl);
    request.setRawHeader("User-Agent", "QtApp-Updater");

    QNetworkReply* reply = m_netManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onCheckReplyFinished(reply);
        });

    //connect(reply, &QNetworkReply::errorOccurred, this, [this, reply]() {
    //    onCheckReplyFinished(reply);
    //    });
}

void GitHubUpdater::onCheckReplyFinished(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("网络错误: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    // 打印原始响应内容
    //qDebug() << "Raw Response:" << data;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit errorOccurred("JSON 解析失败");
        reply->deleteLater();
        return;
    }

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QString key = it.key();
        QJsonValue value = it.value();

        qDebug() << key << ":" << value;
    }

    QString tagName = root["tag_name"].toString();
    // 移除可能的 'v' 前缀，如 v1.2.0 → 1.2.0
    if (tagName.startsWith('v') || tagName.startsWith('V')) {
        tagName = tagName.mid(1);
    }

    QVersionNumber latestVer = QVersionNumber::fromString(tagName);
    QVersionNumber currentVer = QVersionNumber::fromString(m_currentVersion);

    if (latestVer > currentVer) {
        QString changelog = root["body"].toString();
        QString downloadUrl = findDownloadUrl(root);

        if (downloadUrl.isEmpty()) {
            emit errorOccurred("未找到 Windows 安装包（请确保 Release 中包含 .exe 文件）");
            reply->deleteLater();
            return;
        }

        emit updateAvailable(tagName, changelog);
        startDownload(QUrl(downloadUrl));
    }
    else {
        emit noUpdateAvailable();
    }

    reply->deleteLater();
}

QString GitHubUpdater::findDownloadUrl(const QJsonObject& releaseObj)
{
    // 优先从 assets 中找 .exe 文件（可按关键词过滤）
    QJsonArray assets = releaseObj["assets"].toArray();
    for (const QJsonValue& val : assets) {
        QJsonObject asset = val.toObject();
        QString name = asset["name"].toString();
        qDebug() << "=====NAME: "<<name;

        if (name.endsWith(".exe", Qt::CaseInsensitive)) {
            // 可进一步匹配 "setup", "installer", 项目名等
            //if (name.contains("setup", Qt::CaseInsensitive) ||
            //    name.contains("installer", Qt::CaseInsensitive) ||
            //    name.contains(m_repoName, Qt::CaseInsensitive)) {
                return asset["browser_download_url"].toString();
            //}
        }
    }
    return {}; // 未找到
}

void GitHubUpdater::startDownload(const QUrl& url)
{
    qDebug() << "[Download] Url: " << url;

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_netManager.get(request);

    // 连接 finished 信号到处理函数
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onDownloadFinished(reply);
        });

    // 超时控制
    QTimer* timer = new QTimer();
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, [=]() {
        if (!reply->isFinished()) {
            reply->abort();
            emit errorOccurred("下载超时");
        }
        });
    connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    connect(reply, &QNetworkReply::finished, timer, &QObject::deleteLater);
    timer->start(30000); // 30秒超时
}

void GitHubUpdater::onDownloadFinished(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("下载失败: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    // 获取文件名
    QString contentDisposition = reply->rawHeader("Content-Disposition");
    QString fileName;
    if (!contentDisposition.isEmpty()) {
        // 简单解析 filename="xxx"
        int pos = contentDisposition.indexOf("filename=");
        if (pos != -1) {
            fileName = contentDisposition.mid(pos + 9).remove('"').trimmed();
        }
    }
    if (fileName.isEmpty()) {
        fileName = QFileInfo(reply->url().path()).fileName();
    }
    if (fileName.isEmpty()) {
        fileName = "update.exe";
    }

    m_downloadFileName = QDir(m_downloadDir).filePath(fileName);
    QFile file(m_downloadFileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();
        emit updateDownloaded(m_downloadFileName);
    }
    else {
        emit errorOccurred("无法保存文件: " + m_downloadFileName);
    }

    reply->deleteLater();
}
