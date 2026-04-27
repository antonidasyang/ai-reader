#pragma once

#include <QHash>
#include <QObject>
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
//        "lang": "...", "text": "..."}
//   ]}
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
    QTimer m_saveTimer;
};
