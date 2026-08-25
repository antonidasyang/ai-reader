#include "LibraryDb.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {
// store_meta keys. Both belong to this copy of the database, never to the
// account: a settings key would be carried to another machine by the
// user-preferences sync and would then vouch for a store it has never seen.
constexpr auto kMetaOwner = "owner_user_id";
constexpr auto kMetaSession = "session_user_id";

QString jsonToText(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject textToJson(const QString &t)
{
    return QJsonDocument::fromJson(t.toUtf8()).object();
}
} // namespace

LibraryDb::LibraryDb(QObject *parent)
    : QObject(parent)
    , m_connName(QStringLiteral("ai-reader-library"))
{
    if (open())
        m_ready = migrate();
}

QSqlDatabase LibraryDb::database() const
{
    return QSqlDatabase::database(m_connName);
}

bool LibraryDb::open()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library");
    QDir().mkpath(dir);
    m_path = dir + QStringLiteral("/library.db");

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(m_path);
    if (!db.open()) {
        qWarning() << "LibraryDb: open failed:" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    qInfo() << "LibraryDb: opened" << m_path;
    return true;
}

bool LibraryDb::migrate()
{
    QSqlDatabase db = database();
    QSqlQuery q(db);
    const auto run = [&](const QString &sql) -> bool {
        if (!q.exec(sql)) {
            qWarning() << "LibraryDb migrate failed:" << q.lastError().text()
                       << "\n  SQL:" << sql;
            return false;
        }
        return true;
    };

    bool ok = true;
    ok &= run(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_objects ("
        " id TEXT PRIMARY KEY,"
        " project_id TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " data TEXT NOT NULL,"
        " version INTEGER NOT NULL DEFAULT 0,"
        " deleted INTEGER NOT NULL DEFAULT 0,"
        " updated_at TEXT,"
        " updated_by TEXT,"
        " dirty INTEGER NOT NULL DEFAULT 0,"
        " base_version INTEGER NOT NULL DEFAULT 0)"));
    ok &= run(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_obj_project_type "
        "ON sync_objects(project_id, type)"));
    ok &= run(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_obj_dirty "
        "ON sync_objects(project_id, dirty)"));
    ok &= run(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_state ("
        " project_id TEXT PRIMARY KEY,"
        " last_version INTEGER NOT NULL DEFAULT 0)"));
    ok &= run(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS projects ("
        " id TEXT PRIMARY KEY,"
        " name TEXT,"
        " description TEXT,"
        " role TEXT,"
        " version INTEGER NOT NULL DEFAULT 0)"));

    // Side index over the `paper_data` objects (a member's segmentation /
    // translations for one paper). Their payloads run to hundreds of KB, so
    // the paper-open path must never scan them; it keys straight in here.
    ok &= run(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS paper_data_index ("
        " object_id TEXT PRIMARY KEY,"
        " project_id TEXT NOT NULL,"
        " paper_id TEXT NOT NULL,"
        " kind TEXT NOT NULL,"
        " author TEXT,"
        " author_email TEXT,"
        " n INTEGER NOT NULL DEFAULT 0,"
        " updated_at TEXT)"));
    ok &= run(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_paper_data_lookup "
        "ON paper_data_index(project_id, paper_id, kind)"));
    // Backfill for a store that synced paper_data before the index existed
    // (and after a purge-and-repull, where rows arrive through the indexed
    // path anyway). Cheap: the WHERE clause hits idx_obj_project_type.
    {
        QSqlQuery c(db);
        if (c.exec(QStringLiteral("SELECT COUNT(*) FROM paper_data_index"))
            && c.next() && c.value(0).toInt() == 0) {
            QSqlQuery scan(db);
            if (scan.exec(QStringLiteral(
                    "SELECT id, project_id, data, updated_at FROM sync_objects "
                    "WHERE type='paper_data' AND deleted=0"))) {
                while (scan.next()) {
                    const QJsonObject d = textToJson(scan.value(2).toString());
                    PaperDataRef ref;
                    ref.objectId = scan.value(0).toString();
                    ref.projectId = scan.value(1).toString();
                    ref.paperId = d.value(QStringLiteral("paperId")).toString();
                    ref.kind = d.value(QStringLiteral("kind")).toString();
                    ref.author = d.value(QStringLiteral("author")).toString();
                    ref.authorEmail =
                        d.value(QStringLiteral("authorEmail")).toString();
                    ref.count = d.value(QStringLiteral("n")).toInt();
                    ref.updatedAt = scan.value(3).toString();
                    if (!ref.paperId.isEmpty() && !ref.kind.isEmpty())
                        indexPaperData(ref);
                }
            }
        }
    }

    // Who this store belongs to, and whose session is open on it. One row
    // per fact, in the database rather than in QSettings — see the header.
    ok &= run(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS store_meta ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL)"));
    // ...and which account claimed each project. sync_state already has one
    // row per project, so the claim rides along with the sync cursor. ALTER
    // is the only way to add it to a store that already exists; on one that
    // already has the column it just fails, which is the "already migrated"
    // answer.
    {
        QSqlQuery alter(db);
        alter.exec(QStringLiteral(
            "ALTER TABLE sync_state ADD COLUMN owner_id TEXT"));
    }

    // FTS5 self-check: the bundled qsqlite driver normally ships FTS5, but a
    // system-sqlite build might not. If the virtual table can't be created,
    // disable search rather than break the whole DB.
    if (q.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS fts_docs USING fts5("
            "obj_id UNINDEXED, project_id UNINDEXED, kind, content)"))) {
        m_ftsAvailable = true;
    } else {
        m_ftsAvailable = false;
        qWarning() << "LibraryDb: FTS5 unavailable in this SQLite build ("
                   << q.lastError().text()
                   << ") - full-text search disabled.";
    }
    loadGateState();
    return ok;
}

