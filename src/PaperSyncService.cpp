#include "PaperSyncService.h"

#include "AuthController.h"
#include "BlockCache.h"
#include "PaperController.h"
#include "ProjectController.h"
#include "Settings.h"
#include "SyncEngine.h"
#include "TranslationCache.h"
#include "TranslationService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QUuid>

namespace {

const QString kType     = QStringLiteral("paper_data");
const QString kBlocks   = QStringLiteral("blocks");
const QString kTransl   = QStringLiteral("translations");
const QString kCodec    = QStringLiteral("zlib-b64");

// Fixed namespace: the same (project, paper, kind, author) must map to the
// same object id on every machine, so one member keeps exactly one artifact
// per paper per kind and their two machines write to the same row.
const QUuid kNs =
    QUuid::fromString(QStringLiteral("{4a1f2e90-7b3c-4d6a-9f21-a1b2c3d40002}"));

// Publish at most this often per paper. A translation run flushes its cache
// roughly once a second; every publish costs a recompress here and a pull for
// every other member, so the trailing edge does the real work.
constexpr int kPublishThrottleMs = 15000;

// Refuse to publish beyond this much base64. Reached only by something like a
// 900-page book: the paper stays perfectly usable, it just isn't shared. The
// server's own limit caps it further when that is the smaller of the two.
constexpr qint64 kMaxPayloadChars = 4 * 1024 * 1024;

QString encodePayload(const QJsonObject &inner)
{
    const QByteArray raw = QJsonDocument(inner).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(qCompress(raw, 9).toBase64());
}

QJsonObject decodePayload(const QJsonObject &data)
{
    const QString payload = data.value(QStringLiteral("payload")).toString();
    if (payload.isEmpty())
        return {};
    const QByteArray b64 = QByteArray::fromBase64(payload.toLatin1());
    const QByteArray raw =
        data.value(QStringLiteral("codec")).toString() == kCodec
            ? qUncompress(b64)
            : b64;
    if (raw.isEmpty())
        return {};
    return QJsonDocument::fromJson(raw).object();
}

} // namespace

PaperSyncService::PaperSyncService(LibraryDb *db, ProjectController *projects,
                                   SyncEngine *sync, AuthController *auth,
                                   PaperController *paper,
                                   TranslationService *translation,
                                   Settings *settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_projects(projects)
    , m_sync(sync)
    , m_auth(auth)
    , m_paper(paper)
    , m_translation(translation)
    , m_settings(settings)
    , m_blocks(paper ? paper->blockCache() : nullptr)
    , m_trans(translation ? translation->cache() : nullptr)
{
    for (auto *t : {&m_blocksTimer, &m_transTimer}) {
        t->setSingleShot(true);
        t->setInterval(kPublishThrottleMs);
    }
    connect(&m_blocksTimer, &QTimer::timeout, this, [this] {
        if (m_blocksPending) {
            m_blocksPending = false;
            if (publishKind(true))
                m_blocksTimer.start();
        }
    });
    connect(&m_transTimer, &QTimer::timeout, this, [this] {
        if (m_transPending) {
            m_transPending = false;
            if (publishKind(false))
                m_transTimer.start();
        }
    });

    // ── adoption: the paper-open path ─────────────────────────────────
    // Both caches announce themselves after loading from disk and before
    // anything acts on their contents, which is exactly the window where a
    // segmentation the project already has can still save the work.
    connect(m_paper, &PaperController::paperCacheReady,
            this, &PaperSyncService::onBlockCacheReady);
    connect(m_translation, &TranslationService::translationCacheReady,
            this, &PaperSyncService::onTranslationCacheReady);
    // ...and again whenever a sync lands, for the paper that was already
    // open when a collaborator's work arrived.
    connect(m_sync, &SyncEngine::projectSynced,
            this, &PaperSyncService::onProjectSynced);
    // The "whose paragraphs are these" line follows the paragraphs on screen:
    // re-segmenting or editing them makes them ours the moment they change,
    // not when the cache write lands 800 ms later.
    connect(m_paper, &PaperController::blocksChanged,
            this, &PaperSyncService::refreshBlocksOrigin);

    // ── publication ───────────────────────────────────────────────────
    if (m_blocks) {
        connect(m_blocks, &BlockCache::contentChanged,
                this, [this] { onCacheWritten(true); });
        connect(m_blocks, &BlockCache::aboutToSwitch,
                this, [this](const QString &) { flushPending(); });
    }
    if (m_trans) {
        connect(m_trans, &TranslationCache::contentChanged,
                this, [this] { onCacheWritten(false); });
        connect(m_trans, &TranslationCache::aboutToSwitch,
                this, [this](const QString &) { flushPending(); });
    }
    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &PaperSyncService::flushPending);

    const auto restate = [this] { emit stateChanged(); };
    connect(m_auth, &AuthController::authenticatedChanged, this, restate);
    connect(m_projects, &ProjectController::currentChanged, this, [this] {
        emit stateChanged();
        // A different project holds different work for the same file.
        onProjectSynced();
    });
    if (m_settings)
        connect(m_settings, &Settings::sharePaperDataChanged, this, restate);
}

