#include "SyncEngine.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "ProjectController.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

namespace {
constexpr int kPollMs = 30000;
constexpr int kMaxPushAttempts = 5;
} // namespace

SyncEngine::SyncEngine(ApiClient *api, AuthController *auth,
                       ProjectController *projects, LibraryDb *db,
                       QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_auth(auth)
    , m_projects(projects)
    , m_db(db)
{
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, [this] { syncNow(); });

    m_wsReconnect.setSingleShot(true);
    m_wsReconnect.setInterval(5000);
    connect(&m_wsReconnect, &QTimer::timeout, this, [this] { connectWs(); });

    connect(m_projects, &ProjectController::currentChanged, this,
            &SyncEngine::onCurrentProjectChanged);
    connect(m_auth, &AuthController::authenticatedChanged, this,
            &SyncEngine::onAuthChanged);

    connect(&m_ws, &QWebSocket::connected, this, [this] {
        m_wsAuthed = false;
        // Authenticate the socket, then subscribe to the current project.
        QJsonObject frame{{QStringLiteral("event"), QStringLiteral("auth")},
                          {QStringLiteral("data"),
                           QJsonObject{{QStringLiteral("token"),
                                        m_api->accessToken()}}}};
        m_ws.sendTextMessage(
            QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this,
            [this](const QString &msg) {
                const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
                const QString event = o.value(QStringLiteral("event")).toString();
                const QJsonObject data =
                    o.value(QStringLiteral("data")).toObject();
                if (event == QLatin1String("auth")) {
                    m_wsAuthed = data.value(QStringLiteral("ok")).toBool();
                    if (m_wsAuthed)
                        subscribeWs();
                } else if (event == QLatin1String("changed")) {
                    if (data.value(QStringLiteral("projectId")).toString()
                        == m_projects->currentId())
                        syncNow();
                }
            });
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        if (m_auth->authenticated())
            m_wsReconnect.start();
    });

    if (m_auth->authenticated())
        onAuthChanged();
}

void SyncEngine::onAuthChanged()
{
    if (m_auth->authenticated()) {
        m_poll.start();
        connectWs();
        syncNow();
    } else {
        m_poll.stop();
        m_ws.close();
        m_wsProject.clear();
    }
}

void SyncEngine::onCurrentProjectChanged()
{
    if (!m_auth->authenticated())
        return;
    syncProject(m_projects->currentId());
    subscribeWs();
}

void SyncEngine::syncNow()
{
    if (m_auth->authenticated() && !m_projects->currentId().isEmpty())
        syncProject(m_projects->currentId());
}

void SyncEngine::syncProject(const QString &projectId)
{
    if (projectId.isEmpty() || m_syncing)
        return;
    setSyncing(true);
    pull(projectId, [this, projectId] {
        push(projectId, 0, [this, projectId] {
            setSyncing(false);
            emit projectSynced(projectId);
        });
    });
}

void SyncEngine::pull(const QString &projectId, std::function<void()> then)
{
    const qint64 since = m_db->lastVersion(projectId);
    m_api->get(
        QStringLiteral("/projects/") + projectId + QStringLiteral("/sync?since=")
            + QString::number(since),
        [this, projectId, then](bool ok, int status, const QJsonDocument &doc) {
            if (!ok) {
                setError(tr("Pull failed (HTTP %1)").arg(status));
                if (then)
                    then();
                return;
            }
            const QJsonObject root = doc.object();
            const QJsonArray objects =
                root.value(QStringLiteral("objects")).toArray();
            for (const QJsonValue &v : objects)
                applyServerObject(projectId, v.toObject());
            const qint64 newVersion =
                root.value(QStringLiteral("newVersion")).toString().toLongLong();
            m_db->setLastVersion(projectId, newVersion);
            if (then)
                then();
        });
}

void SyncEngine::applyServerObject(const QString &projectId,
                                   const QJsonObject &o)
{
    // Don't clobber an object the user is mid-editing; the push path resolves
    // its conflict via the 409 + merge flow.
    const QString id = o.value(QStringLiteral("id")).toString();
    if (m_db->isDirty(projectId, id))
        return;
    const SyncObjectRow row = parseServer(projectId, o);
    m_db->upsertFromServer(row);
    indexObject(row);
}

