#include "PaperController.h"
#include "BlockClusterer.h"

#include <QFileInfo>

PaperController::PaperController(QObject *parent)
    : QObject(parent)
    , m_doc(this)
{
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
    m_source = {};
    m_password.clear();
    setStatus(Empty);
    emit pdfSourceChanged();
    emit pdfPasswordChanged();
    emit blocksChanged();
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
        QVector<Block> blocks = BlockClusterer::extract(m_doc);
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

void PaperController::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_errorString)
        return;
    m_status = s;
    m_errorString = err;
    emit statusChanged();
}
