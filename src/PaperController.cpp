#include "PaperController.h"
#include "BlockClusterer.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSize>
#include <QSizeF>
#include <QtConcurrent>

namespace {
constexpr auto kKeyLastUrl = "paper/lastUrl";
} // namespace

// Hash the first 4 MB of the PDF (and the file size) so we get a stable
// id even when the file is moved or renamed, but we don't pay for full
// SHA-256 over a 200 MB book. Cheap and good enough as a cache key.
QString PaperController::paperIdForFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(qint64(f.size())));
    hash.addData(f.read(4 * 1024 * 1024));
    return QString::fromUtf8(hash.result().toHex());
}

PaperController::PaperController(QObject *parent)
    : QObject(parent)
    , m_doc(this)
    , m_blockCache(this)
{
    // Manual paragraph edits (split/merge/delete) need to refresh the
    // paragraph-count badge bound to PaperController::blockCount and
    // also flush the new block list to the on-disk cache so the edit
    // survives reopening the paper.
    connect(&m_model, &BlockListModel::blocksMutated,
            this, &PaperController::blocksChanged);
    connect(&m_model, &BlockListModel::blocksMutated,
            this, [this]() {
                m_blocksEdited = true;
                m_blockCache.setBlocks(m_model.allBlocks());
            });
    // Lighter signal — just per-block visibility flips. Goes only
    // to the cache (no blocksChanged emission) so TranslationService
    // doesn't see them and rehydrate / cancel translations.
    connect(&m_model, &BlockListModel::blockMetaChanged,
            this, [this]() { m_blockCache.updateBlocks(m_model.allBlocks()); });
    connect(&m_extractWatcher, &QFutureWatcher<QVector<Block>>::finished,
            this, &PaperController::onExtractionFinished);
}

QString PaperController::fileName() const
{
    if (m_source.isLocalFile())
        return QFileInfo(m_source.toLocalFile()).fileName();
    return m_source.fileName();
}

void PaperController::openPdf(const QUrl &url)
{
    // Accept a bare filesystem path as well as a real URL. QUrl parses
    // "/a/b.pdf" (and Windows' "C:/a/b.pdf", whose drive letter becomes
    // a scheme) as non-local, which silently degrades everything
    // downstream: QML's PdfDocument refuses to load it so the viewer
    // goes blank, and both the GROBID upgrade and the async extractor
    // take their remote branches. Normalize once, here, rather than
    // trusting every caller to build the URL correctly.
    QUrl src = url;
    if (!src.isLocalFile() && src.scheme() != QLatin1String("http")
        && src.scheme() != QLatin1String("https")) {
        const QString asPath = url.toString();
        if (!asPath.isEmpty() && QFileInfo::exists(asPath))
            src = QUrl::fromLocalFile(asPath);
    }

    if (src == m_source && m_status == Ready)
        return;
    m_source = src;
    m_password.clear();
    // Persist immediately so a hard crash mid-load still restores the
    // user's last paper next launch. Remote URLs are skipped — restoring
    // them would block on the network at startup.
    if (m_source.isLocalFile()) {
        m_qs.setValue(kKeyLastUrl, m_source);
        m_qs.sync();
    } else {
        m_qs.remove(kKeyLastUrl);
        m_qs.sync();
    }
    emit pdfSourceChanged();
    emit pdfPasswordChanged();
    m_model.clear();
    emit blocksChanged();
    reload();
}

void PaperController::setPassword(const QString &password)
{
    if (password == m_password)
        return;
    m_password = password;
    emit pdfPasswordChanged();
    reload();
}

void PaperController::clear()
{
    m_doc.close();
    m_model.clear();
    m_blockCache.setPaperId({});
    m_source = QUrl();
    m_password.clear();
    m_paperId.clear();
    m_qs.remove(kKeyLastUrl);
    m_qs.sync();
    setStatus(Empty);
    emit pdfSourceChanged();
    emit pdfPasswordChanged();
    emit blocksChanged();
    if (!m_currentSelection.isEmpty() || m_currentSelectionPage != -1) {
        m_currentSelection.clear();
        m_currentSelectionPage = -1;
        emit currentSelectionChanged();
    }
}

void PaperController::restoreLast()
{
    const QUrl saved = m_qs.value(kKeyLastUrl).toUrl();
    if (saved.isEmpty())
        return;
    // Drop the entry quietly if the file has been moved/deleted since
    // the last session — better than booting straight into an Error.
    if (saved.isLocalFile() && !QFileInfo::exists(saved.toLocalFile())) {
        m_qs.remove(kKeyLastUrl);
        m_qs.sync();
        return;
    }
    openPdf(saved);
}

