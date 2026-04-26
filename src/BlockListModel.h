#pragma once

#include "Block.h"

#include <QAbstractListModel>
#include <QVector>

class BlockListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        KindRole,
        KindNameRole,
        PageRole,
        OrdRole,
        BlockIdRole,
        TranslationRole,
        TranslationStatusRole,
        TranslationStatusNameRole,
        TranslationErrorRole,
    };

    explicit BlockListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBlocks(QVector<Block> blocks);
    void clear();

    int blockCount() const { return m_blocks.size(); }

    // Read-only access for in-process consumers (TranslationService).
    const Block *blockAt(int row) const;

    // Mutators used by the translation pipeline.
    void setTranslationStatus(int row, Block::TranslationStatus status,
                              const QString &error = {});
    void appendTranslationChunk(int row, const QString &chunk);
    void setTranslation(int row, const QString &text);

    Q_INVOKABLE int firstRowOnPage(int page) const;
    Q_INVOKABLE int pageOfRow(int row) const;

private:
    QVector<Block> m_blocks;
};
