#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QByteArray>
#include <QObject>
#include <QSaveFile>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

struct UpdateReleaseInfo {
    bool available = false;
    QString version;
    QString notes;
    QUrl downloadUrl;
    QString sha256;
    bool isNewer = false;
    QString errorMessage;
};

// 参考 LUBAN launcher 的客户端更新流程：
// 清单检查 -> 下载 -> SHA-256 校验 -> 解压 -> 延迟替换脚本 -> 重启生效。
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void setServiceBaseUrl(const QUrl& url);
    QUrl serviceBaseUrl() const;

    void checkForUpdates(bool userInitiated);
    void cancelDownload();
    void startDownload();

    // 读取并清除"待更新版本"标记，用于重启后提示更新完成。
    static QString takePendingUpdateVersion();
    static QString updateServiceBaseUrl();

    bool isDownloadInProgress() const;

signals:
    void checkFinished(const UpdateReleaseInfo& info, bool userInitiated);
    void downloadProgress(int percent);
    void applyReady(const QString& version);
    void failed(const QString& message);

private:
    void handleCheckReply(QNetworkReply* reply, bool userInitiated);
    void handleDownloadFinished(QNetworkReply* reply);
    void verifyPackage();
    void extractPackage();
    void launchApplyScript(const QString& extractDir);
    void abortWithError(const QString& message);
    QNetworkReply* createGetReply(const QUrl& url);

    QNetworkAccessManager* m_network = nullptr;
    QUrl m_baseUrl;
    QTimer* m_inactivityTimer = nullptr;
    QNetworkReply* m_checkReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    QProcess* m_extractProcess = nullptr;
    QSaveFile m_packageFile;
    UpdateReleaseInfo m_releaseInfo;
    QString m_packagePath;
    QString m_tempDirectory;
    bool m_userInitiated = false;
    bool m_downloadInProgress = false;
    bool m_failureReported = false;
    qint64 m_bytesWritten = 0;
};

#endif  // UPDATECHECKER_H
