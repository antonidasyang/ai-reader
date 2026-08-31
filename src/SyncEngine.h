#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>
#include <functional>

#include "LibraryDb.h"

class ApiClient;
class AuthController;
class ProjectController;

// Offline-first sync loop for the current project: pull (incremental, applies
// server objects unless locally dirty), then push the outbox; conflicts come
// back as the server value and are merged object-level last-write-wins, then
// re-pushed. A WebSocket carries "changed" notifications for near-real-time
// pulls, with a periodic poll as a fallback. The local SQLite store stays the
// single source the UI reads, so the app works fully offline.
class SyncEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    SyncEngine(ApiClient *api, AuthController *auth, ProjectController *projects,
               LibraryDb *db, QObject *parent = nullptr);

    bool syncing() const { return m_syncing; }
    QString lastError() const { return m_lastError; }
    // Largest push body the server said it accepts, as advertised on the last
    // pull; 0 until we have heard from a server that advertises one. Callers
    // that want to sync large objects must gate on this: a server from before
    // the limit was raised would reject the whole batch, and the outbox — with
    // everyone's ordinary library edits in it — would stop draining.
    qint64 serverPushLimit() const { return m_serverPushLimit; }

    Q_INVOKABLE void syncNow();
    // Record a local item edit and trigger a push (used by LibraryModel etc.).
    Q_INVOKABLE void putObject(const QString &type, const QString &id,
                               const QJsonObject &data, bool deleted = false);

signals:
    void syncingChanged();
    void lastErrorChanged();
    void projectSynced(const QString &projectId);

private:
    void onCurrentProjectChanged();
    void onAuthChanged();
    void syncProject(const QString &projectId);
    void pull(const QString &projectId, std::function<void()> then);
    // One page of the incremental pull. A project that has accumulated a lot
    // of paper_data (segmentations and translations run to hundreds of KB
    // each) would otherwise answer a first sync with one enormous response.
    void pullPage(const QString &projectId, int page, std::function<void()> then);
    // `attempt` counts conflict retries of the same batch; `batch` counts
    // successive outbox batches, which are bounded by size so one push body
    // stays small enough for the server to accept.
    void push(const QString &projectId, int attempt, int batch,
              std::function<void()> then);
    void applyServerObject(const QString &projectId, const QJsonObject &o);
    // Walk one pulled page into the database a slice at a time, handing the
    // event loop back between slices. A page of artifacts is tens of
    // megabytes of paragraphs and translations; applying it in one turn is
    // a frozen window for as long as it takes, and none of it is urgent
    // enough to be worth that. `done` gets the highest version applied.
    void applyPage(const QString &projectId, const QJsonArray &objects,
                   int from, qint64 applied,
                   const std::function<void(qint64)> &done);
    void indexObject(const SyncObjectRow &row);
    void setSyncing(bool v);
    void setError(const QString &e);

    // WebSocket change channel.
    void connectWs();
    void subscribeWs();
    QString wsUrl() const;

    static SyncObjectRow parseServer(const QString &projectId,
                                     const QJsonObject &o);
    static QJsonObject mergeLww(const SyncObjectRow &local,
                                const SyncObjectRow &server, bool &serverWins);

    ApiClient *m_api;
    AuthController *m_auth;
    ProjectController *m_projects;
    LibraryDb *m_db;

    QWebSocket m_ws;
    QTimer m_poll;
    QTimer m_wsReconnect;
    bool m_wsAuthed = false;
    QString m_wsProject;

    bool m_syncing = false;
    qint64 m_serverPushLimit = 0;
    QString m_lastError;
};
