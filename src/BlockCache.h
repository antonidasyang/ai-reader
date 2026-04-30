#pragma once

#include "Block.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// On-disk JSON cache of the paragraph block list, keyed by paperId.
// Lets us preserve the result of automatic splitting plus any manual
// merge / split / delete the user did, so reopening the same PDF
// shows their edited paragraphs instead of re-running the clusterer.
//
// File location: <AppDataLocation>/cache/blocks/<paperId>.json
// Format:
//   { "paperId": "...", "blocks": [
//       {"id": int, "ord": int, "page": int, "kind": int,
//        "text": "...", "bbox": [x, y, w, h]}
//   ]}
//
// Translation text lives separately in TranslationCache and is
// rehydrated on top of these blocks via the existing block-id +
// source-text key. No translation data is stored here.
class BlockCache : public QObject
{
    Q_OBJECT
public:
    explicit BlockCache(QObject *parent = nullptr);

    // Switch to a new paper. Loads its cache file (if any). Pass an
    // empty paperId when no paper is open — clears in-memory state.
    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    // True when an existing cache file was loaded for this paper —
    // i.e., the caller can use blocks() instead of re-extracting.
    bool hasBlocks() const { return m_loaded && !m_blocks.isEmpty(); }

    // Returns the in-memory block vector (empty if hasBlocks() is
    // false).
    QVector<Block> blocks() const { return m_blocks; }

    // Replace the cached blocks and schedule a debounced write.
    void setBlocks(const QVector<Block> &blocks);

    // Drop the in-memory + on-disk cache for the current paper.
    // Called when the user asks to re-extract from scratch.
    void clear();

private:
    void load();
    void scheduleSave();
    void saveNow();
    QString filePath() const;

    QString          m_paperId;
    QString          m_cacheDir;
    QVector<Block>   m_blocks;
    bool             m_loaded = false;
    QTimer           m_saveTimer;
};