void PaperController::setCurrentSelection(const QString &text, int page)
{
    if (text == m_currentSelection && page == m_currentSelectionPage) return;
    m_currentSelection = text;
    m_currentSelectionPage = page;
    emit currentSelectionChanged();
}

void PaperController::reload()
{
    if (m_source.isEmpty()) {
        setStatus(Empty);
        return;
    }
    setStatus(Loading);

    m_doc.setPassword(m_password);

    QPdfDocument::Error err;
    if (m_source.isLocalFile())
        err = m_doc.load(m_source.toLocalFile());
    else
        err = m_doc.load(m_source.toString());

    switch (err) {
    case QPdfDocument::Error::None: {
        if (m_source.isLocalFile())
            m_paperId = paperIdForFile(m_source.toLocalFile());
        else
            m_paperId.clear();
        m_forceExtract = false;   // a force never outlives its paper
        m_blocksEdited = false;
        m_blockCache.setPaperId(m_paperId);
        // Before anything decides to segment: let PaperSyncService fill the
        // cache from a segmentation the project already holds for this exact
        // file — ours from another machine, or a collaborator's. It reads the
        // local sync mirror, so this stays instant and works offline.
        if (!m_paperId.isEmpty())
            emit paperCacheReady(m_paperId);
        // Use the user's saved/edited paragraph list when one exists;
        // otherwise run automatic extraction and seed the cache so
        // future opens skip clustering and any later edits are
        // preserved.
        if (m_blockCache.hasBlocks()) {
            m_model.setBlocks(m_blockCache.blocks());
            emit blocksChanged();
            setStatus(Ready);
        } else {
            // Show the paper immediately; paragraphs stream in when
            // the extraction worker finishes (autoExtracted → GROBID
            // then chains off that result as before).
            m_model.clear();
            emit blocksChanged();
            setStatus(Ready);
            // Off by default: clustering a long PDF (and the GROBID
            // round trip behind it) costs seconds that a reader who
            // only wants to page through the document never asked
            // for. The Segment button runs the exact same path.
            if (m_autoSegment)
                startAsyncExtraction();
        }
        break;
    }
    case QPdfDocument::Error::IncorrectPassword:
        emit passwordRequired();
        // Stay in Loading until a password is supplied or user cancels.
        break;
    case QPdfDocument::Error::FileNotFound:
        setStatus(Error, tr("File not found."));
        break;
    case QPdfDocument::Error::InvalidFileFormat:
        setStatus(Error, tr("Invalid PDF format."));
        break;
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        setStatus(Error, tr("Unsupported PDF security scheme."));
        break;
    case QPdfDocument::Error::DataNotYetAvailable:
        setStatus(Error, tr("PDF data not yet available."));
        break;
    case QPdfDocument::Error::Unknown:
    default:
        setStatus(Error, tr("Failed to load PDF."));
        break;
    }
}

bool PaperController::isCurrentFile(const QString &path) const
{
    if (path.isEmpty() || !m_source.isLocalFile())
        return false;
    const QFileInfo want(path);
    const QFileInfo open(m_source.toLocalFile());
    // canonicalFilePath is empty for a file that isn't there any more; fall
    // back to the absolute path so a moved-away row still compares sanely.
    const QString a = want.canonicalFilePath();
    const QString b = open.canonicalFilePath();
    if (!a.isEmpty() && !b.isEmpty())
        return a == b;
    return want.absoluteFilePath() == open.absoluteFilePath();
}

bool PaperController::applyCachedBlocks()
{
    if (m_status != Ready || m_paperId.isEmpty())
        return false;
    if (!m_blockCache.hasBlocks())
        return false;
    // Same guards as a GROBID swap: never replace paragraphs the user is
    // already looking at or has edited, and never race a running extraction.
    if (m_blocksEdited || m_model.blockCount() > 0)
        return false;
    if (m_extractWatcher.isRunning())
        return false;
    m_model.setBlocks(m_blockCache.blocks());
    emit blocksChanged();
    return true;
}

void PaperController::rebuildBlocks()
{
    if (m_status != Ready) return;
    if (m_extractWatcher.isRunning()) return;   // one rebuild at a time
    m_blockCache.clear();
    m_blocksEdited = false;
    m_forceExtract = true;
    m_model.clear();
    emit blocksChanged();
    startAsyncExtraction();
}

