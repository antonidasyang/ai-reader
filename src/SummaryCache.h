#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// On-disk JSON cache of paper summaries, keyed by (model, promptHash,
// lang). Same shape as TocCache: one file per paper at
// <AppDataLocation>/cache/summary/<paperId>.json with one entry per
// (model, prompt, lang) tuple.
class SummaryCache : public QObject
{
    Q_OBJECT
public:
    explicit SummaryCache(QObject *parent = nullptr);

    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    QString lookup(const QString &model,
                   const QString &promptHash,
                   const QString &lang) const;

    void store(const QString &model,
               const QString &promptHash,
               const QString &lang,
               const QString &text);

    static QString sha(const QString &s);

private:
    struct Entry {
        QString model;
        QString promptHash;
        QString lang;
        QString text;
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
