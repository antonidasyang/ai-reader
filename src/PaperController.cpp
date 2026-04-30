#include "PaperController.h"
#include "BlockClusterer.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSize>
#include <QSizeF>

namespace {
constexpr auto kKeyLastUrl = "paper/lastUrl";

// Hash the first 4 MB of the PDF (and the file size) so we get a stable
// id even when the file is moved or renamed, but we don't pay for full
// SHA-256 over a 200 MB book. Cheap and good enough as a cache key.
QString computePaperId(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(qint64(f.size())));
    hash.addData(f.read(4 * 1024 * 1024));
    return QString::fromUtf8(hash.result().toHex());
}
} // namespace

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
            this, [this]() { m_blockCache.setBlocks(m_model.allBlocks()); });
}

QString PaperController::fileName() const
{
    if (m_source.isLocalFile())
        return QFileInfo(m_source.toLocalFile()).fileName();
    return m_source.fileName();
}

void PaperController::openPdf(const QUrl &url)
{
    if (url == m_source && m_status == Ready)
        return;
    m_source = url;
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
    m_source = {};
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
            m_paperId = computePaperId(m_source.toLocalFile());
        else
            m_paperId.clear();
        m_blockCache.setPaperId(m_paperId);
        // Use the user's saved/edited paragraph list when one exists;
        // otherwise run automatic extraction and seed the cache so
        // future opens skip clustering and any later edits are
        // preserved.
        QVector<Block> blocks;
        if (m_blockCache.hasBlocks()) {
            blocks = m_blockCache.blocks();
        } else {
            blocks = BlockClusterer::extract(m_doc);
            m_blockCache.setBlocks(blocks);
        }
        m_model.setBlocks(std::move(blocks));
        emit blocksChanged();
        setStatus(Ready);
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

void PaperController::rebuildBlocks()
{
    if (m_status != Ready) return;
    m_blockCache.clear();
    QVector<Block> blocks = BlockClusterer::extract(m_doc);
    m_blockCache.setBlocks(blocks);
    m_model.setBlocks(std::move(blocks));
    emit blocksChanged();
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