bool PaperSyncService::sharing() const
{
    // A server that never advertised a push limit predates these objects: its
    // body limit is Express' 100 KB default, well under one segmented paper,
    // and a rejected batch would stall the outbox for ordinary library edits
    // too. Wait for it to be upgraded rather than jam the queue.
    return m_auth->authenticated() && m_projects->canWrite()
           && !m_projects->currentId().isEmpty()
           && m_sync->serverPushLimit() > 0
           && (!m_settings || m_settings->sharePaperData());
}

QString PaperSyncService::artifactId(const QString &paperId,
                                     const QString &kind,
                                     const QString &author) const
{
    const QString name = m_projects->currentId() + QChar('|') + paperId
                         + QChar('|') + kind + QChar('|') + author;
    return QUuid::createUuidV5(kNs, name.toUtf8()).toString(QUuid::WithoutBraces);
}

QJsonObject PaperSyncService::loadPayload(const PaperDataRef &ref) const
{
    SyncObjectRow row;
    if (!m_db->getObject(ref.projectId, ref.objectId, row) || row.deleted)
        return {};
    return decodePayload(row.data);
}

// ─────────────────────────────────────────────────────────── adoption ──

void PaperSyncService::onBlockCacheReady(const QString &paperId)
{
    adoptBlocks(paperId);
    refreshBlocksOrigin();
    offerExisting(true);
}

void PaperSyncService::refreshBlocksOrigin()
{
    const bool adopted = m_blocks && !m_blocks->owned();
    const QString origin = adopted ? m_blocks->origin() : QString();
    QString label = adopted ? m_blocks->originLabel() : QString();
    if (adopted && label.isEmpty())
        label = tr("a collaborator");
    if (origin == m_blocksOrigin && label == m_blocksOriginLabel)
        return;
    // Segmenting or editing the paragraphs ourselves retires the line saying
    // whose they were.
    if (origin.isEmpty())
        setNotice(QString());
    m_blocksOrigin = origin;
    m_blocksOriginLabel = label;
    emit stateChanged();
}

void PaperSyncService::onTranslationCacheReady(const QString &paperId)
{
    if (paperId != m_mergedPaper) {
        m_mergedPaper = paperId;
        m_mergedRev.clear();
    }
    adoptTranslations(paperId);
    offerExisting(false);
}

void PaperSyncService::onProjectSynced()
{
    const QString paperId = m_paper ? m_paper->paperId() : QString();
    if (paperId.isEmpty())
        return;
    // A collaborator's work can land at any moment; take it only where this
    // machine still has a gap, and only into what isn't already on screen.
    if (adoptBlocks(paperId)) {
        m_paper->applyCachedBlocks();
        refreshBlocksOrigin();
    }
    if (adoptTranslations(paperId) > 0)
        m_translation->refreshFromCache();
}

