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
    void push(const QString &projectId, int attempt, std::function<void()> then);
    void applyServerObject(const QString &projectId, const QJsonObject &o);
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
    QString m_lastError;
};