// ─────────────────────────────────────────────────── the account gate ──

void LibraryDb::loadGateState()
{
    m_owner.clear();
    m_sessionUser.clear();
    m_claims.clear();
    QSqlQuery q(database());
    if (q.exec(QStringLiteral("SELECT key, value FROM store_meta"))) {
        while (q.next()) {
            const QString key = q.value(0).toString();
            if (key == QLatin1String(kMetaOwner))
                m_owner = q.value(1).toString();
            else if (key == QLatin1String(kMetaSession))
                m_sessionUser = q.value(1).toString();
        }
    }
    QSqlQuery c(database());
    if (c.exec(QStringLiteral(
            "SELECT project_id, owner_id FROM sync_state "
            "WHERE owner_id IS NOT NULL AND owner_id <> ''"))) {
        while (c.next())
            m_claims.insert(c.value(0).toString(), c.value(1).toString());
    }
}

void LibraryDb::setMeta(const QString &key, const QString &value)
{
    QSqlQuery q(database());
    if (value.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM store_meta WHERE key=?"));
        q.addBindValue(key);
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO store_meta(key, value) VALUES(?,?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        q.addBindValue(key);
        q.addBindValue(value);
    }
    if (!q.exec())
        qWarning() << "LibraryDb::setMeta:" << q.lastError().text();
}

QStringList LibraryDb::knownProjectIds() const
{
    QStringList ids;
    QSqlQuery q(database());
    if (q.exec(QStringLiteral(
            "SELECT id FROM projects "
            "UNION SELECT project_id FROM sync_state "
            "UNION SELECT DISTINCT project_id FROM sync_objects"))) {
        while (q.next()) {
            const QString id = q.value(0).toString();
            if (!id.isEmpty())
                ids << id;
        }
    }
    return ids;
}

void LibraryDb::openSession(const QString &userId)
{
    if (userId.isEmpty())
        return;
    if (m_owner.isEmpty()) {
        // Nothing has claimed this store: a fresh profile, or one that
        // predates the gate and is full of its own user's papers. It adopts
        // whoever signs in first, together with everything already in it —
        // the alternative is a library that locks out the person who built
        // it the first time they update the app.
        m_owner = userId;
        setMeta(QString::fromLatin1(kMetaOwner), userId);
        claimProjects(knownProjectIds(), userId);
        qInfo() << "LibraryDb: store adopted by" << userId;
    }
    if (m_sessionUser != userId) {
        m_sessionUser = userId;
        setMeta(QString::fromLatin1(kMetaSession), userId);
    }
}

void LibraryDb::closeSession()
{
    if (m_sessionUser.isEmpty())
        return;
    m_sessionUser.clear();
    setMeta(QString::fromLatin1(kMetaSession), QString());
}

void LibraryDb::claimProjects(const QStringList &projectIds,
                              const QString &userId)
{
    if (userId.isEmpty() || projectIds.isEmpty())
        return;
    QSqlDatabase db = database();
    db.transaction();
    QSqlQuery q(db);
    for (const QString &pid : projectIds) {
        // First claim wins. Re-assigning a project would hand the account
        // that pulled it a second reader, which is the whole thing this
        // gate exists to stop.
        if (pid.isEmpty() || m_claims.contains(pid))
            continue;
        q.prepare(QStringLiteral(
            "INSERT INTO sync_state(project_id, last_version, owner_id) "
            "VALUES(?,0,?) ON CONFLICT(project_id) DO UPDATE SET "
            "owner_id=excluded.owner_id "
            "WHERE sync_state.owner_id IS NULL OR sync_state.owner_id=''"));
        q.addBindValue(pid);
        q.addBindValue(userId);
        if (!q.exec()) {
            qWarning() << "LibraryDb::claimProjects:" << q.lastError().text();
            continue;
        }
        m_claims.insert(pid, userId);
    }
    db.commit();
}

bool LibraryDb::canRead(const QString &projectId) const
{
    if (projectId.isEmpty())
        return false;
    // A store nobody has ever claimed has no owner to protect, and the
    // first session to open will adopt it.
    if (m_owner.isEmpty() && m_claims.isEmpty())
        return true;
    // Whoever signed in — and, after a restart with no network, still the
    // account whose session was never closed. Empty only after a sign-out.
    const QString me = m_sessionUser;
    if (me.isEmpty())
        return false;
    const QString owner = m_claims.value(projectId);
    // A project nobody claimed belongs to whoever owns the store: a
    // brand-new project of theirs is readable before its first sync, and
    // one of the owner's that predates the claim column stays readable.
    return owner.isEmpty() ? me == m_owner : owner == me;
}

QString LibraryDb::lockReason() const
{
    if (m_owner.isEmpty() && m_claims.isEmpty())
        return {};
    if (m_sessionUser.isEmpty())
        return QStringLiteral("signed-out");
    if (m_sessionUser != m_owner)
        return QStringLiteral("other-account");
    return {};
}

qint64 LibraryDb::lastVersion(const QString &projectId) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT last_version FROM sync_state WHERE project_id=?"));
    q.addBindValue(projectId);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return 0;
}

