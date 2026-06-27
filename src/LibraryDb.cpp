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
    return ok;
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

QList<SyncObjectRow> LibraryDb::dirtyObjects(const QString &projectId) const
{
    QList<SyncObjectRow> rows;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, type, data, version, deleted, updated_at,"
        " updated_by, base_version FROM sync_objects "
        "WHERE project_id=? AND dirty=1"));
    q.addBindValue(projectId);
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
        p.name = q.value(1).toString();
        p.description = q.value(2).toString();
        p.role = q.value(3).toString();
        p.version = q.value(4).toLongLong();
        rows.append(p);
    }
    return rows;
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
    if (!m_ftsAvailable || query.trimmed().isEmpty())
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
