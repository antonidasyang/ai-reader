#include "SummaryCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

QString SummaryCache::sha(const QString &s)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(16));
}

SummaryCache::SummaryCache(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/cache/summary");
    QDir().mkpath(m_cacheDir);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &SummaryCache::saveNow);
}

QString SummaryCache::filePath() const
{
    if (m_paperId.isEmpty()) return {};
    return m_cacheDir + QChar('/') + m_paperId + QStringLiteral(".json");
}

void SummaryCache::setPaperId(const QString &paperId)
{
    if (paperId == m_paperId) return;
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
    m_paperId = paperId;
    m_entries.clear();
    if (!m_paperId.isEmpty()) load();
}

void SummaryCache::load()
{
    const QString path = filePath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonArray entries = doc.object().value(QStringLiteral("entries")).toArray();
    m_entries.reserve(entries.size());
    for (const QJsonValue &v : entries) {
        const QJsonObject e = v.toObject();
        Entry entry;
        entry.model      = e.value(QStringLiteral("model")).toString();
        entry.promptHash = e.value(QStringLiteral("prompt")).toString();
        entry.lang       = e.value(QStringLiteral("lang")).toString();
        entry.text       = e.value(QStringLiteral("text")).toString();
        if (entry.text.isEmpty()) continue;
        m_entries.append(entry);
    }
}

QString SummaryCache::lookup(const QString &model,
                             const QString &promptHash,
                             const QString &lang) const
{
    if (m_paperId.isEmpty()) return {};
    for (const Entry &e : m_entries) {
        if (e.model == model && e.promptHash == promptHash && e.lang == lang)
            return e.text;
    }
    return {};
}

void SummaryCache::store(const QString &model,
                         const QString &promptHash,
                         const QString &lang,
                         const QString &text)
{
    if (m_paperId.isEmpty() || text.isEmpty()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].model == model
            && m_entries[i].promptHash == promptHash
            && m_entries[i].lang == lang) {
            if (m_entries[i].text == text) return;
            m_entries[i].text = text;
            scheduleSave();
            return;
        }
    }
    m_entries.append({ model, promptHash, lang, text });
    scheduleSave();
}

void SummaryCache::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void SummaryCache::saveNow()
{
    const QString path = filePath();
    if (path.isEmpty()) return;
    QJsonArray entries;
    for (const Entry &e : m_entries) {
        QJsonObject eo;
        eo[QStringLiteral("model")]  = e.model;
        eo[QStringLiteral("prompt")] = e.promptHash;
        eo[QStringLiteral("lang")]   = e.lang;
        eo[QStringLiteral("text")]   = e.text;
        entries.append(eo);
    }
    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("entries")] = entries;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}
