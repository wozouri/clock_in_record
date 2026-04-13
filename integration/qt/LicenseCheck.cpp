#include "LicenseCheck.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace license_public {

namespace {

QString normalizedBaseUrl(const QString &serverBaseUrl) {
    QString normalized = serverBaseUrl.trimmed();
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

qint64 jsonInt64(const QJsonObject &object, const char *key) {
    return static_cast<qint64>(object.value(QLatin1String(key)).toDouble());
}

QString jsonString(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}

QNetworkRequest buildNetworkRequest(const QString &serverBaseUrl) {
    const QUrl url(normalizedBaseUrl(serverBaseUrl) + QStringLiteral("/api/client/activate"));

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return networkRequest;
}

LicenseCheckResult buildResponse(QNetworkReply *reply) {
    LicenseCheckResult response;
    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.errorText = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    const QByteArray rawBody = reply->readAll();
    response.rawBody = QString::fromUtf8(rawBody);

    if (rawBody.trimmed().isEmpty()) {
        if (response.message.isEmpty()) {
            response.message = response.errorText.isEmpty() ? QStringLiteral("empty_response") : QStringLiteral("network_error");
        }
        return response;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(rawBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (response.message.isEmpty()) {
            response.message = QStringLiteral("invalid_json_response");
        }
        if (response.errorText.isEmpty()) {
            response.errorText = parseError.errorString();
        }
        return response;
    }

    const auto root = document.object();
    response.code = root.value(QStringLiteral("code")).toInt();
    response.message = root.value(QStringLiteral("message")).toString();

    const auto data = root.value(QStringLiteral("data")).toObject();
    response.requestId = jsonString(data, "request_id");
    response.licenseId = jsonString(data, "license_id");
    response.licenseStatus = jsonString(data, "license_status");
    response.expiresAt = jsonString(data, "expires_at");
    response.result = jsonString(data, "result");
    response.applyCount = jsonInt64(data, "apply_count");
    response.payloadJson = jsonString(data, "payload_json");
    response.signatureText = jsonString(data, "signature_text");
    response.signatureAlgorithm = jsonString(data, "signature_algorithm");
    response.boundDeviceFingerprint = jsonString(data, "bound_device_fingerprint");
    return response;
}

QString buildErrorMessage(const LicenseCheckResult &result) {
    if (!result.errorText.isEmpty()) {
        if (result.httpStatus > 0) {
            return QStringLiteral("http %1: %2 (%3)")
                .arg(result.httpStatus)
                .arg(result.message.isEmpty() ? QStringLiteral("request_failed") : result.message)
                .arg(result.errorText);
        }
        return result.errorText;
    }

    if (!result.message.isEmpty()) {
        return result.message;
    }

    if (result.httpStatus > 0) {
        return QStringLiteral("http %1").arg(result.httpStatus);
    }

    return QStringLiteral("license_check_failed");
}

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

QString openSslErrorString() {
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        return QStringLiteral("openssl error");
    }

    char buffer[256] = {};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return QString::fromLatin1(buffer);
}

int fromHexNibble(const QChar ch) {
    if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
        return ch.toLatin1() - '0';
    }
    if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) {
        return 10 + (ch.toLatin1() - 'a');
    }
    if (ch >= QLatin1Char('A') && ch <= QLatin1Char('F')) {
        return 10 + (ch.toLatin1() - 'A');
    }
    return -1;
}

QByteArray fromHex(const QString &text, QString &errorMessage) {
    if (text.size() % 2 != 0) {
        errorMessage = QStringLiteral("invalid signature hex length");
        return {};
    }

    QByteArray bytes;
    bytes.resize(text.size() / 2);
    for (int index = 0; index < text.size(); index += 2) {
        const int high = fromHexNibble(text[index]);
        const int low = fromHexNibble(text[index + 1]);
        if (high < 0 || low < 0) {
            errorMessage = QStringLiteral("invalid signature hex character");
            return {};
        }
        bytes[index / 2] = static_cast<char>((high << 4) | low);
    }
    return bytes;
}

