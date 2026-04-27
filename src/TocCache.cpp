#include "TocCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QJsonObject sectionToJson(const Section &s)
{
    QJsonObject o;
    o[QStringLiteral("id")]           = s.id;
    o[QStringLiteral("level")]        = s.level;
    o[QStringLiteral("title")]        = s.title;
    o[QStringLiteral("startBlockId")] = s.startBlockId;
    o[QStringLiteral("startPage")]    = s.startPage;
    return o;
}

Section sectionFromJson(const QJsonObject &o)
{
    Section s;
    s.id           = o.value(QStringLiteral("id")).toString();
    s.level        = o.value(QStringLiteral("level")).toInt(1);
    s.title        = o.value(QStringLiteral("title")).toString();
    s.startBlockId = o.value(QStringLiteral("startBlockId")).toInt(-1);
    s.startPage    = o.value(QStringLiteral("startPage")).toInt(0);
    return s;
}
} // namespace

QString TocCache::sha(const QString &s)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(16));
}

TocCache::TocCache(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/cache/toc");
    QDir().mkpath(m_cacheDir);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &TocCache::saveNow);
}

QString TocCache::filePath() const
{
    if (m_paperId.isEmpty()) return {};
    return m_cacheDir + QChar('/') + m_paperId + QStringLiteral(".json");
}

void TocCache::setPaperId(const QString &paperId)
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

void TocCache::load()
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
        const QJsonArray secs =
            e.value(QStringLiteral("sections")).toArray();
        entry.sections.reserve(secs.size());
        for (const QJsonValue &sv : secs)
            entry.sections.append(sectionFromJson(sv.toObject()));
        m_entries.append(entry);
    }
}

QVector<Section> TocCache::lookup(const QString &model,
                                  const QString &promptHash) const
{
    if (m_paperId.isEmpty()) return {};
    for (const Entry &e : m_entries) {
        if (e.model == model && e.promptHash == promptHash)
            return e.sections;
    }
    return {};
}

void TocCache::store(const QString &model,
                     const QString &promptHash,
                     const QVector<Section> &sections)
{
    if (m_paperId.isEmpty() || sections.isEmpty()) return;

    // Replace any existing entry with the same (model, promptHash), then
    // append. We don't keep multiple snapshots for the same key.
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].model == model
            && m_entries[i].promptHash == promptHash) {
            m_entries[i].sections = sections;
            scheduleSave();
            return;
        }
    }
    m_entries.append({ model, promptHash, sections });
    scheduleSave();
}

void TocCache::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void TocCache::saveNow()
{
    const QString path = filePath();
    if (path.isEmpty()) return;

    QJsonArray entries;
    for (const Entry &e : m_entries) {
        QJsonArray secs;
        for (const Section &s : e.sections)
            secs.append(sectionToJson(s));
        QJsonObject eo;
        eo[QStringLiteral("model")]    = e.model;
        eo[QStringLiteral("prompt")]   = e.promptHash;
        eo[QStringLiteral("sections")] = secs;
        entries.append(eo);
    }
    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("entries")] = entries;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("TocCache: cannot open %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning("TocCache: commit failed for %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
}