void LibraryDb::setLastVersion(const QString &projectId, qint64 version)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT INTO sync_state(project_id, last_version) VALUES(?, ?) "
        "ON CONFLICT(project_id) DO UPDATE SET last_version=excluded.last_version"));
    q.addBindValue(projectId);
    q.addBindValue(version);
    if (!q.exec())
        qWarning() << "LibraryDb::setLastVersion:" << q.lastError().text();
}

void LibraryDb::upsertFromServer(const SyncObjectRow &row)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT INTO sync_objects"
        "(id, project_id, type, data, version, deleted, updated_at, updated_by,"
        " dirty, base_version) VALUES(?,?,?,?,?,?,?,?,0,?) "
        "ON CONFLICT(id) DO UPDATE SET project_id=excluded.project_id,"
        " type=excluded.type, data=excluded.data, version=excluded.version,"
        " deleted=excluded.deleted, updated_at=excluded.updated_at,"
        " updated_by=excluded.updated_by, dirty=0,"
        " base_version=excluded.version"));
    q.addBindValue(row.id);
    q.addBindValue(row.projectId);
    q.addBindValue(row.type);
    q.addBindValue(jsonToText(row.data));
    q.addBindValue(row.version);
    q.addBindValue(row.deleted ? 1 : 0);
    q.addBindValue(row.updatedAt);
    q.addBindValue(row.updatedBy);
    q.addBindValue(row.version);
    if (!q.exec())
        qWarning() << "LibraryDb::upsertFromServer:" << q.lastError().text();
}

