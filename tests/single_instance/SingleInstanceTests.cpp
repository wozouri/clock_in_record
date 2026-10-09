#include "SingleInstanceGuard.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <cstdio>

namespace {
void startWorker(QProcess& process, const QString& directory, int delay = 0)
{
    process.start(QCoreApplication::applicationFilePath(),
        {QStringLiteral("--worker"), directory, QString::number(delay)});
}

bool waitForPrimary(QProcess& process)
{
    return process.waitForStarted() && process.waitForReadyRead(5000)
        && process.readAllStandardOutput().contains("primary");
}

void stopWorker(QProcess& process, bool crash = false)
{
    if (crash) {
        process.kill();
    } else {
        process.write("quit\n");
        process.closeWriteChannel();
    }
    process.waitForFinished(5000);
}
} // namespace

class SingleInstanceTests : public QObject
{
    Q_OBJECT
private slots:
    void repeatLaunchAndCleanRestart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess primary;
        startWorker(primary, directory.path());
        QVERIFY(waitForPrimary(primary));
        QProcess secondary;
        startWorker(secondary, directory.path());
        QVERIFY(secondary.waitForFinished(7000));
        QCOMPARE(secondary.exitCode(), 10);
        QVERIFY(primary.waitForReadyRead(2000));
        QVERIFY(primary.readAllStandardOutput().contains("activated"));
        stopWorker(primary);
        QCOMPARE(primary.exitCode(), 0);
        startWorker(primary, directory.path());
        QVERIFY(waitForPrimary(primary));
        stopWorker(primary);
    }

    void concurrentLaunchDuringInitialization()
    {
        QTemporaryDir directory;
        QProcess primary;
        startWorker(primary, directory.path(), 1000);
        QVERIFY(waitForPrimary(primary));
        QProcess competitors[6];
        for (auto& process : competitors) {
            startWorker(process, directory.path());
        }
        for (auto& process : competitors) {
            QVERIFY(process.waitForFinished(7000));
            QCOMPARE(process.exitCode(), 10);
        }
        QCOMPARE(primary.state(), QProcess::Running);
        stopWorker(primary);
    }

    void recoverAfterCrash()
    {
        QTemporaryDir directory;
        QProcess primary;
        startWorker(primary, directory.path());
        QVERIFY(waitForPrimary(primary));
        stopWorker(primary, true);
        startWorker(primary, directory.path());
        QVERIFY(waitForPrimary(primary));
        stopWorker(primary);
    }

    void simultaneousLockCompetition()
    {
        QTemporaryDir directory;
        QProcess processes[6];
        for (auto& process : processes) {
            startWorker(process, directory.path(), 1000);
        }
        int primaryCount = 0;
        for (auto& process : processes) {
            QVERIFY(process.waitForStarted());
            if (process.waitForReadyRead(7000)
                && process.readAllStandardOutput().contains("primary")) {
                ++primaryCount;
            } else {
                QVERIFY(process.state() == QProcess::NotRunning
                    || process.waitForFinished(7000));
                QCOMPARE(process.exitCode(), 10);
            }
        }
        QCOMPARE(primaryCount, 1);
        for (auto& process : processes) {
            if (process.state() == QProcess::Running) {
                stopWorker(process);
            }
        }
    }

    void separateDataDirectories()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QProcess a;
        QProcess b;
        startWorker(a, first.path());
        startWorker(b, second.path());
        QVERIFY(waitForPrimary(a));
        QVERIFY(waitForPrimary(b));
        stopWorker(a);
        stopWorker(b);
    }

    void occupiedLockWithoutServer()
    {
        QTemporaryDir directory;
        const QString lockPath = directory.filePath(QStringLiteral("attendance-instance.lock"));
        QLockFile lock(lockPath);
        QVERIFY(lock.tryLock());
        // Windows 独占锁文件不允许修改时间，实际等待超过 Qt 默认的 30 秒阈值。
        QTest::qWait(31000);
        QProcess secondary;
        startWorker(secondary, directory.path());
        QVERIFY(secondary.waitForFinished(7000));
        QCOMPARE(secondary.exitCode(), 1);
        QVERIFY(lock.isLocked());
    }

    void invalidDirectory()
    {
        SingleInstanceGuard empty(QString{});
        QCOMPARE(empty.start(), SingleInstanceGuard::StartResult::Failed);
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("file")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        SingleInstanceGuard blocked(file.fileName());
        QCOMPARE(blocked.start(), SingleInstanceGuard::StartResult::Failed);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--worker"))) {
        SingleInstanceGuard guard(app.arguments().at(2));
        const auto result = guard.start();
        if (result != SingleInstanceGuard::StartResult::Primary) {
            return result == SingleInstanceGuard::StartResult::ActivatedExisting ? 10 : 1;
        }
        QObject::connect(&guard, &SingleInstanceGuard::activationRequested, [] {
            std::puts("activated");
            std::fflush(stdout);
        });
        std::puts("primary");
        std::fflush(stdout);
        QThread::msleep(app.arguments().at(3).toInt());
        // 测试进程正常退出请求使用独立线程读取 stdin。
        QThread* input = QThread::create([] {
            char buffer[16];
            if (std::fgets(buffer, sizeof(buffer), stdin)) {
                QCoreApplication::quit();
            }
        });
        input->start();
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        const int exitCode = app.exec();
        input->wait(1000);
        if (input->isFinished()) {
            delete input;
        }
        return exitCode;
    }
    SingleInstanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "SingleInstanceTests.moc"
