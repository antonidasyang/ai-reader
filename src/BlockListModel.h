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
    };

    explicit BlockListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBlocks(QVector<Block> blocks);
    void clear();

    int blockCount() const { return m_blocks.size(); }

    // Lookup helpers exposed to QML for sync-scroll wiring.
    Q_INVOKABLE int firstRowOnPage(int page) const;
    Q_INVOKABLE int pageOfRow(int row) const;

private:
    QVector<Block> m_blocks;
};
