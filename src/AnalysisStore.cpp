#include "AnalysisStore.h"
#include "Stall.h"

#include "AnalysisTypes.h"
#include "AuthController.h"
#include "PayloadCodec.h"
#include "ProjectController.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace {

// Keep this many superseded versions of a project-wide analysis. Enough
// to answer "what did it say before I regenerated it" (§16 历史版本)
// without letting one object grow without bound.
constexpr int kHistoryDepth = 3;

// Per-paper analyses keep fewer: they are per member, so a regeneration only
// ever costs the person who asked for it -- one step back is enough.
constexpr int kPaperHistoryDepth = 2;

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

AnalysisStore::AnalysisStore(LibraryDb *db, ProjectController *projects,
                             SyncEngine *sync, AuthController *auth,
                             QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_projects(projects)
    , m_sync(sync)
    , m_auth(auth)
{
    // Any of these can add, replace or remove an analysis, so the index of
    // whose-is-where has to be rebuilt after them.
    connect(this, &AnalysisStore::changed, this,
            [this] { m_othersIndexValid = false; });
    connect(m_sync, &SyncEngine::projectSynced, this,
            [this](const QString &) {
                Stall::Mark mark("telling everything the project changed");
                emit changed();
            });
    connect(m_projects, &ProjectController::currentChanged, this,
            &AnalysisStore::changed);
    connect(m_auth, &AuthController::authenticatedChanged, this,
            &AnalysisStore::changed);
}

QString AnalysisStore::projectId() const { return m_projects->currentId(); }
QString AnalysisStore::userId() const { return m_auth->userId(); }
QString AnalysisStore::userEmail() const { return m_auth->userEmail(); }

bool AnalysisStore::canWrite() const
{
    return m_auth->authenticated() && !projectId().isEmpty()
           && m_projects->canWrite();
}

// ── per-paper ────────────────────────────────────────────────────────

bool AnalysisStore::putPaperAnalysis(const QString &paperId,
                                     const QString &kind,
                                     const QJsonObject &payload,
                                     const QString &model,
                                     const QString &inputHash,
                                     const QString &status,
                                     const QString &error,
                                     const QString &title)
{
    Stall::Mark mark("packing an interpretation to store");
    if (!canWrite() || paperId.isEmpty())
        return false;
    const QString project = projectId();
    const QString author = userId();
    const QString id = Analysis::paperAnalysisId(project, paperId, kind, author);

    // Keep what is there now, so "regenerate" is not a one-way door.
    QJsonArray history;
    SyncObjectRow existing;
    if (m_db->getObject(project, id, existing) && !existing.deleted) {
        history = existing.data.value(QStringLiteral("history")).toArray();
        const QString prev =
            existing.data.value(QStringLiteral("payload")).toString();
        if (!prev.isEmpty()) {
            history.prepend(QJsonObject{
                {QStringLiteral("updatedAt"),
                 existing.data.value(QStringLiteral("updatedAt"))},
                {QStringLiteral("model"), existing.data.value(QStringLiteral("model"))},
                {QStringLiteral("codec"), existing.data.value(QStringLiteral("codec"))},
                {QStringLiteral("payload"), prev}});
        }
        while (history.size() > kPaperHistoryDepth)
            history.removeLast();
    }

    QJsonObject data{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("author"), author},
        {QStringLiteral("authorEmail"), userEmail()},
        {QStringLiteral("model"), model},
        {QStringLiteral("inputHash"), inputHash},
        {QStringLiteral("status"), status},
        {QStringLiteral("title"), title},
        {QStringLiteral("codec"), PayloadCodec::codecName()},
        {QStringLiteral("payload"), PayloadCodec::encode(payload)},
        {QStringLiteral("updatedAt"), nowIso()},
        {QStringLiteral("history"), history}};
    if (!error.isEmpty())
        data.insert(QStringLiteral("error"), error);

    m_sync->putObject(Analysis::TypePaperAnalysis, id, data);
    emit changed();
    return true;
}

