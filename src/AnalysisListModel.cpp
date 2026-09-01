#include "AnalysisListModel.h"
#include "Stall.h"

#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LibraryModel.h"
#include "ProjectController.h"

#include <QJsonArray>
#include <QSet>
#include <QJsonObject>

AnalysisListModel::AnalysisListModel(LibraryDb *db, LibraryModel *library,
                                     ProjectController *projects,
                                     AnalysisStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
    , m_library(library)
    , m_projects(projects)
    , m_store(store)
{
    connect(m_projects, &ProjectController::currentChanged, this, [this]() {
        clearRuntime();
        reload();
    });
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(250);
    connect(&m_reloadTimer, &QTimer::timeout, this, &AnalysisListModel::reload);
    connect(m_store, &AnalysisStore::changed, this,
            [this]() { m_reloadTimer.start(); });
    reload();
}

int AnalysisListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QHash<int, QByteArray> AnalysisListModel::roleNames() const
{
    return {{ItemIdRole, "itemId"},
            {PaperIdRole, "paperId"},
            {TitleRole, "title"},
            {StateRole, "analysisState"},
            {StaleRole, "stale"},
            {ErrorRole, "error"},
            {OneLinerRole, "oneLiner"},
            {RelevanceRole, "relevance"},
            {AdviceRole, "advice"},
            {AuthorEmailRole, "authorEmail"},
            {MineRole, "mine"},
            {ToReadRole, "toRead"},
            {ExcludedRole, "excluded"},
            {HasFileRole, "hasFile"},
            {DeepStateRole, "deepState"},
            {CreatorsRole, "creators"},
            {YearRole, "year"},
            {PublicationRole, "publication"},
            {LocalPathRole, "localPath"}};
}

QString AnalysisListModel::stateOf(const Row &row) const
{
    const auto rt = m_runtime.constFind(row.itemId);
    if (rt != m_runtime.constEnd())
        return rt.value();
    const AnalysisRecord rec = m_digests.value(row.paperId);
    if (!rec.valid)
        return QStringLiteral("none");
    if (rec.status == Analysis::StatusInsufficient)
        return QStringLiteral("insufficient");
    if (rec.status == Analysis::StatusFailed)
        return QStringLiteral("failed");
    return QStringLiteral("done");
}

QString AnalysisListModel::deepStateOf(const Row &row) const
{
    const int have = m_deepModuleCounts.value(row.paperId, 0);
    if (have <= 0)
        return QStringLiteral("none");
    return have >= int(Analysis::deepModules().size())
               ? QStringLiteral("done")
               : QStringLiteral("partial");
}

QVariant AnalysisListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_visible.size())
        return {};
    const Row &row = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case ItemIdRole:  return row.itemId;
    case PaperIdRole: return row.paperId;
    case TitleRole:   return row.title;
    case ToReadRole:  return row.toRead;
    case ExcludedRole:return row.excluded;
    case HasFileRole: return row.hasFile;
    case CreatorsRole:return row.creators;
    case YearRole:    return row.year;
    case PublicationRole: return row.publication;
    case LocalPathRole:   return row.localPath;
    case DeepStateRole:   return deepStateOf(row);
    case StateRole:   return stateOf(row);
    case ErrorRole:   return m_runtimeError.value(row.itemId);
    default: break;
    }

    const AnalysisRecord rec = m_digests.value(row.paperId);
    switch (role) {
    case StaleRole:
        return false;   // needs the paragraphs; only the open paper knows
    case OneLinerRole:
        return rec.payload.value(QStringLiteral("oneLiner")).toString();
    case RelevanceRole:
        return rec.payload.value(QStringLiteral("relevance"))
            .toObject()
            .value(QStringLiteral("level"))
            .toString();
    case AdviceRole:
        return rec.payload.value(QStringLiteral("advice"))
            .toObject()
            .value(QStringLiteral("code"))
            .toString();
    case AuthorEmailRole:
        return rec.authorEmail;
    case MineRole:
        return rec.mine;
    default:
        return {};
    }
}