void SyncEngine::push(const QString &projectId, int attempt,
                      std::function<void()> then)
{
    const QList<SyncObjectRow> dirty = m_db->dirtyObjects(projectId);
    if (dirty.isEmpty() || attempt >= kMaxPushAttempts) {
        if (then)
            then();
        return;
    }

    QJsonArray objects;
    for (const SyncObjectRow &d : dirty) {
        QJsonObject o{
            {QStringLiteral("id"), d.id},
            {QStringLiteral("type"), d.type},
            {QStringLiteral("data"), d.data},
            {QStringLiteral("deleted"), d.deleted},
            {QStringLiteral("expectedVersion"), QString::number(d.baseVersion)}};
        objects.append(o);
    }

    // Keep the locals indexed by id so we can merge any that conflict.
    QHash<QString, SyncObjectRow> localById;
    for (const SyncObjectRow &d : dirty)
        localById.insert(d.id, d);

    m_api->post(
        QStringLiteral("/projects/") + projectId + QStringLiteral("/push"),
        QJsonObject{{QStringLiteral("objects"), objects}},
        [this, projectId, attempt, then, localById](
            bool ok, int status, const QJsonDocument &doc) {
            if (!ok) {
                setError(tr("Push failed (HTTP %1)").arg(status));
                if (then)
                    then();
                return;
            }
            const QJsonObject root = doc.object();
            const qint64 newVersion =
                root.value(QStringLiteral("newVersion")).toString().toLongLong();

            QStringList appliedIds;
            for (const QJsonValue &v : root.value(QStringLiteral("applied")).toArray())
                appliedIds << v.toString();
            m_db->markPushed(projectId, appliedIds, newVersion);

            bool producedDirty = false;
            for (const QJsonValue &v :
                 root.value(QStringLiteral("conflicts")).toArray()) {
                const QJsonObject c = v.toObject();
                const QString id = c.value(QStringLiteral("id")).toString();
                const SyncObjectRow server =
                    parseServer(projectId, c.value(QStringLiteral("server")).toObject());
                const SyncObjectRow local = localById.value(id);

                // Adopt the server value first (clears dirty, sets version to
                // server.version), then re-apply our merged change if it still
                // differs -- giving base_version = server.version for re-push.
                m_db->upsertFromServer(server);
                indexObject(server);
                bool serverWins = false;
                const QJsonObject merged = mergeLww(local, server, serverWins);
                if (!serverWins) {
                    m_db->localUpsert(projectId, id, local.type, merged,
                                      local.deleted, m_auth->userId());
                    producedDirty = true;
                }
            }

            if (producedDirty)
                push(projectId, attempt + 1, then);
            else if (then)
                then();
        });
}

SyncObjectRow SyncEngine::parseServer(const QString &projectId,
                                      const QJsonObject &o)
{
    SyncObjectRow r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.projectId = projectId;
    r.type = o.value(QStringLiteral("type")).toString();
    r.data = o.value(QStringLiteral("data")).toObject();
    r.version = o.value(QStringLiteral("version")).toString().toLongLong();
    r.deleted = o.value(QStringLiteral("deleted")).toBool();
    r.updatedAt = o.value(QStringLiteral("updatedAt")).toString();
    r.updatedBy = o.value(QStringLiteral("updatedBy")).toString();
    return r;
}

QJsonObject SyncEngine::mergeLww(const SyncObjectRow &local,
                                 const SyncObjectRow &server, bool &serverWins)
{
    // Object-level last-write-wins by updated_at (ISO-8601 UTC sorts lexically).
    // If the local edit is newer we overlay its fields onto the server value
    // (so untouched server fields survive); otherwise the server wins outright.
    if (local.updatedAt > server.updatedAt) {
        QJsonObject merged = server.data;
        for (auto it = local.data.begin(); it != local.data.end(); ++it)
            merged.insert(it.key(), it.value());
        serverWins = false;
        return merged;
    }
    serverWins = true;
    return server.data;
}

void SyncEngine::indexObject(const SyncObjectRow &row)
{
    if (row.type != QLatin1String("item"))
        return;
    if (row.deleted) {
        m_db->removeDoc(row.id);
        return;
    }
    QStringList parts;
    parts << row.data.value(QStringLiteral("title")).toString();
    parts << row.data.value(QStringLiteral("abstract")).toString();
    parts << row.data.value(QStringLiteral("publication")).toString();
    const QJsonArray creators = row.data.value(QStringLiteral("creators")).toArray();
    for (const QJsonValue &c : creators)
        parts << c.toString();
    m_db->indexDoc(row.id, row.projectId, QStringLiteral("item"),
                   parts.join(QChar(' ')));
}

void SyncEngine::putObject(const QString &type, const QString &id,
                           const QJsonObject &data, bool deleted)
{
    const QString projectId = m_projects->currentId();
    if (projectId.isEmpty())
        return;
    m_db->localUpsert(projectId, id, type, data, deleted, m_auth->userId());
    SyncObjectRow row;
    row.id = id;
    row.projectId = projectId;
    row.type = type;
    row.data = data;
    row.deleted = deleted;
    indexObject(row);
    syncNow();
}

QString SyncEngine::wsUrl() const
{
    QUrl u(m_api->baseUrl());
    u.setScheme(u.scheme() == QLatin1String("https") ? QStringLiteral("wss")
                                                      : QStringLiteral("ws"));
    if (u.path().isEmpty())
        u.setPath(QStringLiteral("/"));
    return u.toString();
}

void SyncEngine::connectWs()
{
    if (!m_auth->authenticated())
        return;
    if (m_ws.state() == QAbstractSocket::ConnectedState
        || m_ws.state() == QAbstractSocket::ConnectingState)
        return;
    m_ws.open(QUrl(wsUrl()));
}

void SyncEngine::subscribeWs()
{
    if (!m_wsAuthed || m_ws.state() != QAbstractSocket::ConnectedState)
        return;
    const QString pid = m_projects->currentId();
    if (pid.isEmpty() || pid == m_wsProject)
        return;
    m_wsProject = pid;
    QJsonObject frame{
        {QStringLiteral("event"), QStringLiteral("subscribe")},
        {QStringLiteral("data"), QJsonObject{{QStringLiteral("projectId"), pid}}}};
    m_ws.sendTextMessage(
        QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void SyncEngine::setSyncing(bool v)
{
    if (v == m_syncing)
        return;
    m_syncing = v;
    emit syncingChanged();
}

void SyncEngine::setError(const QString &e)
{
    m_lastError = e;
    emit lastErrorChanged();
}
