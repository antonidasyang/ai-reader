#include "BlockCache.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    m_paperId = paperId;
    m_blocks.clear();
    m_loaded = false;
    if (!m_paperId.isEmpty()) load();
}

void BlockCache::setBlocks(const QVector<Block> &blocks)
{
    m_blocks = blocks;
    // Always considered loaded once setBlocks is called — even an
    // empty list represents "we know what's here, it's just empty"
    // which is different from "haven't checked yet".
    m_loaded = true;
    scheduleSave();
}

void BlockCache::clear()
{
    if (m_saveTimer.isActive())
        m_saveTimer.stop();
    m_blocks.clear();
    m_loaded = false;
    const QString path = filePath();
    if (!path.isEmpty())
        QFile::remove(path);
}

void BlockCache::load()
{
    const QString path = filePath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    const QJsonArray arr = doc.object().value(QStringLiteral("blocks")).toArray();
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
        if (b.text.isEmpty()) continue;  // ignore obviously broken entries
        out.append(b);
    }
    m_blocks = out;
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

    QJsonArray arr;
    for (const Block &b : m_blocks) {
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
        arr.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("blocks")]  = arr;

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
    }
}
