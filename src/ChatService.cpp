#include "ChatService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"
#include "TocModel.h"
#include "TocService.h"

#include <climits>

ChatService::ChatService(Settings *settings,
                         PaperController *paper,
                         TocService *toc,
                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_toc(toc)
{
    if (m_paper) {
        connect(m_paper, &PaperController::pdfSourceChanged,
                this, &ChatService::onPaperChanged);
    }
}

ChatService::~ChatService() = default;

void ChatService::onPaperChanged()
{
    cancel();
    clear();
}

void ChatService::clear()
{
    m_messages.clear();
    setLastError({});
}

void ChatService::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
        m_messages.setLastStatus(ChatMessage::Failed, tr("Cancelled."));
        emit busyChanged();
    }
}

void ChatService::sendMessage(const QString &text)
{
    if (!m_settings) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    if (busy()) return;

    if (!m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }

    setLastError({});
    m_messages.appendMessage(QStringLiteral("user"), trimmed,
                             ChatMessage::Done);
    m_messages.appendMessage(QStringLiteral("assistant"), QString(),
                             ChatMessage::Streaming);

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
    // Send the conversation so far (skip the empty assistant placeholder
    // we just inserted).
    const QVector<ChatMessage> &all = m_messages.messages();
    for (int i = 0; i < all.size() - 1; ++i) {
        const ChatMessage &m = all[i];
        if (m.role != QLatin1String("user")
            && m.role != QLatin1String("assistant"))
            continue;
        if (m.content.isEmpty()) continue;
        req.messages.append({ m.role, m.content, {} });
    }
    req.temperature = qBound(0.0, m_settings->temperature(), 1.0);
    req.maxTokens = m_settings->maxTokens();
    req.stream = true;

    m_reply = m_client->send(req);
    emit busyChanged();

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) {
        m_messages.appendChunkToLast(chunk);
    });
    connect(m_reply, &LlmReply::finished, this, [this]() {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        m_messages.setLastStatus(ChatMessage::Done);
        emit busyChanged();
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        m_messages.setLastStatus(ChatMessage::Failed, message);
        setLastError(message);
        emit busyChanged();
    });
}

QString ChatService::defaultSystemPrompt() const
{
    return QStringLiteral(
        "You are a careful research assistant helping the user read a "
        "specific academic paper. Be precise, cite sections by name, and "
        "answer in the user's language. If the paper text is not enough to "
        "answer, say so rather than guessing.");
}

QString ChatService::systemPrompt() const
{
    QString out;
    if (m_settings && !m_settings->chatPrompt().isEmpty())
        out = m_settings->chatPrompt();
    else
        out = defaultSystemPrompt();
    out += QChar('\n');

    if (m_paper) {
        const QString title = m_paper->fileName();
        if (!title.isEmpty())
            out += QStringLiteral("\nPaper file: %1\n").arg(title);
        if (m_paper->pageCount() > 0)
            out += QStringLiteral("Pages: %1\n").arg(m_paper->pageCount());
    }

    if (m_toc && m_toc->sectionCount() > 0) {
        out += QStringLiteral("\nTable of contents (page · title):\n");
        const TocModel *tm = const_cast<TocService *>(m_toc.data())->sections();
        for (int row = 0; row < tm->sectionCount(); ++row) {
            const QModelIndex idx = tm->index(row);
            const int level = tm->data(idx, TocModel::LevelRole).toInt();
            const QString title = tm->data(idx, TocModel::TitleRole).toString();
            const int page = tm->data(idx, TocModel::StartPageRole).toInt();
            out += QString(qMax(0, level - 1) * 2, QChar(' '));
            out += QStringLiteral("- p.%1 %2\n").arg(page + 1).arg(title);
        }
    }

    if (m_settings && m_settings->chatIncludePaperText()
        && m_paper && m_paper->blocks()
        && m_paper->blocks()->blockCount() > 0) {
        // Cap inlined text at ~70% of the configured context window
        // (approx. chars/4 per token) so we don't overflow the model.
        const int ctx = m_settings->contextWindow();
        const int reserve = qMax(2000, m_settings->maxTokens() + 1000);
        const int budgetTokens = ctx > 0 ? qMax(0, int(ctx * 0.7) - reserve) : INT_MAX;
        const int budgetChars  = budgetTokens == INT_MAX
                                 ? INT_MAX
                                 : budgetTokens * 4;

        out += QStringLiteral("\nFull paper text (block-by-block):\n\n");

        BlockListModel *bm = m_paper->blocks();
        int currentPage = -1;
        int written = 0;
        bool truncated = false;
        for (int row = 0; row < bm->blockCount(); ++row) {
            const Block *b = bm->blockAt(row);
            if (!b) continue;
            QString chunk;
            if (b->page != currentPage) {
                chunk += QStringLiteral("\n[page %1]\n").arg(b->page + 1);
                currentPage = b->page;
            }
            switch (b->kind) {
            case Block::Heading:
                chunk += QStringLiteral("\n## %1\n").arg(b->text);
                break;
            case Block::Caption:
                chunk += QStringLiteral("\n_(%1)_\n").arg(b->text);
                break;
            default:
                chunk += b->text;
                chunk += QChar('\n');
                break;
            }
            if (budgetChars != INT_MAX && written + chunk.size() > budgetChars) {
                truncated = true;
                break;
            }
            out += chunk;
            written += chunk.size();
        }
        if (truncated) {
            out += QStringLiteral(
                "\n[…paper text truncated to fit context window. Ask the user "
                "to enable a model with a larger context window if needed.]\n");
        }
    }

    return out;
}

void ChatService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