void LibraryDb::localUpsert(const QString &projectId, const QString &id,
                            const QString &type, const QJsonObject &data,
                            bool deleted, const QString &authorId)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery q(database());
    // On insert, version/base_version default 0. On update we keep the existing
    // version (the last-synced version the edit is based on) and only flip dirty
    // + refresh data/updated_at, so base_version stays the expectedVersion.
    q.prepare(QStringLiteral(
        "INSERT INTO sync_objects"
        "(id, project_id, type, data, version, deleted, updated_at, updated_by,"
        " dirty, base_version) VALUES(?,?,?,?,0,?,?,?,1,0) "
        "ON CONFLICT(id) DO UPDATE SET type=excluded.type, data=excluded.data,"
        " deleted=excluded.deleted, updated_at=excluded.updated_at,"
        " updated_by=excluded.updated_by, dirty=1,"
        " base_version=sync_objects.version"));
    q.addBindValue(id);
    q.addBindValue(projectId);
    q.addBindValue(type);
    q.addBindValue(jsonToText(data));
    q.addBindValue(deleted ? 1 : 0);
    q.addBindValue(now);
    q.addBindValue(authorId);
    if (!q.exec())
        qWarning() << "LibraryDb::localUpsert:" << q.lastError().text();
}

bool LibraryDb::getObject(const QString &projectId, const QString &id,
                          SyncObjectRow &out) const
{
    if (!canRead(projectId))
        return false;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, type, data, version, deleted, updated_at,"
        " updated_by, base_version FROM sync_objects WHERE project_id=? AND id=?"));
    q.addBindValue(projectId);
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    out.id = q.value(0).toString();
    out.projectId = q.value(1).toString();
    out.type = q.value(2).toString();
    out.data = textToJson(q.value(3).toString());
    out.version = q.value(4).toLongLong();
    out.deleted = q.value(5).toInt() != 0;
    out.updatedAt = q.value(6).toString();
    out.updatedBy = q.value(7).toString();
    out.baseVersion = q.value(8).toLongLong();
    return true;
}

bool LibraryDb::isDirty(const QString &projectId, const QString &id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT dirty FROM sync_objects WHERE project_id=? AND id=?"));
    q.addBindValue(projectId);
    q.addBindValue(id);
    return q.exec() && q.next() && q.value(0).toInt() != 0;
}

QList<SyncObjectRow> LibraryDb::dirtyObjects(const QString &projectId,
                                            int maxCount,
                                            qint64 maxBytes) const
{
    QList<SyncObjectRow> rows;
    // The outbox is content too: pushing it would put one account's
    // unsynced work on the wire under another's token. It is not deleted,
    // only held, and drains as soon as its own user is back.
    if (!canRead(projectId))
        return rows;
    QSqlQuery q(database());
    // Oldest first (rowid order) so a busy paper can't keep jumping the
    // queue ahead of edits that have been waiting.
    q.prepare(QStringLiteral(
        "SELECT id, project_id, type, data, version, deleted, updated_at,"
        " updated_by, base_version FROM sync_objects "
        "WHERE project_id=? AND dirty=1 ORDER BY rowid"));
    q.addBindValue(projectId);
    if (!q.exec())
        return rows;
    qint64 bytes = 0;
    while (q.next()) {
        const QString text = q.value(3).toString();
        // Stop before overshooting the batch budget, but never return an
        // empty batch: one object bigger than the budget still has to go.
        if (!rows.isEmpty()) {
            if (maxCount > 0 && rows.size() >= maxCount)
                break;
            if (maxBytes > 0 && bytes + text.size() > maxBytes)
                break;
        }
        bytes += text.size();
        SyncObjectRow r;
        r.id = q.value(0).toString();
        r.projectId = q.value(1).toString();
        r.type = q.value(2).toString();
        r.data = textToJson(text);
        r.version = q.value(4).toLongLong();
        r.deleted = q.value(5).toInt() != 0;
        r.updatedAt = q.value(6).toString();
        r.updatedBy = q.value(7).toString();
        r.baseVersion = q.value(8).toLongLong();
        rows.append(r);
    }
    return rows;
}

