#include "VisionService.h"

#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"

#include <QBuffer>
#include <QImage>

namespace {

QByteArray encodePng(const QImage &img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

} // namespace

VisionService::VisionService(Settings *settings,
                             PaperController *paper,
                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
{
}

VisionService::~VisionService() = default;

void VisionService::clear()
{
    if (m_text.isEmpty() && m_status == Idle && m_lastError.isEmpty()
        && m_page < 0)
        return;
    m_text.clear();
    m_lastError.clear();
    m_status = Idle;
    m_page = -1;
    emit textChanged();
    emit pageChanged();
    emit statusChanged();
}

void VisionService::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    if (m_status == Rendering || m_status == Generating)
        setStatus(Idle);
}

void VisionService::readPage(int pageIdx)
{
    if (!m_settings || !m_paper) return;

    if (!m_settings->isConfigured()) {
        setStatus(Failed,
                  tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }
    if (pageIdx < 0 || pageIdx >= m_paper->pageCount()) {
        setStatus(Failed, tr("Page %1 is out of range.").arg(pageIdx + 1));
        return;
    }

    cancel();
    m_text.clear();
    emit textChanged();
    if (m_page != pageIdx) {
        m_page = pageIdx;
        emit pageChanged();
    }
    setStatus(Rendering);

    const QImage img = m_paper->renderPage(pageIdx, 1280);
    if (img.isNull()) {
        setStatus(Failed, tr("Failed to render page %1.").arg(pageIdx + 1));
        return;
    }
    const QByteArray png = encodePng(img);
    if (png.isEmpty()) {
        setStatus(Failed, tr("Failed to encode page image."));
        return;
    }

    if (!m_client)
        m_client = m_settings->createClient(this);
    else {
        m_client->setApiKey(m_settings->apiKey());
        m_client->setModel(m_settings->model());
        if (!m_settings->baseUrl().isEmpty())
            m_client->setBaseUrl(QUrl(m_settings->baseUrl()));
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    LlmClient::Message msg;
    msg.role = QStringLiteral("user");
    msg.content = userPrompt(pageIdx);
    msg.images.append(png);
    req.messages.append(msg);
    req.temperature = 0.0;
    req.maxTokens = m_settings->maxTokens();
    req.stream = true;

    setStatus(Generating);
    m_reply = m_client->send(req);

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) {
        m_text += chunk;
        emit textChanged();
    });
    connect(m_reply, &LlmReply::finished, this, [this]() {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        if (m_status == Generating)
            setStatus(Done);
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        setStatus(Failed, message);
    });
}

QString VisionService::systemPrompt() const
{
    return QStringLiteral(
        "You are reading one rendered page of an academic paper.\n"
        "\n"
        "Extract the page's content as clean Markdown:\n"
        "- Section headings as `##` / `###`.\n"
        "- Body paragraphs as plain text. Reflow line breaks; keep paragraph "
        "breaks.\n"
        "- Math: inline as `$…$`, display as `$$…$$`. Transcribe formulas "
        "verbatim where readable.\n"
        "- Figures and diagrams: describe what is shown (chart type, axes, "
        "trend) under a `**Figure N (described):**` label.\n"
        "- Tables: render as Markdown tables when feasible; otherwise "
        "describe.\n"
        "- Drop running headers, footers, and page numbers.\n"
        "\n"
        "Output ONLY the page's Markdown content. No preamble, no closing "
        "remarks.");
}

QString VisionService::userPrompt(int pageIdx) const
{
    return QStringLiteral("Extract the content of page %1.").arg(pageIdx + 1);
}

void VisionService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit statusChanged();
}
