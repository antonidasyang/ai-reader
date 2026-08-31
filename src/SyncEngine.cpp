#include "SyncEngine.h"
#include "Stall.h"

#include <QElapsedTimer>
#include <QTimer>
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
// Pull one page at a time. Sized for the biggest objects we sync (a paper's
// blocks or translations, a few hundred KB compressed) rather than for
// items, which are tiny.
// Objects per pulled page. Small on purpose: an `item` is a few hundred
// bytes, but a `paper_data` artifact is a whole paper's paragraphs or a
// whole paper's translations, and two hundred of those is a response the
// GUI thread cannot parse inside a frame. The parse is the one part of a
// pull that cannot be sliced, so the page is what has to be.
constexpr int kPullPageLimit = 25;
constexpr int kMaxPullPages = 4000;
// How long one slice of a page may hold the thread before it hands back.
// About a frame: long enough that the walk is not all overhead, short
// enough that nobody sees it.
constexpr int kApplySliceMs = 12;
// Outbox batching. The server's JSON body limit is well above this; the point
// is to keep any single request modest and to make progress incrementally.
constexpr int kPushMaxObjects = 100;
constexpr qint64 kPushMaxBytes = 6 * 1024 * 1024;
constexpr int kMaxPushBatches = 200;
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
        push(projectId, 0, 0, [this, projectId] {
            setSyncing(false);
            // Everything that reacts to a sync runs inside this emission,
            // on the GUI thread; the watchdog should say so.
            Stall::Mark mark("a sync landing");
            emit projectSynced(projectId);
        });
    });
}

void SyncEngine::pull(const QString &projectId, std::function<void()> then)
{
    pullPage(projectId, 0, then);
}

void SyncEngine::pullPage(const QString &projectId, int page,
                          std::function<void()> then)
{
    const qint64 since = m_db->lastVersion(projectId);
    m_api->get(
        QStringLiteral("/projects/") + projectId + QStringLiteral("/sync?since=")
            + QString::number(since) + QStringLiteral("&limit=")
            + QString::number(kPullPageLimit),
        [this, projectId, page, since, then](bool ok, int status,
                                             const QJsonDocument &doc) {
            if (!ok) {
                setError(tr("Pull failed (HTTP %1)").arg(status));
                if (then)
                    then();
                return;
            }
            const QJsonObject root = doc.object();
            const QJsonArray objects =
                root.value(QStringLiteral("objects")).toArray();
            m_serverPushLimit =
                qint64(root.value(QStringLiteral("pushLimitBytes")).toDouble());
            const qint64 newVersion =
                root.value(QStringLiteral("newVersion")).toString().toLongLong();
            // A server that paginates says so; an older one never sets the
            // flag and answers in a single page, exactly as before.
            const bool more = root.value(QStringLiteral("hasMore")).toBool();

            applyPage(
                projectId, objects, 0, since,
                [this, projectId, page, since, then, more, newVersion](
                    qint64 applied) {
                    if (more && applied > since && page + 1 < kMaxPullPages) {
                        // Park the cursor on what we actually applied, so an
                        // interrupted multi-page pull resumes instead of
                        // restarting.
                        m_db->setLastVersion(projectId, applied);
                        pullPage(projectId, page + 1, then);
                        return;
                    }
                    m_db->setLastVersion(projectId, newVersion);
                    if (then)
                        then();
                });
        });
}

void SyncEngine::applyPage(const QString &projectId, const QJsonArray &objects,
                           int from, qint64 applied,
                           const std::function<void(qint64)> &done)
{
    if (from >= objects.size()) {
        done(applied);
        return;
    }
    int i = from;
    {
        Stall::Mark mark("taking in what a sync brought");
        QElapsedTimer slice;
        slice.start();
        while (i < objects.size()) {
            const QJsonObject o = objects.at(i).toObject();
            applyServerObject(projectId, o);
            applied = qMax(applied,
                           o.value(QStringLiteral("version"))
                               .toString().toLongLong());
            ++i;
            if (slice.elapsed() >= kApplySliceMs)
                break;
        }
    }
    if (i >= objects.size()) {
        done(applied);
        return;
    }
    QTimer::singleShot(0, this, [this, projectId, objects, i, applied, done] {
        applyPage(projectId, objects, i, applied, done);
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

void SyncEngine::push(const QString &projectId, int attempt, int batch,
                      std::function<void()> then)
{
    // Stay inside whatever the server admitted to accepting, with room for the
    // JSON envelope around the objects.
    const qint64 budget = m_serverPushLimit > 0
                              ? qMin(kPushMaxBytes, m_serverPushLimit * 3 / 4)
                              : kPushMaxBytes;
    const QList<SyncObjectRow> dirty =
        m_db->dirtyObjects(projectId, kPushMaxObjects, budget);
    if (dirty.isEmpty() || attempt >= kMaxPushAttempts
        || batch >= kMaxPushBatches) {
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
        [this, projectId, attempt, batch, then, localById](
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

            if (producedDirty) {
                push(projectId, attempt + 1, batch, then);
            } else if (!appliedIds.isEmpty()
                       && m_db->dirtyCount(projectId) > 0) {
                // More outbox than one batch could carry — keep going, with
                // the retry counter reset for the fresh batch.
                push(projectId, 0, batch + 1, then);
            } else if (then) {
                then();
            }
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
    if (row.type == QLatin1String("paper_data")) {
        // Keep the lightweight side index in step; the payload itself stays
        // in sync_objects and is only read when a paper actually adopts it.
        if (row.deleted) {
            m_db->removePaperData(row.id);
            return;
        }
        PaperDataRef ref;
        ref.objectId = row.id;
        ref.projectId = row.projectId;
        ref.paperId = row.data.value(QStringLiteral("paperId")).toString();
        ref.kind = row.data.value(QStringLiteral("kind")).toString();
        ref.author = row.data.value(QStringLiteral("author")).toString();
        ref.authorEmail =
            row.data.value(QStringLiteral("authorEmail")).toString();
        ref.count = row.data.value(QStringLiteral("n")).toInt();
        ref.updatedAt = row.data.value(QStringLiteral("updatedAt")).toString();
        if (!ref.paperId.isEmpty() && !ref.kind.isEmpty())
            m_db->indexPaperData(ref);
        return;
    }
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
    for (const QJsonValue &t : row.data.value(QStringLiteral("tags")).toArray())
        parts << t.toString();
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