AnalysisRecord AnalysisStore::decodePaper(const SyncObjectRow &row) const
{
    AnalysisRecord r;
    r.id = row.id;
    r.paperId = row.data.value(QStringLiteral("paperId")).toString();
    r.kind = row.data.value(QStringLiteral("kind")).toString();
    r.author = row.data.value(QStringLiteral("author")).toString();
    r.authorEmail = row.data.value(QStringLiteral("authorEmail")).toString();
    r.model = row.data.value(QStringLiteral("model")).toString();
    r.updatedAt = row.data.value(QStringLiteral("updatedAt")).toString();
    r.inputHash = row.data.value(QStringLiteral("inputHash")).toString();
    r.status = row.data.value(QStringLiteral("status")).toString();
    r.error = row.data.value(QStringLiteral("error")).toString();
    r.title = row.data.value(QStringLiteral("title")).toString();
    r.payload = PayloadCodec::decode(row.data);
    r.mine = !r.author.isEmpty() && r.author == userId();
    r.valid = true;
    return r;
}

QList<AnalysisRecord> AnalysisStore::paperAnalysesFor(const QString &paperId,
                                                      const QString &kind) const
{
    Stall::Mark mark("decoding one paper's interpretations");
    QList<AnalysisRecord> out;
    const QString project = projectId();
    if (project.isEmpty() || paperId.isEmpty())
        return out;
    const QList<SyncObjectRow> rows =
        m_db->objectsByType(project, Analysis::TypePaperAnalysis);
    for (const SyncObjectRow &row : rows) {
        if (row.data.value(QStringLiteral("paperId")).toString() != paperId)
            continue;
        if (row.data.value(QStringLiteral("kind")).toString() != kind)
            continue;
        out.append(decodePaper(row));
    }
    std::sort(out.begin(), out.end(),
              [](const AnalysisRecord &a, const AnalysisRecord &b) {
                  if (a.mine != b.mine)
                      return a.mine;          // ours first
                  return a.updatedAt > b.updatedAt;
              });
    return out;
}

void AnalysisStore::ensureOthersIndex() const
{
    if (m_othersIndexValid)
        return;
    m_othersIndex.clear();
    m_othersIndexValid = true;
    const QString project = projectId();
    if (project.isEmpty())
        return;
    const QString me = userId();
    for (const QString &kind : {Analysis::KindQuick, Analysis::KindDeep}) {
        QHash<QString, QString> &byPaper = m_othersIndex[kind];
        QHash<QString, QString> newest;   // paperId -> updatedAt
        // Metadata only: whose it is and when, which is the whole question.
        // This used to parse every stored reading -- payload, history and
        // all -- to answer it.
        for (const PaperAnalysisRef &r : m_db->paperAnalysisRefs(project, kind)) {
            if (r.author.isEmpty() || r.author == me)
                continue;               // ours is found by its id
            if (r.paperId.isEmpty())
                continue;
            // Newest wins, so a paper several members read shows the last word.
            const auto seen = newest.constFind(r.paperId);
            if (seen != newest.constEnd() && seen.value() >= r.updatedAt)
                continue;
            newest.insert(r.paperId, r.updatedAt);
            byPaper.insert(r.paperId, r.objectId);
        }
    }
}

AnalysisRecord AnalysisStore::paperAnalysis(const QString &paperId,
                                            const QString &kind) const
{
    const QString project = projectId();
    if (project.isEmpty() || paperId.isEmpty())
        return {};

    // Ours is a primary-key lookup: the id is derived from the project, the
    // paper, the kind and the author, so there is nothing to search for.
    SyncObjectRow row;
    if (m_db->getObject(project,
                        Analysis::paperAnalysisId(project, paperId, kind,
                                                  userId()),
                        row)
        && !row.deleted)
        return decodePaper(row);

    // Otherwise a collaborator may have one. That needs a search, which is
    // done once per change rather than once per paper the reader opens.
    ensureOthersIndex();
    const QString id = m_othersIndex.value(kind).value(paperId);
    if (id.isEmpty())
        return {};
    if (!m_db->getObject(project, id, row) || row.deleted)
        return {};
    return decodePaper(row);
}

QList<PaperAnalysisRef>
AnalysisStore::paperAnalysisRefs(const QString &kind) const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    return m_db->paperAnalysisRefs(project, kind);
}

