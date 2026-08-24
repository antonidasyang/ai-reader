#include "PaperSource.h"

#include "BlockCache.h"
#include "BlockClusterer.h"
#include "FileSyncService.h"
#include "LibraryDb.h"
#include "LibraryModel.h"
#include "PayloadCodec.h"
#include "ProjectController.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QPdfDocument>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

PaperSource::PaperSource(LibraryDb *db, LibraryModel *library,
                         ProjectController *projects, FileSyncService *files,
                         QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_library(library)
    , m_projects(projects)
    , m_files(files)
{
    connect(&m_watcher, &QFutureWatcher<QVector<Block>>::finished, this,
            [this]() {
                const QVector<Block> blocks = m_watcher.result();
                if (m_cancelled)
                    return;
                if (blocks.isEmpty()) {
                    finishErr(tr("Nothing could be extracted from this PDF — "
                                 "it may be a scan with no text layer."));
                    return;
                }
                // Keep it: the reader opening this paper later, and any
                // re-interpretation, both get it for free.
                BlockCache cache;
                cache.setPaperId(m_paperId);
                cache.setBlocks(blocks);
                cache.flush();
                finishOk(blocks);
            });
}

PaperSource::~PaperSource()
{
    m_cancelled = true;
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
}

void PaperSource::request(const QString &itemId)
{
    m_itemId = itemId;
    m_cancelled = false;
    m_busy = true;
    step();
}

void PaperSource::cancel()
{
    m_cancelled = true;
    m_busy = false;
}

void PaperSource::step()
{
    const QVariantMap fields = m_library->itemFields(m_itemId);
    m_paperId = fields.value(QStringLiteral("paperId")).toString();
    m_title = fields.value(QStringLiteral("title")).toString();
    const QString localPath = fields.value(QStringLiteral("localPath")).toString();

    if (m_paperId.isEmpty()) {
        finishErr(tr("This library entry has no PDF attached."));
        return;
    }
    if (serveFromCache(m_paperId))
        return;
    if (serveFromProject(m_paperId))
        return;

    emit progress(m_itemId, tr("fetching the PDF"));
    const QString item = m_itemId;
    m_files->ensureLocal(
        m_itemId, localPath,
        [this, item](bool ok, const QString &path, const QString &error) {
            if (m_cancelled || item != m_itemId)
                return;
            if (!ok) {
                finishErr(error.isEmpty()
                              ? tr("The PDF could not be fetched.")
                              : error);
                return;
            }
            extractFrom(path);
        });
}

bool PaperSource::serveFromCache(const QString &paperId)
{
    BlockCache cache;
    cache.setPaperId(paperId);
    if (!cache.hasBlocks())
        return false;
    finishOk(cache.blocks());
    return true;
}

bool PaperSource::serveFromProject(const QString &paperId)
{
    const QString project = m_projects->currentId();
    if (project.isEmpty())
        return false;
    const QList<PaperDataRef> refs =
        m_db->paperData(project, paperId, QStringLiteral("blocks"));
    for (const PaperDataRef &ref : refs) {
        SyncObjectRow row;
        if (!m_db->getObject(project, ref.objectId, row) || row.deleted)
            continue;
        const QJsonObject doc = PayloadCodec::decode(row.data);
        if (doc.isEmpty())
            continue;
        BlockCache cache;
        cache.setPaperId(paperId);
        // Adopted, not ours: the marker keeps the batch from re-publishing a
        // collaborator's segmentation under this account.
        if (!cache.adopt(doc, ref.author, ref.authorEmail, ref.updatedAt))
            continue;
        if (!cache.hasBlocks())
            continue;
        finishOk(cache.blocks());
        return true;
    }
    return false;
}

void PaperSource::extractFrom(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        finishErr(tr("The PDF is not where the library says it is."));
        return;
    }
    emit progress(m_itemId, tr("reading the PDF"));
    m_watcher.setFuture(QtConcurrent::run([path]() {
        QPdfDocument doc;
        if (doc.load(path) != QPdfDocument::Error::None)
            return QVector<Block>();
        // Paced, so the reader's own window keeps getting the PDFium lock
        // while a batch grinds through the library behind it.
        return BlockClusterer::extract(doc, 4);
    }));
}

void PaperSource::finishOk(const QVector<Block> &blocks)
{
    if (m_cancelled)
        return;
    m_busy = false;
    emit ready(m_itemId, m_paperId, m_title, blocks);
}

void PaperSource::finishErr(const QString &reason)
{
    if (m_cancelled)
        return;
    m_busy = false;
    emit failed(m_itemId, reason);
}
