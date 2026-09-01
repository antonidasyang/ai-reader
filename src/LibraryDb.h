#pragma once

#include <QHash>
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

// One `paper_data` artifact (a member's segmentation or translations for a
// paper) without its payload. These blobs are far too big to scan on every
// paper open, so SyncEngine keeps this side index as objects are applied and
// PaperSyncService looks a paper up by key.
struct PaperDataRef {
    QString objectId;
    QString projectId;
    QString paperId;
    QString kind;          // "blocks" | "translations"
    QString author;
    QString authorEmail;
    int count = 0;         // blocks / entries in the payload
    QString updatedAt;
};

// One stored interpretation, without the interpretation.
//
// Same idea as PaperDataRef, and for the same reason: a paper_analysis row
// carries a whole close reading plus the last two versions of it, several
// hundred kilobytes of JSON, and almost everything that asks about
// interpretations wants to know which papers have one, whose it is and when
// it was written -- not what it says. Scanning the objects to answer that
// parsed megabytes per question.
struct PaperAnalysisRef {
    QString objectId;
    QString projectId;
    QString paperId;
    QString kind;          // "quick" | "deep"
    QString author;
    QString authorEmail;
    QString status;
    QString title;
    QString updatedAt;
};

// Offline-first local store: a SQLite mirror of the cloud library. Owns the
// per-project objects (+ outbox), the sync cursor, a projects cache, and an
// FTS5 full-text index. SyncEngine / LibraryModel / SearchService go through it.
//
// It also knows whose data it holds. Every row here is scoped by project and
// by nothing else, so without that the library pane happily served the last
// user's papers to whoever opened the app next. The account gate below is the
// one place that is decided: `canRead` guards every content read, and the
// answer is recorded in the database itself rather than in QSettings, which
// follows an account to another machine.
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
    // The outbox, oldest first. `maxCount` / `maxBytes` bound one push
    // batch (0 = unbounded); at least one object always comes back so a
    // single large one can never wedge the queue.
    QList<SyncObjectRow> dirtyObjects(const QString &projectId,
                                      int maxCount = 0,
                                      qint64 maxBytes = 0) const;
    int dirtyCount(const QString &projectId) const;
    // After a successful push: clear dirty, advance version/base_version.
    void markPushed(const QString &projectId, const QStringList &ids,
                    qint64 newVersion);
    QList<SyncObjectRow> objectsByType(const QString &projectId,
                                       const QString &type,
                                       bool includeDeleted = false) const;

    // ── projects cache (offline list) ─────────────────────────────────
    void replaceProjects(const QList<ProjectRow> &projects);
    QList<ProjectRow> projects() const;
    // Drop every local trace of a project (objects, sync cursor, search
    // index). The server cascades its side on delete; without this the
    // rows would linger forever as unreachable orphans, since every
    // query is scoped by the current project id and a deleted project
    // can never be current again.
    void purgeProject(const QString &projectId);

    // ── paper-data index ──────────────────────────────────────────────
    void indexPaperData(const PaperDataRef &ref);
    void removePaperData(const QString &objectId);
    // Every member's artifact of `kind` for this paper, newest first.
    QList<PaperDataRef> paperData(const QString &projectId,
                                  const QString &paperId,
                                  const QString &kind) const;

    // ── interpretation index ──────────────────────────────────────────
    void indexPaperAnalysis(const PaperAnalysisRef &ref);
    void dropPaperAnalysisIndex(const QString &objectId);
    // Every stored interpretation of `kind` in the project, metadata only.
    QList<PaperAnalysisRef> paperAnalysisRefs(const QString &projectId,
                                              const QString &kind) const;

    // ── the account gate ──────────────────────────────────────────────
    // The account this copy of the store holds data for. Empty until the
    // first session opens: a database that predates the gate adopts whoever
    // signs in first rather than locking its own user out.
    QString storeOwner() const { return m_owner; }
    // The account that claimed one project, empty when nobody has. First
    // claim wins: a project is never re-assigned to a second account, so
    // signing in as somebody else can never uncover the first user's rows.
    QString projectOwner(const QString &projectId) const
    {
        return m_claims.value(projectId);
    }
    // The account whose session this store believes is open — whoever
    // signed in, and still them after a restart with no network, because
    // "offline" is not "signed out". Cleared by closeSession() only.
    QString sessionUser() const { return m_sessionUser; }

    // A session opened for `userId`: an unclaimed store adopts them along
    // with everything already in it, and reads are answered for what they
    // hold. Cheap and idempotent — the sign-in path may call it twice.
    void openSession(const QString &userId);
    // The session ended: a sign-out, or a refresh token the server refused.
    // The gate shuts until somebody signs in again. Nothing is deleted, so
    // an un-pushed outbox is still there when its owner comes back.
    void closeSession();
    // The projects the server just listed for `userId` — the only proof of
    // membership a client gets. Claims the ones nobody holds yet.
    void claimProjects(const QStringList &projectIds, const QString &userId);
    // The chokepoint: may this session read `projectId`'s rows at all?
    bool canRead(const QString &projectId) const;
    // Why not, for the empty state to say something true: "signed-out",
    // "other-account", or empty when reads are being answered.
    QString lockReason() const;

    // ── full-text index ───────────────────────────────────────────────
    void indexDoc(const QString &objectId, const QString &projectId,
                  const QString &kind, const QString &content);
    void removeDoc(const QString &objectId);
    QList<SearchHit> search(const QString &projectId, const QString &query,
                            int limit = 50) const;

private:
    bool open();
    bool migrate();
    // Gate state lives in the store: `store_meta` for the owner and the
    // open session, `sync_state.owner_id` for the per-project claims. Read
    // once at startup and kept in step by the writers below, because
    // canRead() is on the path of every single row read.
    void loadGateState();
    void setMeta(const QString &key, const QString &value);
    // Every project id the store has any trace of — what an adopting
    // session claims in one go.
    QStringList knownProjectIds() const;

    QString m_connName;
    QString m_path;
    bool m_ready = false;
    bool m_ftsAvailable = false;

    QString m_owner;                      // store_meta/owner_user_id
    QString m_sessionUser;                // store_meta/session_user_id
    QHash<QString, QString> m_claims;     // project id -> account
};