void PaperController::startAsyncExtraction()
{
    if (!m_source.isLocalFile()) {
        // Remote sources are already fully loaded into m_doc; the
        // (rare) synchronous path is acceptable there.
        QVector<Block> blocks = BlockClusterer::extract(m_doc);
        m_blockCache.setBlocks(blocks);
        m_model.setBlocks(std::move(blocks));
        emit blocksChanged();
        emit autoExtracted();
        return;
    }
    const QString path = m_source.toLocalFile();
    const QString password = m_password;
    m_extractPaperId = m_paperId;
    if (!m_extracting) {
        m_extracting = true;
        emit extractingChanged();
    }
    m_extractWatcher.setFuture(QtConcurrent::run([path, password]() {
        QPdfDocument doc;   // worker-owned: no shared state with the UI
        doc.setPassword(password);
        if (doc.load(path) != QPdfDocument::Error::None)
            return QVector<Block>();
        // Paced so the PDFium global lock stays available to the
        // first page renders of the freshly-opened document.
        return BlockClusterer::extract(doc, 20);
    }));
}

void PaperController::onExtractionFinished()
{
    if (m_extracting) {
        m_extracting = false;
        emit extractingChanged();
    }
    // Stale results (paper switched mid-extraction) and anything the
    // user already touched are dropped, same rules as GROBID swaps.
    // An explicit re-segment (m_forceExtract) skips the second guard:
    // the model may have been repopulated meanwhile (e.g. by a stale
    // GROBID reply from the previous cycle) and the user asked for a
    // fresh result regardless.
    if (m_status != Ready || m_extractPaperId != m_paperId)
        return;
    if (!m_forceExtract && (m_blocksEdited || m_model.blockCount() > 0))
        return;
    QVector<Block> blocks = m_extractWatcher.result();
    if (blocks.isEmpty())
        return;
    m_blockCache.setBlocks(blocks);
    m_model.setBlocks(std::move(blocks));
    emit blocksChanged();
    emit autoExtracted();
}

bool PaperController::applyStructuredBlocks(const QString &paperId,
                                            QVector<Block> blocks)
{
    if (m_status != Ready || paperId.isEmpty() || paperId != m_paperId
        || m_blocksEdited)
        return false;
    // A rebuild is racing us: this reply was made from the OLD
    // segmentation and must not resurrect it — the fresh extraction
    // will trigger its own GROBID request when it lands.
    if (m_extractWatcher.isRunning())
        return false;
    // Any translation state (done, queued, even skipped) means the
    // user already invested in the current segmentation — keep it.
    // Exception: the user explicitly re-segmented; cached
    // translations were made for the old paragraphs and re-stamping
    // them must not block the structural upgrade they asked for.
    const bool forced = m_forceExtract;
    m_forceExtract = false;
    if (!forced) {
        const QVector<Block> current = m_model.allBlocks();
        for (const Block &b : current)
            if (b.translationStatus != Block::NotTranslated)
                return false;
    }
    m_blockCache.setBlocks(blocks);
    m_model.setBlocks(std::move(blocks));
    emit blocksChanged();
    return true;
}

bool PaperController::exportExtractedText(const QUrl &dest)
{
    if (m_status != Ready)
        return false;
    const QString path = dest.isLocalFile() ? dest.toLocalFile() : dest.toString();
    if (path.isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray bytes = BlockClusterer::dumpDebug(m_doc).toUtf8();
    return f.write(bytes) == bytes.size();
}

QString PaperController::headText(int maxChars) const
{
    // First blocks' text, for identifier (DOI/arXiv) extraction.
    QString out;
    const QVector<Block> blocks = m_model.allBlocks();
    for (const Block &b : blocks) {
        out += b.text;
        out += QChar('\n');
        if (out.size() >= maxChars)
            break;
    }
    return out.left(maxChars);
}

QImage PaperController::renderPage(int page, int targetWidthPx) const
{
    if (m_status != Ready) return {};
    if (page < 0 || page >= m_doc.pageCount()) return {};

    const QSizeF pt = m_doc.pagePointSize(page);
    if (pt.width() <= 0 || pt.height() <= 0) return {};

    const qreal scale = targetWidthPx / pt.width();
    const QSize px(qRound(pt.width()  * scale),
                   qRound(pt.height() * scale));
    return const_cast<QPdfDocument &>(m_doc).render(page, px);
}

void PaperController::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_errorString)
        return;
    m_status = s;
    m_errorString = err;
    emit statusChanged();
}