bool verifyEd25519Signature(const QString &publicKeyPath,
                            const QString &payloadJson,
                            const QString &signatureHex,
                            QString &errorMessage) {
    QFile publicKeyFile(publicKeyPath);
    if (!publicKeyFile.exists()) {
        errorMessage = QStringLiteral("missing public key file: %1").arg(publicKeyPath);
        return false;
    }
    if (!publicKeyFile.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("failed to open public key file: %1").arg(publicKeyPath);
        return false;
    }

    const QByteArray publicKeyPem = publicKeyFile.readAll();
    const QByteArray signature = fromHex(signatureHex, errorMessage);
    if (signature.isEmpty() && !signatureHex.isEmpty()) {
        return false;
    }

    BioPtr bio(BIO_new_mem_buf(publicKeyPem.constData(), static_cast<int>(publicKeyPem.size())), &BIO_free);
    if (!bio) {
        errorMessage = QStringLiteral("failed to create BIO for public key");
        return false;
    }

    EvpPkeyPtr pkey(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), &EVP_PKEY_free);
    if (!pkey) {
        errorMessage = QStringLiteral("failed to read Ed25519 public key: %1").arg(openSslErrorString());
        return false;
    }
    if (EVP_PKEY_base_id(pkey.get()) != EVP_PKEY_ED25519) {
        errorMessage = QStringLiteral("public key is not an Ed25519 key");
        return false;
    }

    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx) {
        errorMessage = QStringLiteral("failed to allocate OpenSSL digest context");
        return false;
    }

    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1) {
        errorMessage = QStringLiteral("failed to initialize Ed25519 verifier: %1").arg(openSslErrorString());
        return false;
    }

    const QByteArray payloadBytes = payloadJson.toUtf8();
    const int verified = EVP_DigestVerify(
        ctx.get(),
        reinterpret_cast<const unsigned char *>(signature.constData()),
        static_cast<size_t>(signature.size()),
        reinterpret_cast<const unsigned char *>(payloadBytes.constData()),
        static_cast<size_t>(payloadBytes.size()));

    if (verified == 1) {
        return true;
    }
    if (verified == 0) {
        errorMessage = QStringLiteral("license signature verification failed");
        return false;
    }

    errorMessage = QStringLiteral("license signature verification error: %1").arg(openSslErrorString());
    return false;
}

}  // namespace

bool LicenseCheckResult::isAccepted() const {
    return httpStatus == 200 && code == 200 && result == QLatin1String("accepted");
}

bool LicenseCheck::check(const LicenseCheckRequest &request, QString &errorMessage, LicenseCheckResult *result) {
    const auto response = performCheck(request);
    if (result != nullptr) {
        *result = response;
    }

    if (!response.isAccepted()) {
        errorMessage = buildErrorMessage(response);
        return false;
    }

    if (!verifyAcceptedSignature(request, response, errorMessage)) {
        return false;
    }

    errorMessage.clear();
    return true;
}

bool LicenseCheck::check(QString &errorMessage, LicenseCheckResult *result, const QString &iniFilePath) {
    LicenseCheckRequest request;
    request.iniFilePath = iniFilePath;
    return check(request, errorMessage, result);
}

QString LicenseCheck::defaultIniFilePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("license_public.ini"));
}

QString LicenseCheck::defaultPublicKeyPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("license_ed25519_public.pem"));
}

QString LicenseCheck::defaultDeviceFingerprint() {
    QByteArray machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) {
        const QString fallbackSeed = QSysInfo::machineHostName()
            + QLatin1Char('|')
            + QSysInfo::prettyProductName()
            + QLatin1Char('|')
            + QSysInfo::currentCpuArchitecture()
            + QLatin1Char('|')
            + QCoreApplication::applicationName();
        machineId = fallbackSeed.toUtf8();
    }

    return QString::fromLatin1(QCryptographicHash::hash(machineId, QCryptographicHash::Sha256).toHex());
}

