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
        SourceVisibleRole,
        TranslationVisibleRole,
    };

    explicit BlockListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    void setBlocks(QVector<Block> blocks);
    void clear();

    int blockCount() const { return m_blocks.size(); }

    // Read-only access for in-process consumers (TranslationService).
    const Block *blockAt(int row) const;

    // Snapshot of the current block vector. Used by BlockCache to
    // persist after every mutation. Returns a copy so callers don't
    // have to worry about lifetime if they outlive the model.
    QVector<Block> allBlocks() const { return m_blocks; }

    // Mutators used by the translation pipeline.
    void setTranslationStatus(int row, Block::TranslationStatus status,
                              const QString &error = {});
    void appendTranslationChunk(int row, const QString &chunk);
    void setTranslation(int row, const QString &text);

    Q_INVOKABLE int firstRowOnPage(int page) const;
    Q_INVOKABLE int pageOfRow(int row) const;

    // Manual paragraph editing — exposed to QML. Splits/merges/removes
    // operate on the row indices the view sees. Translations on edited
    // rows are reset to NotTranslated since the source text changes.
    // Note: if a translation is in flight on the affected row, its
    // chunks may land on the wrong row — caller should avoid editing
    // during translation.
    Q_INVOKABLE bool splitBlock(int row, int textOffset);
    Q_INVOKABLE bool mergeWithNext(int row);
    Q_INVOKABLE bool removeBlock(int row);

signals:
    void blocksMutated();
    // Lightweight per-block metadata change (currently just the
    // sourceVisible / translationVisible toggles). Distinct from
    // blocksMutated so that visibility flips don't kick the
    // TranslationService into rehydrate/cancel — only PaperController
    // should listen to this, to schedule a cache write.
    void blockMetaChanged();

private:
    int nextBlockId() const;

    QVector<Block> m_blocks;
};