bool AnalysisListModel::passes(const Row &row) const
{
    if (m_hideExcluded && row.excluded)
        return false;

    const QString state = stateOf(row);
    if (!m_filterState.isEmpty()) {
        if (m_filterState == QLatin1String("toRead")) {
            if (!row.toRead)
                return false;
        } else if (m_filterState == QLatin1String("excluded")) {
            if (!row.excluded)
                return false;
        } else if (state != m_filterState) {
            return false;
        }
    }
    if (m_filterRelevance.isEmpty() && m_filterAdvice.isEmpty())
        return true;

    const AnalysisRecord rec = m_digests.value(row.paperId);
    if (!rec.valid)
        return false;      // a relevance filter can only mean interpreted ones
    if (!m_filterRelevance.isEmpty()
        && rec.payload.value(QStringLiteral("relevance"))
                   .toObject()
                   .value(QStringLiteral("level"))
                   .toString()
               != m_filterRelevance)
        return false;
    if (!m_filterAdvice.isEmpty()
        && rec.payload.value(QStringLiteral("advice"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != m_filterAdvice)
        return false;
    return true;
}

void AnalysisListModel::rebuildVisible()
{
    Stall::Mark mark("filtering the paper list");
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (passes(m_all.at(i)))
            m_visible.append(i);
    }
    endResetModel();
    ++m_revision;
    emit countsChanged();
}

void AnalysisListModel::reload()
{
    Stall::Mark mark("rebuilding the paper list");
    m_all.clear();
    m_digests.clear();
    m_deepModuleCounts.clear();
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick))
        m_digests.insert(r.paperId, r);
    // Only the count of parts is kept: nine payloads per paper across a
    // hundred-paper project is a lot of JSON to hold for a row that just
    // wants to draw a star differently.
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindDeep)) {
        m_deepModuleCounts.insert(
            r.paperId,
            r.payload.value(QStringLiteral("modules")).toObject().size());
    }
    const QString project = m_projects->currentId();
    if (!project.isEmpty()) {
        const QList<SyncObjectRow> items =
            m_db->objectsByType(project, QStringLiteral("item"));
        for (const SyncObjectRow &o : items) {
            Row r;
            r.itemId = o.id;
            r.paperId = o.data.value(QStringLiteral("paperId")).toString();
            r.title = o.data.value(QStringLiteral("title")).toString();
            if (r.title.isEmpty())
                r.title = tr("(untitled)");
            QStringList names;
            for (const QJsonValue &v :
                 o.data.value(QStringLiteral("creators")).toArray())
                names << v.toString();
            r.creators = names.join(QStringLiteral(", "));
            const QJsonValue y = o.data.value(QStringLiteral("year"));
            r.year = y.isDouble() ? QString::number(y.toInt()) : y.toString();
            r.publication = o.data.value(QStringLiteral("publication")).toString();
            r.localPath = o.data.value(QStringLiteral("localPath")).toString();
            r.toRead = o.data.value(QStringLiteral("toRead")).toBool();
            r.excluded = o.data.value(QStringLiteral("excluded")).toBool();
            r.hasFile = !r.paperId.isEmpty();
            m_all.append(r);
        }
    }
    rebuildVisible();
}

int AnalysisListModel::interpretedCount() const
{
    int n = 0;
    for (const Row &r : m_all) {
        const QString s = stateOf(r);
        if (s == QLatin1String("done") || s == QLatin1String("insufficient"))
            ++n;
    }
    return n;
}

int AnalysisListModel::pendingCount() const
{
    int n = 0;
    for (const Row &r : m_all) {
        if (!r.excluded && stateOf(r) == QLatin1String("none"))
            ++n;
    }
    return n;
}

int AnalysisListModel::failedCount() const
{
    int n = 0;
    for (const Row &r : m_all) {
        if (stateOf(r) == QLatin1String("failed"))
            ++n;
    }
    return n;
}

int AnalysisListModel::excludedCount() const
{
    int n = 0;
    for (const Row &r : m_all)
        if (r.excluded)
            ++n;
    return n;
}

int AnalysisListModel::toReadCount() const
{
    int n = 0;
    for (const Row &r : m_all)
        if (r.toRead)
            ++n;
    return n;
}

int AnalysisListModel::deepDoneCount() const
{
    int n = 0;
    for (const Row &r : m_all)
        if (deepStateOf(r) == QLatin1String("done"))
            ++n;
    return n;
}

int AnalysisListModel::deepPendingCount() const
{
    int n = 0;
    for (const Row &r : m_all) {
        if (!r.toRead || r.excluded || !r.hasFile)
            continue;
        if (deepStateOf(r) != QLatin1String("done"))
            ++n;
    }
    return n;
}