bool LicenseCheck::loadIniConfig(const QString &iniFilePath, QString &serverBaseUrl, QString &licenseKey, QString &errorMessage) {
    const QString resolvedIniPath = iniFilePath.trimmed().isEmpty() ? defaultIniFilePath() : iniFilePath;

    QSettings settings(resolvedIniPath, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings.setIniCodec("UTF-8");
#endif

    settings.beginGroup(QStringLiteral("license_public"));
    serverBaseUrl = settings.value(QStringLiteral("server_base_url")).toString().trimmed();
    licenseKey = settings.value(QStringLiteral("license_key")).toString().trimmed();
    settings.endGroup();

    if (serverBaseUrl.isEmpty()) {
        serverBaseUrl = settings.value(QStringLiteral("server_base_url")).toString().trimmed();
    }
    if (licenseKey.isEmpty()) {
        licenseKey = settings.value(QStringLiteral("license_key")).toString().trimmed();
    }

    if (serverBaseUrl.isEmpty()) {
        errorMessage = QStringLiteral("missing server_base_url in %1").arg(resolvedIniPath);
        return false;
    }
    if (licenseKey.isEmpty()) {
        errorMessage = QStringLiteral("missing license_key in %1").arg(resolvedIniPath);
        return false;
    }
    if (!serverBaseUrl.startsWith(QStringLiteral("https://")) && !serverBaseUrl.startsWith(QStringLiteral("http://"))) {
        errorMessage = QStringLiteral("invalid server_base_url in %1").arg(resolvedIniPath);
        return false;
    }
    return true;
}

QByteArray LicenseCheck::buildRequestBody(const LicenseCheckRequest &request, const QString &licenseKey) {
    QJsonObject body;
    body[QStringLiteral("license_key")] = licenseKey;
    body[QStringLiteral("device_fingerprint")] = request.deviceFingerprint.isEmpty()
        ? defaultDeviceFingerprint()
        : request.deviceFingerprint;
    body[QStringLiteral("machine_name")] = request.machineName.isEmpty() ? QSysInfo::machineHostName() : request.machineName;
    body[QStringLiteral("os_name")] = request.osName.isEmpty() ? QSysInfo::prettyProductName() : request.osName;
    body[QStringLiteral("app_version")] = request.appVersion;
    body[QStringLiteral("client_note")] = request.clientNote;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

LicenseCheckResult LicenseCheck::performCheck(const LicenseCheckRequest &request) {
    QString iniLoadError;
    QString serverBaseUrl;
    QString licenseKey;
    if (!loadIniConfig(request.iniFilePath, serverBaseUrl, licenseKey, iniLoadError)) {
        LicenseCheckResult response;
        response.message = QStringLiteral("ini_config_error");
        response.errorText = iniLoadError;
        return response;
    }

    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply = network.post(buildNetworkRequest(serverBaseUrl), buildRequestBody(request, licenseKey));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        if (reply != nullptr) {
            reply->abort();
        }
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(request.timeoutMs);
    loop.exec();

    LicenseCheckResult response;
    if (reply == nullptr) {
        response.message = QStringLiteral("request_aborted");
        response.errorText = QStringLiteral("reply_deleted");
        return response;
    }

    response = buildResponse(reply);
    if (!timer.isActive() && response.httpStatus == 0 && response.errorText.isEmpty()) {
        response.message = QStringLiteral("request_timeout");
        response.errorText = QStringLiteral("license check timed out");
    }

    reply->deleteLater();
    return response;
}

bool LicenseCheck::verifyAcceptedSignature(const LicenseCheckRequest &request,
                                          const LicenseCheckResult &result,
                                          QString &errorMessage) {
    if (result.signatureAlgorithm != QLatin1String("Ed25519")) {
        errorMessage = QStringLiteral("unexpected signature algorithm: %1").arg(result.signatureAlgorithm);
        return false;
    }
    if (result.payloadJson.isEmpty() || result.signatureText.isEmpty()) {
        errorMessage = QStringLiteral("missing signed license payload in activation response");
        return false;
    }

    const QString publicKeyPath = request.publicKeyPath.trimmed().isEmpty()
        ? defaultPublicKeyPath()
        : request.publicKeyPath.trimmed();

    return verifyEd25519Signature(publicKeyPath, result.payloadJson, result.signatureText, errorMessage);
}

}  // namespace license_public
