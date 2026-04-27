#include "TranslationCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

TranslationCache::TranslationCache(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/cache/translations");
    QDir().mkpath(m_cacheDir);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);  // debounce — coalesce bursts of writes
    connect(&m_saveTimer, &QTimer::timeout, this, &TranslationCache::saveNow);
}

QString TranslationCache::sha(const QString &s)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(16));
}

QString TranslationCache::filePath() const
{
    if (m_paperId.isEmpty()) return {};
    return m_cacheDir + QChar('/') + m_paperId + QStringLiteral(".json");
}

QString TranslationCache::makeKey(int blockId, const QString &srcHash,
                                  const QString &model, const QString &promptHash,
                                  const QString &lang) const
{
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5")
        .arg(blockId).arg(srcHash, model, promptHash, lang);
}

void TranslationCache::setPaperId(const QString &paperId)
{
    if (paperId == m_paperId) return;
    // Flush any pending writes against the previous paper before switching.
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
    m_paperId = paperId;
    m_index.clear();
    if (!m_paperId.isEmpty()) load();
}

void TranslationCache::load()
{
    const QString path = filePath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonArray entries = doc.object().value(QStringLiteral("entries")).toArray();
    m_index.reserve(entries.size());
    for (const QJsonValue &v : entries) {
        const QJsonObject e = v.toObject();
        const int blockId = e.value(QStringLiteral("blockId")).toInt(-1);
        if (blockId < 0) continue;
        const QString srcHash    = e.value(QStringLiteral("src")).toString();
        const QString model      = e.value(QStringLiteral("model")).toString();
        const QString promptHash = e.value(QStringLiteral("prompt")).toString();
        const QString lang       = e.value(QStringLiteral("lang")).toString();
        const QString text       = e.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) continue;
        m_index.insert(makeKey(blockId, srcHash, model, promptHash, lang), text);
    }
}

QString TranslationCache::lookup(int blockId, const QString &sourceText,
                                 const QString &model, const QString &promptHash,
                                 const QString &lang) const
{
    if (m_paperId.isEmpty()) return {};
    return m_index.value(makeKey(blockId, sha(sourceText), model, promptHash, lang));
}

void TranslationCache::store(int blockId, const QString &sourceText,
                             const QString &model, const QString &promptHash,
                             const QString &lang, const QString &translation)
{
    if (m_paperId.isEmpty() || translation.isEmpty()) return;
    const QString key = makeKey(blockId, sha(sourceText), model, promptHash, lang);
    if (m_index.value(key) == translation) return;
    m_index.insert(key, translation);
    scheduleSave();
}

void TranslationCache::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void TranslationCache::saveNow()
{
    const QString path = filePath();
    if (path.isEmpty()) return;

    QJsonArray entries;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        // Reverse-parse the composite key. Using \x1f as a separator that
        // won't appear in any of the source fields.
        const QStringList parts = it.key().split(QChar(0x1f));
        if (parts.size() != 5) continue;
        QJsonObject e;
        e[QStringLiteral("blockId")] = parts[0].toInt();
        e[QStringLiteral("src")]     = parts[1];
        e[QStringLiteral("model")]   = parts[2];
        e[QStringLiteral("prompt")]  = parts[3];
        e[QStringLiteral("lang")]    = parts[4];
        e[QStringLiteral("text")]    = it.value();
        entries.append(e);
    }

    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("entries")] = entries;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("TranslationCache: cannot open %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning("TranslationCache: commit failed for %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
}