int AnalysisStore::paperAnalysisCount(const QString &kind) const
{
    QSet<QString> papers;
    for (const PaperAnalysisRef &r : paperAnalysisRefs(kind))
        if (!r.paperId.isEmpty())
            papers.insert(r.paperId);
    return papers.size();
}

QList<AnalysisRecord> AnalysisStore::paperAnalyses(const QString &kind) const
{
    Stall::Mark mark("decoding every interpretation in the project");
    QList<AnalysisRecord> out;
    const QString project = projectId();
    if (project.isEmpty())
        return out;

    // Which rows exist is answered from the index; only the ones that will
    // be returned are fetched, and only the ones whose stamp moved since we
    // last looked are decoded again.
    QHash<QString, PaperAnalysisRef> best;   // paperId -> the one that wins
    const QString me = userId();
    for (const PaperAnalysisRef &r : m_db->paperAnalysisRefs(project, kind)) {
        if (r.paperId.isEmpty())
            continue;
        const bool mine = !r.author.isEmpty() && r.author == me;
        const auto it = best.constFind(r.paperId);
        if (it == best.constEnd()) {
            best.insert(r.paperId, r);
            continue;
        }
        // One digest per paper: ours wins, then whichever is newer. A
        // five-person project should pay for a paper once (§ R4).
        const bool curMine = !it->author.isEmpty() && it->author == me;
        if ((mine && !curMine)
            || (mine == curMine && r.updatedAt > it->updatedAt))
            best.insert(r.paperId, r);
    }

    out.reserve(best.size());
    for (const PaperAnalysisRef &r : std::as_const(best)) {
        const auto cached = m_decodeCache.constFind(r.objectId);
        if (cached != m_decodeCache.constEnd()
            && cached->updatedAt == r.updatedAt) {
            if (!cached->record.payload.isEmpty())
                out.append(cached->record);
            continue;
        }
        SyncObjectRow row;
        if (!m_db->getObject(project, r.objectId, row) || row.deleted)
            continue;
        const AnalysisRecord rec = decodePaper(row);
        // Bounded, but not below the size of one answer: a cap smaller than
        // the project is a cache that clears itself on every call and
        // therefore never hits. A quick digest is a few kilobytes decoded,
        // so five hundred of them is a few megabytes.
        if (m_decodeCache.size() > 512)
            m_decodeCache.clear();
        m_decodeCache.insert(r.objectId, Decoded{r.updatedAt, rec});
        if (!rec.payload.isEmpty())
            out.append(rec);
    }
    return out;
}

void AnalysisStore::removePaperAnalysis(const QString &paperId,
                                        const QString &kind)
{
    if (!canWrite())
        return;
    const QString id =
        Analysis::paperAnalysisId(projectId(), paperId, kind, userId());
    SyncObjectRow existing;
    if (!m_db->getObject(projectId(), id, existing) || existing.deleted)
        return;
    m_sync->putObject(Analysis::TypePaperAnalysis, id, existing.data, true);
    emit changed();
}

QJsonArray AnalysisStore::paperHistoryIndex(const QString &paperId,
                                            const QString &kind) const
{
    const QString project = projectId();
    if (project.isEmpty() || paperId.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::paperAnalysisId(project, paperId, kind,
                                                   userId()),
                         row)
        || row.deleted)
        return {};
    QJsonArray out;
    for (const QJsonValue &v : row.data.value(QStringLiteral("history")).toArray()) {
        const QJsonObject h = v.toObject();
        out.append(QJsonObject{
            {QStringLiteral("updatedAt"), h.value(QStringLiteral("updatedAt"))},
            {QStringLiteral("model"), h.value(QStringLiteral("model"))}});
    }
    return out;
}

bool AnalysisStore::restorePaperVersion(const QString &paperId,
                                        const QString &kind, int index)
{
    const QString project = projectId();
    if (project.isEmpty())
        return false;
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::paperAnalysisId(project, paperId, kind,
                                                   userId()),
                         row)
        || row.deleted)
        return false;
    const QJsonArray hist = row.data.value(QStringLiteral("history")).toArray();
    if (index < 0 || index >= hist.size())
        return false;
    const QJsonObject payload = PayloadCodec::decode(hist.at(index).toObject());
    if (payload.isEmpty())
        return false;
    return putPaperAnalysis(
        paperId, kind, payload,
        row.data.value(QStringLiteral("model")).toString(),
        row.data.value(QStringLiteral("inputHash")).toString(),
        row.data.value(QStringLiteral("status")).toString(), QString(),
        row.data.value(QStringLiteral("title")).toString());
}

