#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

// On-disk JSON cache of translations, keyed by paper. The composite cache
// key is (blockId, sourceTextHash, model, promptHash, lang) so a change to
// any of those (block content, model, prompt template, or target language)
// is treated as a miss.
//
// File location: <AppDataLocation>/cache/translations/<paperId>.json
// Format:
//   { "paperId": "...", "entries": [
//       {"blockId": int, "src": "<sha256-prefix>",
//        "model": "...", "prompt": "<sha256-prefix>",
//        "lang": "...", "text": "...", "ext": true}
//   ]}
//
// `ext` marks an entry adopted from another member (or another machine of
// ours) through PaperSyncService. Adopted entries are used like any other,
// but they are not re-published under this account — otherwise every
// member would end up storing a copy of everyone else's work. Ownership is
// per entry: a local translation always wins over an adopted one with the
// same key, and re-translating an adopted entry makes it ours.
class TranslationCache : public QObject
{
    Q_OBJECT
public:
    explicit TranslationCache(QObject *parent = nullptr);

    // Switch to a new paper. Loads its cache file (if any). Pass empty
    // paperId when no paper is open — clears in-memory state.
    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    // Returns the cached translation, or an empty string on miss.
    QString lookup(int blockId, const QString &sourceText,
                   const QString &model, const QString &promptHash,
                   const QString &lang) const;

    void store(int blockId, const QString &sourceText,
               const QString &model, const QString &promptHash,
               const QString &lang, const QString &translation);

    // SHA-256 prefix utility — used for both source-text and prompt hashes.
    static QString sha(const QString &s);

    // ── sync bridge ───────────────────────────────────────────────────
    int count() const { return m_index.size(); }
    // Entries this account produced (adopted ones are left out, so they
    // are not duplicated into every member's artifact).
    QJsonArray ownEntriesJson() const;
    int ownCount() const { return m_index.size() - m_foreign.size(); }
    // Merge somebody else's entries in. A key we already hold is never
    // overwritten — local work wins per paragraph — so this only fills
    // gaps. Returns how many entries were new.
    int mergeEntries(const QJsonArray &entries);

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
    QString makeKey(int blockId, const QString &srcHash,
                    const QString &model, const QString &promptHash,
                    const QString &lang) const;

    QString m_paperId;
    QString m_cacheDir;
    QHash<QString, QString> m_index;   // composite key → translation text
    QSet<QString> m_foreign;           // keys adopted from another member
    QTimer m_saveTimer;
};