bool PaperSyncService::adoptBlocks(const QString &paperId)
{
    if (!m_blocks || paperId.isEmpty() || m_blocks->paperId() != paperId)
        return false;
    // Our own segmentation, or one we already edited, is never replaced.
    if (m_blocks->hasBlocks() && m_blocks->owned())
        return false;
    const QString projectId = m_projects->currentId();
    if (projectId.isEmpty())
        return false;

    const QList<PaperDataRef> refs =
        m_db->paperData(projectId, paperId, kBlocks);
    if (refs.isEmpty())
        return false;
    const QString me = m_auth->userId();

    // Ours from another machine first — that is the same account's own work
    // coming home, not somebody else's opinion of where the paragraphs are.
    // Otherwise the freshest collaborator (paperData() is newest-first).
    PaperDataRef pick;
    for (const PaperDataRef &r : refs) {
        if (r.count <= 0)
            continue;
        if (!me.isEmpty() && r.author == me) {
            pick = r;
            break;
        }
        if (pick.objectId.isEmpty())
            pick = r;
    }
    if (pick.objectId.isEmpty())
        return false;

    const bool mine = !me.isEmpty() && pick.author == me;
    const QString label =
        pick.authorEmail.isEmpty() ? tr("a collaborator") : pick.authorEmail;
    if (!m_blocks->adopt(loadPayload(pick), mine ? QString() : pick.author,
                         mine ? QString() : label, pick.updatedAt))
        return false;

    setNotice(mine
                  ? tr("Restored %1 paragraphs you segmented on another machine.")
                        .arg(m_blocks->count())
                  : tr("Using %1 paragraphs segmented by %2.")
                        .arg(m_blocks->count()).arg(label));
    return true;
}

int PaperSyncService::adoptTranslations(const QString &paperId)
{
    if (!m_trans || paperId.isEmpty() || m_trans->paperId() != paperId)
        return 0;
    const QString projectId = m_projects->currentId();
    if (projectId.isEmpty())
        return 0;

    const QList<PaperDataRef> refs =
        m_db->paperData(projectId, paperId, kTransl);
    if (refs.isEmpty())
        return 0;
    const QString me = m_auth->userId();

    // Our own artifact always, then one collaborator's — whichever member's
    // segmentation we are reading against, since translation entries are
    // keyed by block id and only that member's line up. mergeEntries never
    // overwrites, so a paragraph we translated ourselves stays ours.
    PaperDataRef ours, theirs;
    for (const PaperDataRef &r : refs) {
        if (r.count <= 0)
            continue;
        if (!me.isEmpty() && r.author == me) {
            if (ours.objectId.isEmpty())
                ours = r;
        } else if (theirs.objectId.isEmpty()
                   && (m_blocksOrigin.isEmpty()
                       || r.author == m_blocksOrigin)) {
            theirs = r;
        }
    }

    const auto mergeOnce = [this, &me](const PaperDataRef &r) -> int {
        if (r.objectId.isEmpty()
            || m_mergedRev.value(r.objectId) == r.updatedAt)
            return 0;
        m_mergedRev.insert(r.objectId, r.updatedAt);
        // Our own entries from another machine are ours; only a real
        // collaborator's get a name attached for the reading pane.
        const bool mine = !me.isEmpty() && r.author == me;
        const QString donor =
            mine ? QString()
                 : (r.authorEmail.isEmpty() ? tr("a collaborator")
                                            : r.authorEmail);
        return m_trans->mergeEntries(
            loadPayload(r).value(QStringLiteral("entries")).toArray(), donor);
    };

    const int added = mergeOnce(ours) + mergeOnce(theirs);

    if (added > 0) {
        const bool onlyOurs = theirs.objectId.isEmpty();
        setNotice(onlyOurs
                      ? tr("Restored %1 translated paragraphs from your other "
                           "machine.").arg(added)
                      : tr("Picked up %1 translated paragraphs already done in "
                           "this project.").arg(added));
    }
    return added;
}

