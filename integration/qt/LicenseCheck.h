#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace license_public {

struct LicenseCheckRequest {
    QString iniFilePath;
    QString deviceFingerprint;
    QString machineName;
    QString osName;
    QString appVersion;
    QString clientNote;
    int timeoutMs = 10000;
};

struct LicenseCheckResult {
    int httpStatus = 0;
    int code = 0;
    qint64 applyCount = 0;
    QString message;
    QString requestId;
    QString licenseId;
    QString licenseStatus;
    QString expiresAt;
    QString result;
    QString boundDeviceFingerprint;
    QString errorText;
    QString rawBody;

    bool isAccepted() const;
};

class LicenseCheck {
public:
    static bool check(const LicenseCheckRequest &request, QString &errorMessage, LicenseCheckResult *result = nullptr);
    static bool check(QString &errorMessage,
                      LicenseCheckResult *result = nullptr,
                      const QString &iniFilePath = QString());
    static QString defaultDeviceFingerprint();
    static QString defaultIniFilePath();

private:
    static bool loadIniConfig(const QString &iniFilePath, QString &serverBaseUrl, QString &licenseKey, QString &errorMessage);
    static QByteArray buildRequestBody(const LicenseCheckRequest &request, const QString &licenseKey);
    static LicenseCheckResult performCheck(const LicenseCheckRequest &request);
};

}  // namespace license_public
