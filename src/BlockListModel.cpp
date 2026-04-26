#include "BlockListModel.h"

BlockListModel::BlockListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BlockListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_blocks.size();
}

QVariant BlockListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
        return {};

    const Block &b = m_blocks.at(index.row());
    switch (role) {
    case TextRole:
        return b.text;
    case KindRole:
        return int(b.kind);
    case KindNameRole:
        switch (b.kind) {
        case Block::Heading:  return QStringLiteral("heading");
        case Block::Caption:  return QStringLiteral("caption");
        case Block::ListItem: return QStringLiteral("list");
        case Block::Equation: return QStringLiteral("equation");
        case Block::Paragraph:
        default:              return QStringLiteral("paragraph");
        }
    case PageRole:
        return b.page;
    case OrdRole:
        return b.ord;
    case BlockIdRole:
        return b.id;
    default:
        return {};
    }
}

QHash<int, QByteArray> BlockListModel::roleNames() const
{
    return {
        {TextRole,     "text"},
        {KindRole,     "kind"},
        {KindNameRole, "kindName"},
        {PageRole,     "page"},
        {OrdRole,      "ord"},
        {BlockIdRole,  "blockId"},
    };
}

void BlockListModel::setBlocks(QVector<Block> blocks)
{
    beginResetModel();
    m_blocks = std::move(blocks);
    endResetModel();
}

void BlockListModel::clear()
{
    if (m_blocks.isEmpty())
        return;
    beginResetModel();
    m_blocks.clear();
    endResetModel();
}

int BlockListModel::firstRowOnPage(int page) const
{
    if (m_blocks.isEmpty() || page < 0)
        return -1;
    // Blocks are stored in document order with non-decreasing page numbers,
    // so a linear scan returning the first row on (or after) `page` is fine.
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks.at(i).page >= page)
            return i;
    }
    return -1;
}

int BlockListModel::pageOfRow(int row) const
{
    if (row < 0 || row >= m_blocks.size())
        return -1;
    return m_blocks.at(row).page;
}
