#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>

class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    enum class StartResult { Primary, ActivatedExisting, Failed };

    explicit SingleInstanceGuard(const QString& dataDirectory, QObject* parent = nullptr);
    StartResult start();
    QString errorString() const { return m_error; }

signals:
    void activationRequested();

private:
    void acceptConnections();
    bool activateExisting();

    QString m_dataDirectory;
    QString m_serverName;
    QString m_error;
    QLockFile m_lock;
    QLocalServer m_server;
};