// ── project-wide ─────────────────────────────────────────────────────

AnalysisRecord AnalysisStore::decodeLibrary(const SyncObjectRow &row) const
{
    AnalysisRecord r;
    r.id = row.id;
    r.kind = row.data.value(QStringLiteral("kind")).toString();
    r.scopeHash = row.data.value(QStringLiteral("scopeHash")).toString();
    r.author = row.data.value(QStringLiteral("generatedBy")).toString();
    r.authorEmail =
        row.data.value(QStringLiteral("generatedByEmail")).toString();
    r.model = row.data.value(QStringLiteral("model")).toString();
    r.updatedAt = row.data.value(QStringLiteral("updatedAt")).toString();
    r.inputHash = row.data.value(QStringLiteral("inputHash")).toString();
    r.paperCount = row.data.value(QStringLiteral("paperCount")).toInt();
    r.payload = PayloadCodec::decode(row.data);
    r.mine = !r.author.isEmpty() && r.author == userId();
    r.valid = true;
    return r;
}

bool AnalysisStore::putLibraryAnalysis(const QString &kind,
                                       const QString &scopeHash,
                                       const QJsonObject &payload,
                                       const QString &model,
                                       const QString &inputHash,
                                       int paperCount)
{
    if (!canWrite())
        return false;
    const QString project = projectId();
    const QString id = Analysis::libraryAnalysisId(project, kind, scopeHash);

    // Push what is there now onto the history stack before overwriting —
    // regenerating a shared analysis must never be a silent loss for the
    // other members.
    QJsonArray history;
    SyncObjectRow existing;
    if (m_db->getObject(project, id, existing) && !existing.deleted) {
        history = existing.data.value(QStringLiteral("history")).toArray();
        const QString prevPayload =
            existing.data.value(QStringLiteral("payload")).toString();
        if (!prevPayload.isEmpty()) {
            QJsonObject prev{
                {QStringLiteral("generatedAt"),
                 existing.data.value(QStringLiteral("updatedAt"))},
                {QStringLiteral("generatedBy"),
                 existing.data.value(QStringLiteral("generatedBy"))},
                {QStringLiteral("generatedByEmail"),
                 existing.data.value(QStringLiteral("generatedByEmail"))},
                {QStringLiteral("model"),
                 existing.data.value(QStringLiteral("model"))},
                {QStringLiteral("codec"),
                 existing.data.value(QStringLiteral("codec"))},
                {QStringLiteral("payload"), prevPayload}};
            history.prepend(prev);
        }
        while (history.size() > kHistoryDepth)
            history.removeLast();
    }

    QJsonObject data{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("scopeHash"), scopeHash},
        {QStringLiteral("generatedBy"), userId()},
        {QStringLiteral("generatedByEmail"), userEmail()},
        {QStringLiteral("model"), model},
        {QStringLiteral("inputHash"), inputHash},
        {QStringLiteral("paperCount"), paperCount},
        {QStringLiteral("codec"), PayloadCodec::codecName()},
        {QStringLiteral("payload"), PayloadCodec::encode(payload)},
        {QStringLiteral("updatedAt"), nowIso()},
        {QStringLiteral("history"), history}};

    m_sync->putObject(Analysis::TypeLibraryAnalysis, id, data);
    emit changed();
    return true;
}

AnalysisRecord AnalysisStore::libraryAnalysis(const QString &kind,
                                              const QString &scopeHash) const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    const QString id = Analysis::libraryAnalysisId(project, kind, scopeHash);
    SyncObjectRow row;
    if (!m_db->getObject(project, id, row) || row.deleted)
        return {};
    return decodeLibrary(row);
}

