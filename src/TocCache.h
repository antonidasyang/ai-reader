#pragma once

#include "Section.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// On-disk JSON cache of a paper's table of contents, keyed by
// (model, promptHash). Same shape as TranslationCache: one file per
// paper at <AppDataLocation>/cache/toc/<paperId>.json.
class TocCache : public QObject
{
    Q_OBJECT
public:
    explicit TocCache(QObject *parent = nullptr);

    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    QVector<Section> lookup(const QString &model,
                            const QString &promptHash) const;

    void store(const QString &model,
               const QString &promptHash,
               const QVector<Section> &sections);

    // SHA-256 prefix utility — used by callers to hash the prompt template.
    static QString sha(const QString &s);

private:
    struct Entry {
        QString model;
        QString promptHash;
        QVector<Section> sections;
    };

    void load();
    void scheduleSave();
    void saveNow();
    QString filePath() const;

    QString m_paperId;
    QString m_cacheDir;
    QVector<Entry> m_entries;
    QTimer m_saveTimer;
};