int LibraryDb::dirtyCount(const QString &projectId) const
{
    if (!canRead(projectId))
        return 0;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM sync_objects WHERE project_id=? AND dirty=1"));
    q.addBindValue(projectId);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

void LibraryDb::markPushed(const QString &projectId, const QStringList &ids,
                           qint64 newVersion)
{
    QSqlDatabase db = database();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_objects SET version=?, base_version=?, dirty=0 "
        "WHERE project_id=? AND id=?"));
    for (const QString &id : ids) {
        q.bindValue(0, newVersion);
        q.bindValue(1, newVersion);
        q.bindValue(2, projectId);
        q.bindValue(3, id);
        if (!q.exec())
            qWarning() << "LibraryDb::markPushed:" << q.lastError().text();
    }
    db.commit();
}

QList<SyncObjectRow> LibraryDb::objectsByType(const QString &projectId,
                                              const QString &type,
                                              bool includeDeleted) const
{
    QList<SyncObjectRow> rows;
    if (!canRead(projectId))
        return rows;
    QSqlQuery q(database());
    QString sql = QStringLiteral(
        "SELECT id, project_id, type, data, version, deleted, updated_at,"
        " updated_by, base_version FROM sync_objects "
        "WHERE project_id=? AND type=?");
    if (!includeDeleted)
        sql += QStringLiteral(" AND deleted=0");
    q.prepare(sql);
    q.addBindValue(projectId);
    q.addBindValue(type);
    if (!q.exec())
        return rows;
    while (q.next()) {
        SyncObjectRow r;
        r.id = q.value(0).toString();
        r.projectId = q.value(1).toString();
        r.type = q.value(2).toString();
        r.data = textToJson(q.value(3).toString());
        r.version = q.value(4).toLongLong();
        r.deleted = q.value(5).toInt() != 0;
        r.updatedAt = q.value(6).toString();
        r.updatedBy = q.value(7).toString();
        r.baseVersion = q.value(8).toLongLong();
        rows.append(r);
    }
    return rows;
}

void LibraryDb::replaceProjects(const QList<ProjectRow> &projects)
{
    QSqlDatabase db = database();
    db.transaction();
    QSqlQuery del(db);
    del.exec(QStringLiteral("DELETE FROM projects"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO projects(id, name, description, role, version) "
        "VALUES(?,?,?,?,?)"));
    for (const ProjectRow &p : projects) {
        q.bindValue(0, p.id);
        q.bindValue(1, p.name);
        q.bindValue(2, p.description);
        q.bindValue(3, p.role);
        q.bindValue(4, p.version);
        if (!q.exec())
            qWarning() << "LibraryDb::replaceProjects:" << q.lastError().text();
    }
    db.commit();
}

QList<ProjectRow> LibraryDb::projects() const
{
    QList<ProjectRow> rows;
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral(
            "SELECT id, name, description, role, version FROM projects "
            "ORDER BY name")))
        return rows;
    while (q.next()) {
        ProjectRow p;
        p.id = q.value(0).toString();
        // The cache is whatever the last session fetched, so it is gated
        // like everything else: signed out it answers nothing, and a
        // second account sees only the projects it claimed itself.
        if (!canRead(p.id))
            continue;
        p.name = q.value(1).toString();
        p.description = q.value(2).toString();
        p.role = q.value(3).toString();
        p.version = q.value(4).toLongLong();
        rows.append(p);
    }
    return rows;
}

void LibraryDb::purgeProject(const QString &projectId)
{
    if (projectId.isEmpty())
        return;
    QSqlDatabase db = database();
    db.transaction();
    QSqlQuery q(db);
    for (const auto &sql : {
             QStringLiteral("DELETE FROM sync_objects WHERE project_id=?"),
             QStringLiteral("DELETE FROM sync_state WHERE project_id=?"),
             QStringLiteral("DELETE FROM paper_data_index WHERE project_id=?"),
         }) {
        q.prepare(sql);
        q.addBindValue(projectId);
        if (!q.exec())
            qWarning() << "LibraryDb: purgeProject:" << q.lastError().text();
    }
    if (m_ftsAvailable) {
        q.prepare(QStringLiteral("DELETE FROM fts_docs WHERE project_id=?"));
        q.addBindValue(projectId);
        q.exec();
    }
    db.commit();
    // The sync_state row carried the claim, so the project is unclaimed
    // again — which is what a project that no longer exists should be.
    m_claims.remove(projectId);
}

