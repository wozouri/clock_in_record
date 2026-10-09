#include "BackupStore.h"
#include "Backup/BackupClient.h"
#include "Data/AttendanceStorage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class BackupTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_workspace;
    QTemporaryDir m_serverDirectory;
    QProcess m_server;
    QUrl m_endpoint;
    QByteArray m_snapshot;

    struct Response { int status; QByteArray body; };
    Response http(const QString& method, const QString& path, const QString& device,
        const QString& key, const QByteArray& body = {})
    {
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        QNetworkRequest request(m_endpoint.resolved(QUrl(path)));
        request.setRawHeader("X-Backup-Device", device.toLatin1());
        request.setRawHeader("X-Backup-Key", key.toLatin1());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        QNetworkReply* reply = method == QStringLiteral("PUT") ? manager.put(request, body) : manager.get(request);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(5000);
        if (!reply->isFinished()) {
            loop.exec();
        }
        return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
    }

    void startServer()
    {
        m_server.start(m_serverDirectory.filePath(QStringLiteral("AttendanceUpdateService.exe")),
            {QStringLiteral("--console")});
        QVERIFY(m_server.waitForStarted());
        QElapsedTimer deadline;
        deadline.start();
        int status = 0;
        do {
            status = http(QStringLiteral("GET"), QStringLiteral("/health"), {}, {}).status;
            if (status == 200) {
                break;
            }
        } while (deadline.elapsed() < 10000 && m_server.state() == QProcess::Running);
        QVERIFY2(status == 200, m_server.readAllStandardError().constData());
    }

    void stopServer()
    {
        m_server.kill();
        m_server.waitForFinished(5000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_workspace.isValid());
        QVERIFY(m_serverDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("AttendanceBackupTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AttendanceBackupTests"));
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("attendance-storage"));
        database.setDatabaseName(m_workspace.filePath(QStringLiteral("live.db")));
        QVERIFY(database.open());


        const QString executable = qEnvironmentVariable("ATTENDANCE_BACKUP_TEST_SERVICE");
        QVERIFY2(QFile::exists(executable), "Set ATTENDANCE_BACKUP_TEST_SERVICE to the compiled service executable");
        QVERIFY(QFile::copy(executable, m_serverDirectory.filePath(QStringLiteral("AttendanceUpdateService.exe"))));
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 port = reservation.serverPort();
        reservation.close();
        m_endpoint = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        QSettings config(m_serverDirectory.filePath(QStringLiteral("updateservice.ini")), QSettings::IniFormat);
        config.setValue(QStringLiteral("service/host"), QStringLiteral("127.0.0.1"));
        config.setValue(QStringLiteral("service/port"), port);
        config.setValue(QStringLiteral("service/root"), m_serverDirectory.filePath(QStringLiteral("updates")));
        config.setValue(QStringLiteral("service/backupRoot"), m_serverDirectory.filePath(QStringLiteral("backups")));
        config.sync();
        startServer();
    }

    void completeDatabaseSnapshot()
    {
        WorkSchedule schedule;
        schedule.mealAllowanceTime = QTime(20, 45);
        schedule.showMealAllowanceMarker = true;
        AttendanceStorage::saveWorkSchedule(schedule);
        AttendanceRecord record;
        record.note = QStringLiteral("完整备注，包含中文");
        record.needAverageCal = false;
        AttendanceStorage::saveRecord(QDate(2020, 1, 1), record);
        AttendanceStorage::saveRecord(QDate(2026, 10, 9), record);
        AttendanceStorage::saveRecord(QDate(2027, 1, 1), record);
        QString error;
        m_snapshot = AttendanceStorage::createBackup(error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(m_snapshot.startsWith(QByteArray("SQLite format 3\0", 16)));
        QFile file(m_workspace.filePath(QStringLiteral("snapshot.db")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(m_snapshot), qint64(m_snapshot.size()));
        file.close();
        {
            auto copy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("snapshot-check"));
            copy.setDatabaseName(file.fileName());
            QVERIFY(copy.open());
            QSqlQuery query(copy);
            QVERIFY(query.exec(QStringLiteral("PRAGMA integrity_check")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("ok"));
            QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*), MIN(need_average_cal), MIN(note) FROM records")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 3);
            QCOMPARE(query.value(1).toInt(), 0);
            QCOMPARE(query.value(2).toString(), record.note);
            QVERIFY(query.exec(QStringLiteral("SELECT meal_allowance_time, show_meal_allowance_marker FROM work_schedule")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("20:45"));
            QCOMPARE(query.value(1).toInt(), 1);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("snapshot-check"));
    }

    void retentionAndOwnership()
    {
        QTemporaryDir directory;
        BackupStore store;
        store.setRoot(directory.path());
        const QString device(64, QLatin1Char('a'));
        const QString key(64, QLatin1Char('1'));
        const QString wrongKey(64, QLatin1Char('2'));
        const QDate today(2026, 10, 9);
        for (int offset = -4; offset <= 0; ++offset) {
            QCOMPARE(store.upload(device, key, m_snapshot, today.addDays(offset)).status, 200);
        }
        const BackupResult listed = store.list(device, key, today);
        QCOMPARE(listed.entries.size(), 3);
        QCOMPARE(listed.entries.first().toObject().value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-09"));
        QCOMPARE(store.download(device, key, QStringLiteral("2026-10-06"), today).status, 404);
        QCOMPARE(store.download(device, key, QStringLiteral("2026-10-07"), today).data, m_snapshot);
        QCOMPARE(store.list(device, wrongKey, today).status, 403);
        QCOMPARE(store.upload(device, wrongKey, m_snapshot, today).status, 403);
        QCOMPARE(store.download(device, wrongKey, QStringLiteral("2026-10-09"), today).status, 403);
        QCOMPARE(store.list(QStringLiteral("../escape"), key, today).status, 400);
        QCOMPARE(store.download(device, key, QStringLiteral("../credential.sha256"), today).status, 400);
        QCOMPARE(store.upload(device, key, QByteArray("invalid"), today).status, 400);
        QCOMPARE(store.upload(device, key, QByteArray(BackupStore::maxBackupBytes + 1, 'x'), today).status, 413);
        // 同一天再次上传只更新一份；停用客户端的旧文件也由全局清理移除。
        QCOMPARE(store.upload(device, key, m_snapshot, today).status, 200);
        QCOMPARE(store.list(device, key, today).entries.size(), 3);
        store.prune(today.addDays(3));
        QCOMPARE(store.list(device, key, today.addDays(3)).entries.size(), 0);
        BackupStore restarted;
        restarted.setRoot(directory.path());
        QCOMPARE(restarted.list(device, wrongKey, today.addDays(3)).status, 403);
    }

    void realHttpAuthorizationAndRestart()
    {
        const QString a(64, QLatin1Char('b'));
        const QString b(64, QLatin1Char('c'));
        const QString keyA(64, QLatin1Char('3'));
        const QString keyB(64, QLatin1Char('4'));
        QCOMPARE(http(QStringLiteral("PUT"), QStringLiteral("/api/backups"), a, keyA, m_snapshot).status, 200);
        QCOMPARE(http(QStringLiteral("PUT"), QStringLiteral("/api/backups"), b, keyB, m_snapshot).status, 200);
        const QString path = QStringLiteral("/api/backups/") + QDate::currentDate().toString(Qt::ISODate);
        QCOMPARE(http(QStringLiteral("GET"), path, a, keyA).body, m_snapshot);
        QCOMPARE(http(QStringLiteral("GET"), path, a, keyB).status, 403);
        QCOMPARE(http(QStringLiteral("GET"), path, b, keyA).status, 403);
        QCOMPARE(http(QStringLiteral("GET"), path, {}, {}).status, 400);
        QCOMPARE(http(QStringLiteral("GET"), QStringLiteral("/api/backups"), a, keyB).status, 403);
        QCOMPARE(http(QStringLiteral("PUT"), QStringLiteral("/api/backups"), a, keyB, m_snapshot).status, 403);
        stopServer();
        startServer();
        QCOMPARE(http(QStringLiteral("GET"), path, a, keyA).body, m_snapshot);
    }

    void enableDownloadAndStartupCatchup()
    {
        QTemporaryDir directory;
        QString device;
        {
            BackupClient client(directory.path());
            client.setEndpoint(m_endpoint);
            device = client.deviceId();
            QVERIFY(!client.isEnabled());
            QSignalSpy status(&client, &BackupClient::statusChanged);
            client.setEnabled(true);
            QTRY_VERIFY_WITH_TIMEOUT(client.statusText().startsWith(QStringLiteral("上次成功备份")), 5000);
            QSignalSpy listed(&client, &BackupClient::backupsListed);
            client.listBackups();
            QVERIFY(listed.wait(5000));
            const QJsonArray entries = listed.first().at(0).toJsonArray();
            QCOMPARE(entries.size(), 1);
            const QString day = entries.first().toObject().value(QStringLiteral("date")).toString();
            QSignalSpy downloaded(&client, &BackupClient::backupDownloaded);
            client.downloadBackup(day, directory.filePath(QStringLiteral("download.db")));
            QVERIFY(downloaded.wait(5000));
            QFile file(directory.filePath(QStringLiteral("download.db")));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), m_snapshot);
            QSignalSpy errors(&client, &BackupClient::operationFailed);
            client.downloadBackup(day, QSqlDatabase::database(QStringLiteral("attendance-storage")).databaseName());
            QCOMPARE(errors.size(), 1);
        }
        const QString settingsPath = directory.filePath(QStringLiteral("backup.ini"));
        // 模拟错过零点：成功日期仍是昨天；下一次启动应上传最新的全部数据。
        QSettings settings(settingsPath, QSettings::IniFormat);
        const QString endpointHash = QString::fromLatin1(QCryptographicHash::hash(m_endpoint.toEncoded(), QCryptographicHash::Sha256).toHex());
        settings.setValue(QStringLiteral("servers/") + endpointHash + QStringLiteral("/day"),
            QDate::currentDate().addDays(-1).toString(Qt::ISODate));
        settings.sync();
        BackupClient resumed(directory.path());
        QCOMPARE(resumed.deviceId(), device);
        QVERIFY(resumed.isEnabled());
        resumed.setEndpoint(m_endpoint);
        QTRY_VERIFY_WITH_TIMEOUT(resumed.statusText().startsWith(QStringLiteral("上次成功备份")), 5000);
        QSettings saved(settingsPath, QSettings::IniFormat);
        QCOMPARE(saved.value(QStringLiteral("servers/") + endpointHash + QStringLiteral("/day")).toString(),
            QDate::currentDate().toString(Qt::ISODate));
        resumed.setEnabled(false);
        QVERIFY(!resumed.isEnabled());
        QSignalSpy listed(&resumed, &BackupClient::backupsListed);
        resumed.listBackups();
        QVERIFY(listed.wait(5000));
        QCOMPARE(listed.first().at(0).toJsonArray().size(), 1);
    }

    void emptyRecordsStillBackUpSchedule()
    {
        QSqlQuery query(QSqlDatabase::database(QStringLiteral("attendance-storage")));
        QVERIFY(query.exec(QStringLiteral("DELETE FROM records")));
        QString error;
        const QByteArray data = AttendanceStorage::createBackup(error);
        QVERIFY(error.isEmpty());
        QVERIFY(data.startsWith(QByteArray("SQLite format 3\0", 16)));
        QVERIFY(!data.isEmpty());
    }

    void midnightUploadsNewCompleteSnapshot()
    {
        QTemporaryDir directory;
        QDateTime now(QDate::currentDate(), QTime(23, 59, 59));
        BackupClient client(directory.path(), nullptr, [&now] { return now; });
        client.setEndpoint(m_endpoint);
        client.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(client.statusText().startsWith(QStringLiteral("上次成功备份")), 5000);
        AttendanceRecord record;
        record.note = QStringLiteral("零点前保存的新记录");
        AttendanceStorage::saveRecord(QDate(2026, 10, 9), record);
        now = QDateTime(now.date().addDays(1), QTime(0, 0));
        const QString endpointHash = QString::fromLatin1(QCryptographicHash::hash(m_endpoint.toEncoded(), QCryptographicHash::Sha256).toHex());
        const QString settingsKey = QStringLiteral("servers/") + endpointHash + QStringLiteral("/day");
        const QString settingsPath = directory.filePath(QStringLiteral("backup.ini"));
        const auto savedDay = [&] { return QSettings(settingsPath, QSettings::IniFormat).value(settingsKey).toString(); };
        QTRY_COMPARE_WITH_TIMEOUT(savedDay(), now.date().toString(Qt::ISODate), 5000);
        QSignalSpy downloaded(&client, &BackupClient::backupDownloaded);
        const QString path = directory.filePath(QStringLiteral("midnight.db"));
        client.downloadBackup(QDate::currentDate().toString(Qt::ISODate), path);
        QVERIFY(downloaded.wait(5000));
        {
            auto copy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("midnight-check"));
            copy.setDatabaseName(path);
            QVERIFY(copy.open());
            QSqlQuery query(copy);
            QVERIFY(query.exec(QStringLiteral("SELECT note FROM records WHERE record_date = '2026-10-09'")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), record.note);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("midnight-check"));
    }

    void cleanupTestCase()
    {
        stopServer();
        QSqlDatabase::database(QStringLiteral("attendance-storage")).close();
        QSqlDatabase::removeDatabase(QStringLiteral("attendance-storage"));
    }
};

QTEST_GUILESS_MAIN(BackupTests)
#include "BackupTests.moc"
