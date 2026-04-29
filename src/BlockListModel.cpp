#include "BlockListModel.h"

namespace {

QString translationStatusName(Block::TranslationStatus s)
{
    switch (s) {
    case Block::Queued:        return QStringLiteral("queued");
    case Block::Translating:   return QStringLiteral("translating");
    case Block::Translated:    return QStringLiteral("translated");
    case Block::Failed:        return QStringLiteral("failed");
    case Block::Skipped:       return QStringLiteral("skipped");
    case Block::NotTranslated:
    default:                   return QStringLiteral("idle");
    }
}

} // namespace

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
    case TranslationRole:
        return b.translation;
    case TranslationStatusRole:
        return int(b.translationStatus);
    case TranslationStatusNameRole:
        return translationStatusName(b.translationStatus);
    case TranslationErrorRole:
        return b.translationError;
    default:
        return {};
    }
}

QHash<int, QByteArray> BlockListModel::roleNames() const
{
    return {
        {TextRole,                     "text"},
        {KindRole,                     "kind"},
        {KindNameRole,                 "kindName"},
        {PageRole,                     "page"},
        {OrdRole,                      "ord"},
        {BlockIdRole,                  "blockId"},
        {TranslationRole,              "translation"},
        {TranslationStatusRole,        "translationStatus"},
        {TranslationStatusNameRole,    "translationStatusName"},
        {TranslationErrorRole,         "translationError"},
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

const Block *BlockListModel::blockAt(int row) const
{
    if (row < 0 || row >= m_blocks.size())
        return nullptr;
    return &m_blocks.at(row);
}

void BlockListModel::setTranslationStatus(int row,
                                          Block::TranslationStatus status,
                                          const QString &error)
{
    if (row < 0 || row >= m_blocks.size())
        return;
    Block &b = m_blocks[row];
    if (b.translationStatus == status && b.translationError == error)
        return;
    b.translationStatus = status;
    b.translationError = error;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx,
                     {TranslationStatusRole,
                      TranslationStatusNameRole,
                      TranslationErrorRole});
}

void BlockListModel::appendTranslationChunk(int row, const QString &chunk)
{
    if (row < 0 || row >= m_blocks.size() || chunk.isEmpty())
        return;
    m_blocks[row].translation += chunk;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {TranslationRole});
}

void BlockListModel::setTranslation(int row, const QString &text)
{
    if (row < 0 || row >= m_blocks.size())
        return;
    if (m_blocks[row].translation == text)
        return;
    m_blocks[row].translation = text;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {TranslationRole});
}

int BlockListModel::firstRowOnPage(int page) const
{
    if (m_blocks.isEmpty() || page < 0)
        return -1;
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

int BlockListModel::nextBlockId() const
{
    int id = 0;
    for (const Block &b : m_blocks)
        id = qMax(id, b.id);
    return id + 1;
}

bool BlockListModel::splitBlock(int row, int textOffset)
{
    if (row < 0 || row >= m_blocks.size())
        return false;
    const Block &orig = m_blocks.at(row);
    if (textOffset <= 0 || textOffset >= orig.text.size())
        return false;

    const QString left  = orig.text.left(textOffset).trimmed();
    const QString right = orig.text.mid(textOffset).trimmed();
    if (left.isEmpty() || right.isEmpty())
        return false;

    // Update the original row in place.
    Block &edited = m_blocks[row];
    edited.text              = left;
    edited.translation.clear();
    edited.translationStatus = Block::NotTranslated;
    edited.translationError.clear();
    const QModelIndex idxRow = index(row);
    emit dataChanged(idxRow, idxRow,
                     {TextRole, TranslationRole,
                      TranslationStatusRole, TranslationStatusNameRole,
                      TranslationErrorRole});

    // Insert the right half as a new row immediately after.
    Block tail;
    tail.id   = nextBlockId();
    tail.ord  = tail.id;
    tail.page = orig.page;
    tail.kind = orig.kind;
    tail.text = right;
    tail.bbox = orig.bbox;  // best we can do — no per-character geometry
    beginInsertRows({}, row + 1, row + 1);
    m_blocks.insert(row + 1, tail);
    endInsertRows();

    emit blocksMutated();
    return true;
}

bool BlockListModel::mergeWithNext(int row)
{
    if (row < 0 || row + 1 >= m_blocks.size())
        return false;

    const Block next = m_blocks.at(row + 1);
    Block &keep = m_blocks[row];
    if (!keep.text.endsWith(QChar(' ')) && !next.text.startsWith(QChar(' ')))
        keep.text += QChar(' ');
    keep.text             += next.text;
    keep.bbox              = keep.bbox.united(next.bbox);
    keep.translation.clear();
    keep.translationStatus = Block::NotTranslated;
    keep.translationError.clear();

    beginRemoveRows({}, row + 1, row + 1);
    m_blocks.removeAt(row + 1);
    endRemoveRows();

    const QModelIndex idxRow = index(row);
    emit dataChanged(idxRow, idxRow,
                     {TextRole, TranslationRole,
                      TranslationStatusRole, TranslationStatusNameRole,
                      TranslationErrorRole});

    emit blocksMutated();
    return true;
}

bool BlockListModel::removeBlock(int row)
{
    if (row < 0 || row >= m_blocks.size())
        return false;
    beginRemoveRows({}, row, row);
    m_blocks.removeAt(row);
    endRemoveRows();
    emit blocksMutated();
    return true;
}
