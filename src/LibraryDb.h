#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// One synced object mirrored locally. `version` is the server version this row
// is based on (0 = never synced / brand-new local). The outbox is the set of
// rows with dirty=1: local changes awaiting push, carrying base_version as the
// expectedVersion for optimistic concurrency.
struct SyncObjectRow {
    QString id;
    QString projectId;
    QString type;
    QJsonObject data;
    qint64 version = 0;
    bool deleted = false;
    QString updatedAt;
    QString updatedBy;
    qint64 baseVersion = 0;
};

struct ProjectRow {
    QString id;
    QString name;
    QString description;
    QString role;
    qint64 version = 0;
};

struct SearchHit {
    QString objectId;
    QString kind;
    QString snippet;
};

// Offline-first local store: a SQLite mirror of the cloud library. Owns the
// per-project objects (+ outbox), the sync cursor, a projects cache, and an
// FTS5 full-text index. SyncEngine / LibraryModel / SearchService go through it.
class LibraryDb : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady CONSTANT)
    Q_PROPERTY(bool ftsAvailable READ ftsAvailable CONSTANT)

public:
    explicit LibraryDb(QObject *parent = nullptr);

    bool isReady() const { return m_ready; }
    bool ftsAvailable() const { return m_ftsAvailable; }
    QString path() const { return m_path; }
    QSqlDatabase database() const;

    // ── sync cursor ───────────────────────────────────────────────────
    qint64 lastVersion(const QString &projectId) const;
    void setLastVersion(const QString &projectId, qint64 version);

    // ── objects ───────────────────────────────────────────────────────
    // Apply an object received from the server (clears dirty; server wins).
    void upsertFromServer(const SyncObjectRow &row);
    // Record a local change (sets dirty=1; keeps base_version = last-synced).
    void localUpsert(const QString &projectId, const QString &id,
                     const QString &type, const QJsonObject &data,
                     bool deleted, const QString &authorId);
    bool getObject(const QString &projectId, const QString &id,
                   SyncObjectRow &out) const;
    bool isDirty(const QString &projectId, const QString &id) const;
    QList<SyncObjectRow> dirtyObjects(const QString &projectId) const;
    // After a successful push: clear dirty, advance version/base_version.
    void markPushed(const QString &projectId, const QStringList &ids,
                    qint64 newVersion);
    QList<SyncObjectRow> objectsByType(const QString &projectId,
                                       const QString &type,
                                       bool includeDeleted = false) const;

    // ── projects cache (offline list) ─────────────────────────────────
    void replaceProjects(const QList<ProjectRow> &projects);
    QList<ProjectRow> projects() const;

    // ── full-text index ───────────────────────────────────────────────
    void indexDoc(const QString &objectId, const QString &projectId,
                  const QString &kind, const QString &content);
    void removeDoc(const QString &objectId);
    QList<SearchHit> search(const QString &projectId, const QString &query,
                            int limit = 50) const;

private:
    bool open();
    bool migrate();

    QString m_connName;
    QString m_path;
    bool m_ready = false;
    bool m_ftsAvailable = false;
};
