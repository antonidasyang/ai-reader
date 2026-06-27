#include "LibraryModel.h"
#include "ProjectController.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStringList>
#include <QUuid>

LibraryModel::LibraryModel(LibraryDb *db, ProjectController *projects,
                           SyncEngine *sync, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
    , m_projects(projects)
    , m_sync(sync)
{
    connect(m_projects, &ProjectController::currentChanged, this,
            &LibraryModel::reload);
    connect(m_sync, &SyncEngine::projectSynced, this,
            [this](const QString &) { reload(); });
    reload();
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QString LibraryModel::creatorsDisplay(const QJsonObject &data)
{
    QStringList names;
    for (const QJsonValue &v : data.value(QStringLiteral("creators")).toArray())
        names << v.toString();
    return names.join(QStringLiteral(", "));
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.size())
        return {};
    const QJsonObject d = m_items.at(index.row()).data;
    switch (role) {
    case IdRole:
        return m_items.at(index.row()).id;
    case TitleRole: {
        const QString t = d.value(QStringLiteral("title")).toString();
        return t.isEmpty() ? tr("(untitled)") : t;
    }
    case CreatorsRole:
        return creatorsDisplay(d);
    case YearRole: {
        const QJsonValue y = d.value(QStringLiteral("year"));
        return y.isDouble() ? QString::number(y.toInt()) : y.toString();
    }
    case PublicationRole:
        return d.value(QStringLiteral("publication")).toString();
    case ItemTypeRole:
        return d.value(QStringLiteral("itemType")).toString();
    case PaperIdRole:
        return d.value(QStringLiteral("paperId")).toString();
    case LocalPathRole:
        return d.value(QStringLiteral("localPath")).toString();
    case DoiRole:
        return d.value(QStringLiteral("doi")).toString();
    case ArxivRole:
        return d.value(QStringLiteral("arxivId")).toString();
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {TitleRole, "title"},
        {CreatorsRole, "creators"},
        {YearRole, "year"},
        {PublicationRole, "publication"},
        {ItemTypeRole, "itemType"},
        {PaperIdRole, "paperId"},
        {LocalPathRole, "localPath"},
        {DoiRole, "doi"},
        {ArxivRole, "arxivId"},
    };
}

void LibraryModel::reload()
{
    beginResetModel();
    m_items = m_db->objectsByType(m_projects->currentId(),
                                  QStringLiteral("item"));
    endResetModel();
    emit countChanged();
}

QString LibraryModel::addCurrentPaper(const QString &title,
                                      const QString &paperId,
                                      const QString &localPath)
{
    if (m_projects->currentId().isEmpty())
        return {};
    const QString id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject data{
        {QStringLiteral("itemType"), QStringLiteral("journalArticle")},
        {QStringLiteral("title"), title},
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("localPath"), localPath},
        {QStringLiteral("dateAdded"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_sync->putObject(QStringLiteral("item"), id, data);
    reload();
    return id;
}

QVariantMap LibraryModel::itemFields(const QString &id) const
{
    SyncObjectRow row;
    if (!m_db->getObject(m_projects->currentId(), id, row))
        return {};
    return row.data.toVariantMap();
}

void LibraryModel::updateItem(const QString &id, const QVariantMap &fields)
{
    SyncObjectRow row;
    if (!m_db->getObject(m_projects->currentId(), id, row))
        return;
    QJsonObject data = row.data;
    const QJsonObject patch = QJsonObject::fromVariantMap(fields);
    for (auto it = patch.begin(); it != patch.end(); ++it)
        data.insert(it.key(), it.value());
    m_sync->putObject(QStringLiteral("item"), id, data);
    reload();
}

void LibraryModel::removeItem(const QString &id)
{
    SyncObjectRow row;
    if (!m_db->getObject(m_projects->currentId(), id, row))
        return;
    m_sync->putObject(QStringLiteral("item"), id, row.data, /*deleted=*/true);
    reload();
}