void LibraryDb::indexPaperData(const PaperDataRef &ref)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT INTO paper_data_index"
        "(object_id, project_id, paper_id, kind, author, author_email, n,"
        " updated_at) VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(object_id) DO UPDATE SET project_id=excluded.project_id,"
        " paper_id=excluded.paper_id, kind=excluded.kind,"
        " author=excluded.author, author_email=excluded.author_email,"
        " n=excluded.n, updated_at=excluded.updated_at"));
    q.addBindValue(ref.objectId);
    q.addBindValue(ref.projectId);
    q.addBindValue(ref.paperId);
    q.addBindValue(ref.kind);
    q.addBindValue(ref.author);
    q.addBindValue(ref.authorEmail);
    q.addBindValue(ref.count);
    q.addBindValue(ref.updatedAt);
    if (!q.exec())
        qWarning() << "LibraryDb::indexPaperData:" << q.lastError().text();
}

void LibraryDb::removePaperData(const QString &objectId)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("DELETE FROM paper_data_index WHERE object_id=?"));
    q.addBindValue(objectId);
    q.exec();
}

QList<PaperDataRef> LibraryDb::paperData(const QString &projectId,
                                         const QString &paperId,
                                         const QString &kind) const
{
    QList<PaperDataRef> out;
    if (paperId.isEmpty() || !canRead(projectId))
        return out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT object_id, project_id, paper_id, kind, author, author_email,"
        " n, updated_at FROM paper_data_index "
        "WHERE project_id=? AND paper_id=? AND kind=? "
        "ORDER BY updated_at DESC"));
    q.addBindValue(projectId);
    q.addBindValue(paperId);
    q.addBindValue(kind);
    if (!q.exec())
        return out;
    while (q.next()) {
        PaperDataRef r;
        r.objectId = q.value(0).toString();
        r.projectId = q.value(1).toString();
        r.paperId = q.value(2).toString();
        r.kind = q.value(3).toString();
        r.author = q.value(4).toString();
        r.authorEmail = q.value(5).toString();
        r.count = q.value(6).toInt();
        r.updatedAt = q.value(7).toString();
        out.append(r);
    }
    return out;
}

void LibraryDb::indexDoc(const QString &objectId, const QString &projectId,
                         const QString &kind, const QString &content)
{
    if (!m_ftsAvailable)
        return;
    removeDoc(objectId);
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT INTO fts_docs(obj_id, project_id, kind, content) "
        "VALUES(?,?,?,?)"));
    q.addBindValue(objectId);
    q.addBindValue(projectId);
    q.addBindValue(kind);
    q.addBindValue(content);
    if (!q.exec())
        qWarning() << "LibraryDb::indexDoc:" << q.lastError().text();
}

void LibraryDb::removeDoc(const QString &objectId)
{
    if (!m_ftsAvailable)
        return;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("DELETE FROM fts_docs WHERE obj_id=?"));
    q.addBindValue(objectId);
    q.exec();
}

QList<SearchHit> LibraryDb::search(const QString &projectId,
                                   const QString &query, int limit) const
{
    QList<SearchHit> hits;
    if (!m_ftsAvailable || query.trimmed().isEmpty() || !canRead(projectId))
        return hits;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT obj_id, kind, snippet(fts_docs, 3, '[', ']', '...', 12) "
        "FROM fts_docs WHERE project_id=? AND fts_docs MATCH ? "
        "ORDER BY bm25(fts_docs) LIMIT ?"));
    q.addBindValue(projectId);
    q.addBindValue(query);
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning() << "LibraryDb::search:" << q.lastError().text();
        return hits;
    }
    while (q.next()) {
        SearchHit h;
        h.objectId = q.value(0).toString();
        h.kind = q.value(1).toString();
        h.snippet = q.value(2).toString();
        hits.append(h);
    }
    return hits;
}
