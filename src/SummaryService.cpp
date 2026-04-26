#include "SummaryService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "Settings.h"

#include <QAbstractItemModel>

namespace {

QString resolveLanguageName(const QString &code)
{
    if (code.isEmpty() || code.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Simplified Chinese (zh-CN)");
    if (code.compare(QStringLiteral("zh-TW"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Traditional Chinese (zh-TW)");
    if (code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0
        || code.compare(QStringLiteral("en-US"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("English");
    if (code.compare(QStringLiteral("ja"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Japanese");
    return code;
}

} // namespace

SummaryService::SummaryService(Settings *settings,
                               BlockListModel *blocks,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_blocks(blocks)
{
    if (m_blocks) {
        connect(m_blocks, &QAbstractItemModel::modelReset,
                this, &SummaryService::onModelReset);
    }
}

SummaryService::~SummaryService() = default;

void SummaryService::setPaperTitle(const QString &title)
{
    if (title == m_paperTitle) return;
    m_paperTitle = title;
    emit paperTitleChanged();
}

void SummaryService::onModelReset()
{
    cancel();
    clear();
}

void SummaryService::clear()
{
    if (m_text.isEmpty() && m_status == Idle && m_lastError.isEmpty())
        return;
    m_text.clear();
    m_lastError.clear();
    m_status = Idle;
    emit textChanged();
    emit statusChanged();
}

void SummaryService::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    if (m_status == Generating)
        setStatus(Idle);
}

void SummaryService::generate()
{
    if (!m_settings || !m_blocks) return;

    if (!m_settings->isConfigured()) {
        setStatus(Failed,
                  tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }
    if (m_blocks->blockCount() == 0) {
        setStatus(Failed, tr("No paper open."));
        return;
    }

    cancel();
    m_text.clear();
    emit textChanged();
    setStatus(Generating);

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
    req.messages.append({QStringLiteral("user"), userPrompt()});
    req.temperature = qBound(0.0, m_settings->temperature(), 1.0);
    req.maxTokens = 4096;
    req.stream = true;

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

QString SummaryService::systemPrompt() const
{
    const QString lang = resolveLanguageName(m_settings ? m_settings->targetLang()
                                                       : QString());
    return QStringLiteral(
        "You are reading an academic paper. Produce a structured interpretation in %1.\n"
        "\n"
        "Output sections, in this order, using Markdown headings (`##`):\n"
        "1. **Abstract** — a ≈150-word standalone summary.\n"
        "2. **Problem** — what specific problem the paper addresses and why it matters.\n"
        "3. **Method** — the approach taken; how it works.\n"
        "4. **Contributions** — the main novel claims, as a bullet list.\n"
        "5. **Limitations** — weaknesses, scope restrictions, threats to validity, "
        "either acknowledged by the authors or evident from the work.\n"
        "6. **Open questions** — questions that remain after reading.\n"
        "\n"
        "Be precise. Cite figure/table/section numbers when relevant. "
        "Prefer plain prose and short bullet lists; do not invent details that "
        "aren't in the source text."
    ).arg(lang);
}

QString SummaryService::userPrompt() const
{
    QString out;
    if (!m_paperTitle.isEmpty())
        out += QStringLiteral("Paper file: %1\n\n").arg(m_paperTitle);
    out += QStringLiteral("Full text (block-by-block):\n\n");

    int currentPage = -1;
    for (int row = 0; row < m_blocks->blockCount(); ++row) {
        const Block *b = m_blocks->blockAt(row);
        if (!b) continue;
        if (b->page != currentPage) {
            if (currentPage >= 0) out += QChar('\n');
            out += QStringLiteral("[page %1]\n").arg(b->page + 1);
            currentPage = b->page;
        }
        switch (b->kind) {
        case Block::Heading:
            out += QStringLiteral("\n## %1\n").arg(b->text);
            break;
        case Block::Caption:
            out += QStringLiteral("\n_(%1)_\n").arg(b->text);
            break;
        default:
            out += b->text;
            out += QChar('\n');
            break;
        }
    }
    return out;
}

void SummaryService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit statusChanged();
}