QJsonArray AnalysisStore::libraryHistoryIndex(const QString &kind,
                                              const QString &scopeHash) const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::libraryAnalysisId(project, kind, scopeHash),
                         row)
        || row.deleted)
        return {};
    QJsonArray out;
    const QJsonArray hist = row.data.value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : hist) {
        const QJsonObject h = v.toObject();
        out.append(QJsonObject{
            {QStringLiteral("generatedAt"), h.value(QStringLiteral("generatedAt"))},
            {QStringLiteral("generatedBy"), h.value(QStringLiteral("generatedBy"))},
            {QStringLiteral("generatedByEmail"),
             h.value(QStringLiteral("generatedByEmail"))},
            {QStringLiteral("model"), h.value(QStringLiteral("model"))}});
    }
    return out;
}

QJsonObject AnalysisStore::libraryHistoryPayload(const QString &kind,
                                                 const QString &scopeHash,
                                                 int index) const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::libraryAnalysisId(project, kind, scopeHash),
                         row)
        || row.deleted)
        return {};
    const QJsonArray hist = row.data.value(QStringLiteral("history")).toArray();
    if (index < 0 || index >= hist.size())
        return {};
    return PayloadCodec::decode(hist.at(index).toObject());
}

bool AnalysisStore::restoreLibraryVersion(const QString &kind,
                                          const QString &scopeHash, int index)
{
    const QJsonObject payload = libraryHistoryPayload(kind, scopeHash, index);
    if (payload.isEmpty())
        return false;
    const AnalysisRecord cur = libraryAnalysis(kind, scopeHash);
    return putLibraryAnalysis(kind, scopeHash, payload, cur.model,
                              cur.inputHash, cur.paperCount);
}

// ── research profile ─────────────────────────────────────────────────

QJsonObject AnalysisStore::profile() const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project, Analysis::projectProfileId(project), row)
        || row.deleted)
        return {};
    return row.data;
}

bool AnalysisStore::putProfile(const QJsonObject &fields)
{
    if (!canWrite())
        return false;
    const QString project = projectId();
    QJsonObject data = fields;
    data.insert(QStringLiteral("updatedAt"), nowIso());
    data.insert(QStringLiteral("updatedByEmail"), userEmail());
    m_sync->putObject(Analysis::TypeProjectProfile,
                      Analysis::projectProfileId(project), data);
    emit changed();
    return true;
}

// ── personal notes ───────────────────────────────────────────────────

QJsonObject AnalysisStore::note(const QString &paperId) const
{
    const QString project = projectId();
    if (project.isEmpty() || paperId.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::noteId(project, paperId, userId()), row)
        || row.deleted)
        return {};
    return PayloadCodec::decode(row.data);
}

bool AnalysisStore::putNote(const QString &paperId, const QJsonObject &payload)
{
    if (!canWrite() || paperId.isEmpty())
        return false;
    const QString project = projectId();
    QJsonObject data{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("author"), userId()},
        {QStringLiteral("authorEmail"), userEmail()},
        {QStringLiteral("codec"), PayloadCodec::codecName()},
        {QStringLiteral("payload"), PayloadCodec::encode(payload)},
        {QStringLiteral("updatedAt"), nowIso()}};
    m_sync->putObject(Analysis::TypeAnalysisNote,
                      Analysis::noteId(project, paperId, userId()), data);
    emit changed();
    return true;
}

// ── the comparison basket ────────────────────────────────────────────

QJsonArray AnalysisStore::compareBasket() const
{
    const QString project = projectId();
    if (project.isEmpty())
        return {};
    SyncObjectRow row;
    if (!m_db->getObject(project,
                         Analysis::compareBasketId(project, userId()), row)
        || row.deleted)
        return {};
    return row.data.value(QStringLiteral("papers")).toArray();
}

bool AnalysisStore::putCompareBasket(const QJsonArray &papers)
{
    if (!canWrite())
        return false;
    const QString project = projectId();
    m_sync->putObject(
        Analysis::TypeCompareBasket,
        Analysis::compareBasketId(project, userId()),
        QJsonObject{{QStringLiteral("author"), userId()},
                    {QStringLiteral("authorEmail"), userEmail()},
                    {QStringLiteral("papers"), papers},
                    {QStringLiteral("updatedAt"), nowIso()}});
    emit changed();
    return true;
}
