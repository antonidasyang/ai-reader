#include "BlockCache.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kKindMin = int(Block::Paragraph);
constexpr int kKindMax = int(Block::Equation);
} // namespace

BlockCache::BlockCache(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/cache/blocks");
    QDir().mkpath(m_cacheDir);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &BlockCache::saveNow);
}

QString BlockCache::filePath() const
{
    if (m_paperId.isEmpty()) return {};
    return m_cacheDir + QChar('/') + m_paperId + QStringLiteral(".json");
}

void BlockCache::setPaperId(const QString &paperId)
{
    if (paperId == m_paperId) return;
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
    if (!m_paperId.isEmpty())
        emit aboutToSwitch(m_paperId);
    m_paperId = paperId;
    m_blocks.clear();
    m_loaded = false;
    m_origin.clear();
    m_originLabel.clear();
    m_originRev.clear();
    m_owned = true;
    if (!m_paperId.isEmpty()) load();
}

void BlockCache::setBlocks(const QVector<Block> &blocks)
{
    m_blocks = blocks;
    // Always considered loaded once setBlocks is called — even an
    // empty list represents "we know what's here, it's just empty"
    // which is different from "haven't checked yet".
    m_loaded = true;
    // Local work: whatever we had adopted is now ours.
    m_origin.clear();
    m_originLabel.clear();
    m_originRev.clear();
    m_owned = true;
    scheduleSave();
}

void BlockCache::updateBlocks(const QVector<Block> &blocks)
{
    m_blocks = blocks;
    m_loaded = true;
    scheduleSave();
}

void BlockCache::clear()
{
    if (m_saveTimer.isActive())
        m_saveTimer.stop();
    m_blocks.clear();
    m_loaded = false;
    m_origin.clear();
    m_originLabel.clear();
    m_originRev.clear();
    m_owned = true;
    const QString path = filePath();
    if (!path.isEmpty())
        QFile::remove(path);
}

QVector<Block> BlockCache::blocksFromJson(const QJsonArray &arr)
{
    QVector<Block> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Block b;
        b.id   = o.value(QStringLiteral("id")).toInt(0);
        b.ord  = o.value(QStringLiteral("ord")).toInt(b.id);
        b.page = o.value(QStringLiteral("page")).toInt(0);
        const int kind = o.value(QStringLiteral("kind")).toInt(int(Block::Paragraph));
        b.kind = (kind >= kKindMin && kind <= kKindMax)
                 ? static_cast<Block::Kind>(kind) : Block::Paragraph;
        b.text = o.value(QStringLiteral("text")).toString();
        const QJsonArray box = o.value(QStringLiteral("bbox")).toArray();
        if (box.size() == 4) {
            b.bbox = QRectF(box.at(0).toDouble(),
                            box.at(1).toDouble(),
                            box.at(2).toDouble(),
                            box.at(3).toDouble());
        }
        b.sourceVisible      = o.value(QStringLiteral("srcVis")).toBool(true);
        b.translationVisible = o.value(QStringLiteral("transVis")).toBool(true);
        if (b.text.isEmpty()) continue;  // ignore obviously broken entries
        out.append(b);
    }
    return out;
}

QJsonArray BlockCache::blocksToJson(const QVector<Block> &blocks)
{
    QJsonArray arr;
    for (const Block &b : blocks) {
        QJsonObject o;
        o[QStringLiteral("id")]   = b.id;
        o[QStringLiteral("ord")]  = b.ord;
        o[QStringLiteral("page")] = b.page;
        o[QStringLiteral("kind")] = int(b.kind);
        o[QStringLiteral("text")] = b.text;
        QJsonArray box;
        box.append(b.bbox.x());
        box.append(b.bbox.y());
        box.append(b.bbox.width());
        box.append(b.bbox.height());
        o[QStringLiteral("bbox")] = box;
        // Only persist the visibility flags when they're not at their
        // default — keeps the JSON compact for the common case.
        if (!b.sourceVisible)
            o[QStringLiteral("srcVis")] = false;
        if (!b.translationVisible)
            o[QStringLiteral("transVis")] = false;
        arr.append(o);
    }
    return arr;
}

QJsonObject BlockCache::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("blocks")]  = blocksToJson(m_blocks);
    return root;
}

bool BlockCache::adopt(const QJsonObject &doc, const QString &author,
                       const QString &authorLabel, const QString &rev)
{
    // A block list this account produced or edited is never replaced.
    if (m_paperId.isEmpty() || (m_loaded && !m_blocks.isEmpty() && m_owned))
        return false;
    // Same donor, same revision: nothing to do.
    if (!m_blocks.isEmpty() && m_origin == author && m_originRev == rev
        && !rev.isEmpty())
        return false;
    QVector<Block> blocks =
        blocksFromJson(doc.value(QStringLiteral("blocks")).toArray());
    if (blocks.isEmpty())
        return false;
    m_blocks = blocks;
    m_loaded = true;
    m_origin = author;
    m_originLabel = authorLabel;
    m_originRev = rev;
    m_owned  = author.isEmpty();
    if (m_saveTimer.isActive())
        m_saveTimer.stop();
    saveNow();
    return true;
}

void BlockCache::load()
{
    const QString path = filePath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    m_blocks = blocksFromJson(root.value(QStringLiteral("blocks")).toArray());
    m_origin = root.value(QStringLiteral("origin")).toString();
    m_originLabel = root.value(QStringLiteral("originLabel")).toString();
    m_originRev = root.value(QStringLiteral("originRev")).toString();
    m_owned  = m_origin.isEmpty();
    m_loaded = true;
}

void BlockCache::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void BlockCache::saveNow()
{
    const QString path = filePath();
    if (path.isEmpty()) return;

    QJsonObject root = toJson();
    if (!m_origin.isEmpty()) {
        root[QStringLiteral("origin")] = m_origin;
        root[QStringLiteral("originLabel")] = m_originLabel;
        root[QStringLiteral("originRev")] = m_originRev;
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("BlockCache: cannot open %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        qWarning("BlockCache: commit failed for %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
        return;
    }
    emit contentChanged();
}
