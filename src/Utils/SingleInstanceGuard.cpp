#include "SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

SingleInstanceGuard::SingleInstanceGuard(const QString& dataDirectory, QObject* parent)
    : QObject(parent)
    , m_dataDirectory(dataDirectory.isEmpty() ? QString() : QDir(dataDirectory).absolutePath())
    , m_lock(QDir(m_dataDirectory).filePath(QStringLiteral("attendance-instance.lock")))
{
    QString identity = QDir::fromNativeSeparators(m_dataDirectory);
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    m_serverName = QStringLiteral("AttendanceApp-")
        + QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),
            QCryptographicHash::Sha256).toHex());
    // 长时间运行的活进程不能仅因锁文件较旧而失去所有权。
    m_lock.setStaleLockTime(0);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection,
        this, &SingleInstanceGuard::acceptConnections);
}

SingleInstanceGuard::StartResult SingleInstanceGuard::start()
{
    if (m_dataDirectory.isEmpty() || !QDir().mkpath(m_dataDirectory)) {
        m_error = QStringLiteral("无法创建程序数据目录，不能启动工时簿。");
        return StartResult::Failed;
    }

    QElapsedTimer deadline;
    deadline.start();
    do {
        if (m_lock.tryLock()) {
            // 只有锁的所有者才能清理崩溃留下的通信端点。
            QLocalServer::removeServer(m_serverName);
            if (!m_server.listen(m_serverName)) {
                m_error = QStringLiteral("无法建立程序实例通信：%1")
                    .arg(m_server.errorString());
                m_lock.unlock();
                return StartResult::Failed;
            }
            return StartResult::Primary;
        }
        if (m_lock.error() != QLockFile::LockFailedError) {
            m_error = QStringLiteral("无法锁定程序数据目录，请检查目录访问权限。");
            return StartResult::Failed;
        }
        if (activateExisting()) {
            return StartResult::ActivatedExisting;
        }
        // 首个进程可能仍在初始化，或者正在退出；重试时也重新争取锁。
        QThread::msleep(50);
    } while (deadline.elapsed() < 5000);

    m_error = QStringLiteral("工时簿已在运行，但暂时无法唤起窗口，请稍后重试。");
    return StartResult::Failed;
}

bool SingleInstanceGuard::activateExisting()
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(200)) {
        return false;
    }
#ifdef Q_OS_WIN
    qint64 processId = 0;
    QString hostName;
    QString appName;
    if (m_lock.getLockInfo(&processId, &hostName, &appName) && processId > 0) {
        AllowSetForegroundWindow(static_cast<DWORD>(processId));
    }
#endif
    socket.write("activate\n");
    if (!socket.waitForBytesWritten(200)) {
        return false;
    }
    QByteArray reply;
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 300) {
        reply += socket.readAll();
        if (reply == "ok\n") {
            return true;
        }
        if (!socket.waitForReadyRead(qMax(1, 300 - int(deadline.elapsed())))) {
            reply += socket.readAll();
            return reply == "ok\n";
        }
    }
    return false;
}

void SingleInstanceGuard::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket* socket = m_server.nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        const auto readRequest = [this, socket] {
            if (!socket->canReadLine()) {
                if (socket->bytesAvailable() > 64) {
                    socket->abort();
                }
                return;
            }
            if (socket->readLine(65) == "activate\n") {
                emit activationRequested();
                socket->write("ok\n");
            }
            socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, this, readRequest);
        QTimer::singleShot(2000, socket, [socket] {
            socket->abort();
            socket->deleteLater();
        });
        readRequest();
    }
}
