#pragma once

#include "Block.h"

#include <QJsonArray>
#include <QJsonObject>
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
//   { "paperId": "...", "origin": "<authorId|absent>", "blocks": [
//       {"id": int, "ord": int, "page": int, "kind": int,
//        "text": "...", "bbox": [x, y, w, h]}
//   ]}
//
// `origin` marks a block list adopted from another member (or another
// machine of ours) through PaperSyncService, and `originLabel` names them: the content is usable but
// not ours to re-publish. Any local segmentation or paragraph edit
// clears it, and from then on this account owns the list.
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
    int count() const { return m_blocks.size(); }

    // Replace the cached blocks and schedule a debounced write. This is
    // the local-work path: it makes this account the owner.
    void setBlocks(const QVector<Block> &blocks);

    // Same as setBlocks, but leaves the adoption marker alone: used for
    // changes that are view state (per-paragraph visibility toggles), not
    // content, so collapsing a chevron on somebody else's segmentation
    // doesn't turn it into ours to re-publish.
    void updateBlocks(const QVector<Block> &blocks);

    // Write a pending debounced save out now. A short-lived cache (the
    // batch interpreter fills one per paper and lets it go) would
    // otherwise be destroyed with the timer still pending and lose the
    // segmentation it just paid seconds of CPU for.
    void flush();

    // Drop the in-memory + on-disk cache for the current paper.
    // Called when the user asks to re-extract from scratch.
    void clear();

    // ── sync bridge ───────────────────────────────────────────────────
    // The block list as it goes over the wire (no `origin`: the receiver
    // stamps its own).
    QJsonObject toJson() const;
    // Take somebody else's block list (or our own, from another machine)
    // and write it through immediately. Refused when this account already
    // owns blocks for the paper — the local list always wins. `author` is
    // empty when the donor is this same account.
    bool adopt(const QJsonObject &doc, const QString &author,
               const QString &authorLabel, const QString &rev);
    // False once the content was adopted and nothing local has touched it;
    // the bridge publishes only what this account owns.
    bool owned() const { return m_owned; }
    QString origin() const { return m_origin; }
    // How to name the donor in the UI (their email, usually). Kept in the
    // cache file so the label survives a restart with no project loaded.
    QString originLabel() const { return m_originLabel; }
    // Version stamp of the artifact we adopted, so a re-check can tell an
    // unchanged donor from an updated one and skip the rewrite.
    QString originRev() const { return m_originRev; }

signals:
    // A write just landed on disk — the sync bridge publishes off this.
    void contentChanged();
    // The cache is about to leave `paperId` behind (paper switch). Last
    // chance to publish what it still holds.
    void aboutToSwitch(const QString &paperId);

private:
    void load();
    void scheduleSave();
    void saveNow();
    QString filePath() const;
    static QVector<Block> blocksFromJson(const QJsonArray &arr);
    static QJsonArray blocksToJson(const QVector<Block> &blocks);

    QString          m_paperId;
    QString          m_cacheDir;
    QVector<Block>   m_blocks;
    bool             m_loaded = false;
    // Empty when this account produced the list; otherwise the id of the
    // member we adopted it from.
    QString          m_origin;
    QString          m_originLabel;
    QString          m_originRev;
    bool             m_owned = true;
    QTimer           m_saveTimer;
};