// ────────────────────────────────────────────────────────── publishing ──

void PaperSyncService::onCacheWritten(bool blocks)
{
    if (blocks)
        refreshBlocksOrigin();
    if (!sharing())
        return;
    QTimer &timer = blocks ? m_blocksTimer : m_transTimer;
    bool &pending = blocks ? m_blocksPending : m_transPending;
    pending = true;
    if (timer.isActive())
        return;                       // the trailing edge will carry it
    pending = false;
    if (publishKind(blocks))          // leading edge: the common single edit
        timer.start();
}

void PaperSyncService::offerExisting(bool blocks)
{
    if (!sharing())
        return;
    (blocks ? m_blocksPending : m_transPending) = true;
    QTimer &timer = blocks ? m_blocksTimer : m_transTimer;
    if (!timer.isActive())
        timer.start();
}

void PaperSyncService::flushPending()
{
    for (bool blocks : {true, false}) {
        bool &pending = blocks ? m_blocksPending : m_transPending;
        if (!pending)
            continue;
        pending = false;
        (blocks ? m_blocksTimer : m_transTimer).stop();
        publishKind(blocks);
    }
}

bool PaperSyncService::publishKind(bool blocks)
{
    if (!sharing())
        return false;
    if (blocks) {
        if (!m_blocks || m_blocks->paperId().isEmpty())
            return false;
        // Adopted paragraphs belong to whoever segmented them; re-publishing
        // would just give the project a second copy under our name.
        if (!m_blocks->owned() || m_blocks->count() == 0)
            return false;
        return putArtifact(kBlocks, m_blocks->paperId(), m_blocks->toJson(),
                           m_blocks->count());
    }
    if (!m_trans || m_trans->paperId().isEmpty())
        return false;
    const QJsonArray own = m_trans->ownEntriesJson();
    if (own.isEmpty())
        return false;
    const QJsonObject inner{{QStringLiteral("paperId"), m_trans->paperId()},
                            {QStringLiteral("entries"), own}};
    return putArtifact(kTransl, m_trans->paperId(), inner, own.size());
}

bool PaperSyncService::putArtifact(const QString &kind, const QString &paperId,
                                   const QJsonObject &inner, int n)
{
    const QString author = m_auth->userId();
    if (author.isEmpty())
        return false;
    const QString payload = encodePayload(inner);
    const qint64 limit = qMin(kMaxPayloadChars, m_sync->serverPushLimit() / 2);
    if (payload.size() > limit) {
        qWarning() << "PaperSyncService: not sharing" << kind << "for" << paperId
                   << "-" << payload.size() << "chars exceeds the" << limit
                   << "char limit";
        setNotice(tr("This paper is too large to share (%1 MB); it stays on "
                     "this machine.").arg(payload.size() / (1024 * 1024)));
        return false;
    }

    const QString id = artifactId(paperId, kind, author);
    // Re-putting an identical payload would mark the row dirty, bump the
    // project clock and make every other member pull it again for nothing.
    SyncObjectRow existing;
    if (m_db->getObject(m_projects->currentId(), id, existing)
        && !existing.deleted
        && existing.data.value(QStringLiteral("payload")).toString() == payload)
        return false;

    QJsonObject data{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("author"), author},
        {QStringLiteral("authorEmail"), m_auth->userEmail()},
        {QStringLiteral("codec"), kCodec},
        {QStringLiteral("n"), n},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("updatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_sync->putObject(kType, id, data);
    return true;
}

void PaperSyncService::setNotice(const QString &text)
{
    if (text == m_notice)
        return;
    m_notice = text;
    emit noticeChanged();
}

void PaperSyncService::dismissNotice()
{
    setNotice(QString());
}