QString AnalysisListModel::stateForPaper(const QString &paperId) const
{
    if (paperId.isEmpty())
        return QStringLiteral("none");
    for (const Row &r : m_all) {
        if (r.paperId == paperId)
            return stateOf(r);
    }
    const AnalysisRecord rec = m_digests.value(paperId);
    return rec.valid ? QStringLiteral("done") : QStringLiteral("none");
}

QString AnalysisListModel::relevanceForPaper(const QString &paperId) const
{
    return m_digests.value(paperId)
        .payload.value(QStringLiteral("relevance"))
        .toObject()
        .value(QStringLiteral("level"))
        .toString();
}

QStringList AnalysisListModel::visibleItemIds() const
{
    QStringList out;
    for (int i : m_visible)
        out.append(m_all.at(i).itemId);
    return out;
}

QStringList AnalysisListModel::pendingItemIds() const
{
    QStringList out;
    for (const Row &r : m_all) {
        if (r.excluded || !r.hasFile)
            continue;
        if (stateOf(r) == QLatin1String("none"))
            out.append(r.itemId);
    }
    return out;
}

QStringList AnalysisListModel::toReadItemIds() const
{
    QStringList out;
    for (const Row &r : m_all) {
        if (r.toRead && !r.excluded && r.hasFile)
            out.append(r.itemId);
    }
    return out;
}

QStringList AnalysisListModel::deepPendingAmong(const QStringList &itemIds) const
{
    const QSet<QString> wanted(itemIds.begin(), itemIds.end());
    QStringList out;
    for (const Row &r : m_all) {
        if (!wanted.contains(r.itemId) || r.excluded || !r.hasFile)
            continue;
        if (deepStateOf(r) != QLatin1String("done"))
            out.append(r.itemId);
    }
    return out;
}

QVariantList AnalysisListModel::visiblePapers() const
{
    QVariantList out;
    for (int i : m_visible) {
        const Row &r = m_all.at(i);
        if (r.paperId.isEmpty())
            continue;
        out.append(QVariantMap{{QStringLiteral("paperId"), r.paperId},
                               {QStringLiteral("title"), r.title}});
    }
    return out;
}

void AnalysisListModel::setToRead(const QString &itemId, bool on)
{
    applyToRead({itemId}, on);
}

void AnalysisListModel::setExcluded(const QString &itemId, bool on)
{
    applyExcluded({itemId}, on);
}

void AnalysisListModel::applyToRead(const QStringList &itemIds, bool on)
{
    for (const QString &id : itemIds)
        m_library->updateItem(id, QVariantMap{{QStringLiteral("toRead"), on}});
    reload();
}

void AnalysisListModel::applyExcluded(const QStringList &itemIds, bool on)
{
    for (const QString &id : itemIds)
        m_library->updateItem(id, QVariantMap{{QStringLiteral("excluded"), on}});
    reload();
}

void AnalysisListModel::setRuntime(const QString &itemId, const QString &state,
                                   const QString &error)
{
    if (state.isEmpty())
        m_runtime.remove(itemId);
    else
        m_runtime.insert(itemId, state);
    if (error.isEmpty())
        m_runtimeError.remove(itemId);
    else
        m_runtimeError.insert(itemId, error);

    for (int i = 0; i < m_visible.size(); ++i) {
        if (m_all.at(m_visible.at(i)).itemId == itemId) {
            const QModelIndex ix = index(i, 0);
            emit dataChanged(ix, ix, {StateRole, ErrorRole});
            break;
        }
    }
    emit countsChanged();
}

void AnalysisListModel::clearRuntime()
{
    m_runtime.clear();
    m_runtimeError.clear();
}

void AnalysisListModel::setFilterRelevance(const QString &v)
{
    if (v == m_filterRelevance)
        return;
    m_filterRelevance = v;
    emit filtersChanged();
    rebuildVisible();
}

void AnalysisListModel::setFilterAdvice(const QString &v)
{
    if (v == m_filterAdvice)
        return;
    m_filterAdvice = v;
    emit filtersChanged();
    rebuildVisible();
}

void AnalysisListModel::setFilterState(const QString &v)
{
    if (v == m_filterState)
        return;
    m_filterState = v;
    emit filtersChanged();
    rebuildVisible();
}

void AnalysisListModel::setHideExcluded(bool v)
{
    if (v == m_hideExcluded)
        return;
    m_hideExcluded = v;
    emit filtersChanged();
    rebuildVisible();
}
