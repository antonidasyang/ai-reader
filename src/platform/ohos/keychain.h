// HarmonyOS stand-in for qtkeychain's <keychain.h>.
//
// qtkeychain has no HarmonyOS backend, so on OHOS builds CMake puts this
// directory on the include path instead of qtkeychain's. It implements the
// slice of the QKeychain job API that Settings and AuthController use
// (Read/Write/DeletePasswordJob, finished(Job*), error(), textData()) on top
// of the system Asset Store (Asset Store Kit), which keeps secrets encrypted
// per app in the TEE-backed store. Call sites compile unchanged.
//
// Semantics match qtkeychain where the app relies on them:
//   * start() returns immediately; finished() is always delivered from the
//     event loop, never synchronously from start().
//   * autoDelete defaults to true: the job deletes itself after finished().
//   * a missing entry reads/deletes as EntryNotFound, not OtherError.
//   * setInsecureFallback() is accepted and ignored — the Asset Store is
//     always available on HarmonyOS, so there is nothing to fall back from.
#pragma once

#include <QObject>
#include <QString>

namespace QKeychain {

enum Error {
    NoError = 0,
    EntryNotFound,
    CouldNotDeleteEntry,
    AccessDeniedByUser,
    AccessDenied,
    NoBackendAvailable,
    NotImplemented,
    OtherError
};

class Job : public QObject
{
    Q_OBJECT
public:
    ~Job() override;

    QString service() const { return m_service; }
    QString key() const { return m_key; }
    void setKey(const QString &key) { m_key = key; }

    bool autoDelete() const { return m_autoDelete; }
    void setAutoDelete(bool autoDelete) { m_autoDelete = autoDelete; }

    bool insecureFallback() const { return m_insecureFallback; }
    void setInsecureFallback(bool on) { m_insecureFallback = on; }

    Error error() const { return m_error; }
    QString errorString() const { return m_errorString; }

    void start();

Q_SIGNALS:
    void finished(QKeychain::Job *job);

protected:
    Job(const QString &service, QObject *parent);

    // Performs the store operation synchronously; start() schedules it.
    virtual void run() = 0;
    void setError(Error error, const QString &message);
    QByteArray alias() const;

private:
    void runAndFinish();

    QString m_service;
    QString m_key;
    Error m_error = NoError;
    QString m_errorString;
    bool m_autoDelete = true;
    bool m_insecureFallback = false;
};

class ReadPasswordJob : public Job
{
    Q_OBJECT
public:
    explicit ReadPasswordJob(const QString &service, QObject *parent = nullptr);

    QByteArray binaryData() const { return m_data; }
    QString textData() const { return QString::fromUtf8(m_data); }

protected:
    void run() override;

private:
    QByteArray m_data;
};

class WritePasswordJob : public Job
{
    Q_OBJECT
public:
    explicit WritePasswordJob(const QString &service, QObject *parent = nullptr);

    void setBinaryData(const QByteArray &data) { m_data = data; }
    void setTextData(const QString &text) { m_data = text.toUtf8(); }

protected:
    void run() override;

private:
    QByteArray m_data;
};

class DeletePasswordJob : public Job
{
    Q_OBJECT
public:
    explicit DeletePasswordJob(const QString &service, QObject *parent = nullptr);

protected:
    void run() override;
};

} // namespace QKeychain
