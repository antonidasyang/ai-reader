#include "SnippetModel.h"

SnippetModel::SnippetModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SnippetModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant SnippetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Snippet &s = m_items.at(index.row());
    switch (role) {
    case SnippetIdRole: return s.id;
    case SourceRole:    return s.source;
    case TextRole:      return s.text;
    case StatusRole:    return s.status;
    case ErrorRole:     return s.error;
    case ParagraphRole: return s.paragraph;
    case BlockRowRole:  return s.blockRow;
    default:            return {};
    }
}

QHash<int, QByteArray> SnippetModel::roleNames() const
{
    return {
        {SnippetIdRole, "snippetId"},
        {SourceRole,    "source"},
        {TextRole,      "text"},
        {StatusRole,    "status"},
        {ErrorRole,     "error"},
        {ParagraphRole, "paragraph"},
        {BlockRowRole,  "blockRow"},
    };
}

int SnippetModel::add(Snippet s)
{
    s.id = m_nextId++;
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(std::move(s));
    endInsertRows();
    return m_items.last().id;
}

void SnippetModel::remove(int id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;
    beginRemoveRows({}, idx, idx);
    m_items.remove(idx);
    endRemoveRows();
}

void SnippetModel::clear()
{
    if (m_items.isEmpty()) return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

const SnippetModel::Snippet *SnippetModel::byId(int id) const
{
    const int idx = indexOfId(id);
    return idx < 0 ? nullptr : &m_items.at(idx);
}

bool SnippetModel::hasBlockRow(int blockRow) const
{
    for (const Snippet &s : m_items) {
        if (s.blockRow == blockRow)
            return true;
    }
    return false;
}

QVector<int> SnippetModel::idsForBlockRow(int blockRow) const
{
    QVector<int> ids;
    for (const Snippet &s : m_items) {
        if (s.blockRow == blockRow)
            ids.append(s.id);
    }
    return ids;
}

void SnippetModel::setText(int id, const QString &text)
{
    const int idx = indexOfId(id);
    if (idx < 0 || m_items.at(idx).text == text) return;
    m_items[idx].text = text;
    changed(idx, {TextRole});
}

void SnippetModel::appendText(int id, const QString &chunk)
{
    const int idx = indexOfId(id);
    if (idx < 0 || chunk.isEmpty()) return;
    m_items[idx].text += chunk;
    changed(idx, {TextRole});
}

void SnippetModel::setStatus(int id, const QString &status, const QString &error)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;
    Snippet &s = m_items[idx];
    if (s.status == status && s.error == error) return;
    s.status = status;
    s.error = error;
    changed(idx, {StatusRole, ErrorRole});
}

void SnippetModel::detachBlockRows()
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).blockRow < 0) continue;
        m_items[i].blockRow = -1;
        // A card left mid-stream would otherwise spin forever, since
        // nothing will push it another chunk.
        if (m_items.at(i).status == QLatin1String("translating"))
            m_items[i].status = m_items.at(i).text.isEmpty()
                                    ? QStringLiteral("failed")
                                    : QStringLiteral("done");
        changed(i, {BlockRowRole, StatusRole});
    }
}

int SnippetModel::indexOfId(int id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

void SnippetModel::changed(int index, const QVector<int> &roles)
{
    const QModelIndex mi = this->index(index, 0);
    emit dataChanged(mi, mi, roles);
}
