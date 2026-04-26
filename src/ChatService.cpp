#include "ChatService.h"

#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"
#include "TocModel.h"
#include "TocService.h"

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

QString ChatService::systemPrompt() const
{
    QString out = QStringLiteral(
        "You are a careful research assistant helping the user read a "
        "specific academic paper. Be precise, cite sections by name, and "
        "answer in the user's language. If the paper text is not enough to "
        "answer, say so rather than guessing.\n");

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

    return out;
}

void ChatService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
