#include "AnalysisListModel.h"

#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LibraryModel.h"
#include "ProjectController.h"

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
    connect(m_store, &AnalysisStore::changed, this,
            &AnalysisListModel::reload);
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
            {StateRole, "state"},
            {StaleRole, "stale"},
            {ErrorRole, "error"},
            {OneLinerRole, "oneLiner"},
            {RelevanceRole, "relevance"},
            {AdviceRole, "advice"},
            {AuthorEmailRole, "authorEmail"},
            {MineRole, "mine"},
            {ToReadRole, "toRead"},
            {ExcludedRole, "excluded"},
            {HasFileRole, "hasFile"}};
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
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (passes(m_all.at(i)))
            m_visible.append(i);
    }
    endResetModel();
    emit countsChanged();
}

void AnalysisListModel::reload()
{
    m_all.clear();
    m_digests.clear();
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick))
        m_digests.insert(r.paperId, r);
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
