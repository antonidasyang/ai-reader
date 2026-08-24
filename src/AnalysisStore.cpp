#include "AnalysisStore.h"

#include "AnalysisTypes.h"
#include "AuthController.h"
#include "PayloadCodec.h"
#include "ProjectController.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QHash>

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
    connect(m_sync, &SyncEngine::projectSynced, this,
            [this](const QString &) { emit changed(); });
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

AnalysisRecord AnalysisStore::paperAnalysis(const QString &paperId,
                                            const QString &kind) const
{
    const QList<AnalysisRecord> all = paperAnalysesFor(paperId, kind);
    return all.isEmpty() ? AnalysisRecord() : all.first();
}

QList<AnalysisRecord> AnalysisStore::paperAnalyses(const QString &kind) const
{
    QList<AnalysisRecord> out;
    const QString project = projectId();
    if (project.isEmpty())
        return out;
    const QList<SyncObjectRow> rows =
        m_db->objectsByType(project, Analysis::TypePaperAnalysis);
    QHash<QString, int> byPaper;    // paperId -> index in out
    for (const SyncObjectRow &row : rows) {
        if (row.data.value(QStringLiteral("kind")).toString() != kind)
            continue;
        const AnalysisRecord r = decodePaper(row);
        if (r.paperId.isEmpty() || r.payload.isEmpty())
            continue;
        const auto it = byPaper.constFind(r.paperId);
        if (it == byPaper.constEnd()) {
            byPaper.insert(r.paperId, out.size());
            out.append(r);
            continue;
        }
        // One digest per paper: ours wins, then whichever is newer. A
        // five-person project should pay for a paper once (§ R4).
        AnalysisRecord &cur = out[it.value()];
        if ((r.mine && !cur.mine)
            || (r.mine == cur.mine && r.updatedAt > cur.updatedAt))
            cur = r;
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
