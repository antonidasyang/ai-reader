#include "ChatService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "PaperController.h"
#include "Settings.h"
#include "TocModel.h"
#include "TocService.h"

#include <climits>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    m_apiMessages.clear();
    m_iterations = 0;
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
        m_iterations = 0;
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

    // Append the user turn to both the visible chat and the API history,
    // then start an empty assistant bubble that all turns of this exchange
    // (text + post-tool continuations) will stream into.
    m_messages.appendMessage(QStringLiteral("user"), trimmed,
                             ChatMessage::Done);
    m_messages.appendMessage(QStringLiteral("assistant"), QString(),
                             ChatMessage::Streaming);

    LlmClient::Message apiUser;
    apiUser.role = QStringLiteral("user");
    apiUser.content = trimmed;
    m_apiMessages.append(apiUser);

    m_iterations = 0;
    runTurn();
}

void ChatService::runTurn()
{
    if (m_iterations >= kMaxIterations) {
        m_messages.appendChunkToLast(
            QStringLiteral("\n\n_[Tool budget exhausted (%1 iterations).]_")
                .arg(kMaxIterations));
        m_messages.setLastStatus(ChatMessage::Done);
        cleanupAfterFinal();
        return;
    }
    ++m_iterations;

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
    req.messages = m_apiMessages;
    req.tools = toolDefinitions();
    req.temperature = qBound(0.0, m_settings->temperature(), 1.0);
    req.maxTokens = m_settings->maxTokens();
    req.stream = true;

    m_reply = m_client->send(req);
    if (m_iterations == 1)
        emit busyChanged();

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) {
        m_messages.appendChunkToLast(chunk);
    });
    connect(m_reply, &LlmReply::finished, this,
            &ChatService::onTurnFinished);
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        m_messages.setLastStatus(ChatMessage::Failed, message);
        setLastError(message);
        m_iterations = 0;
        emit busyChanged();
    });
}

void ChatService::onTurnFinished()
{
    if (!m_reply) return;
    const QString text = m_reply->text();
    const QList<ToolCall> calls = m_reply->toolCalls();
    m_reply->deleteLater();
    m_reply.clear();

    // Mirror the assistant turn into the API history so the model has
    // continuity on the next round (Anthropic requires the original
    // tool_use blocks to be present in the assistant message).
    LlmClient::Message assistantMsg;
    assistantMsg.role = QStringLiteral("assistant");
    if (!text.isEmpty()) {
        ContentPart p;
        p.type = ContentPart::Text;
        p.text = text;
        assistantMsg.parts.append(p);
    }
    for (const ToolCall &c : calls) {
        ContentPart p;
        p.type = ContentPart::ToolUse;
        p.toolId = c.id;
        p.toolName = c.name;
        p.toolInput = c.input;
        assistantMsg.parts.append(p);
    }
    if (!assistantMsg.parts.isEmpty())
        m_apiMessages.append(assistantMsg);

    if (calls.isEmpty()) {
        m_messages.setLastStatus(ChatMessage::Done);
        cleanupAfterFinal();
        return;
    }

    // Run each tool, append all results in a single user message, and
    // continue the loop.
    LlmClient::Message resultMsg;
    resultMsg.role = QStringLiteral("user");
    for (const ToolCall &c : calls) {
        const QString resultText = runTool(c);
        // Surface the tool call to the user as a small italic chip in
        // the streaming bubble so they know what the model is doing.
        QString chip = QStringLiteral("\n\n_[tool: %1").arg(c.name);
        if (!c.input.isEmpty()) {
            chip += QStringLiteral(" ");
            chip += QString::fromUtf8(
                QJsonDocument(c.input).toJson(QJsonDocument::Compact));
        }
        chip += QStringLiteral("]_\n\n");
        m_messages.appendChunkToLast(chip);

        ContentPart p;
        p.type = ContentPart::ToolResult;
        p.toolId = c.id;
        p.text = resultText;
        resultMsg.parts.append(p);
    }
    m_apiMessages.append(resultMsg);

    runTurn();
}

void ChatService::cleanupAfterFinal()
{
    m_iterations = 0;
    emit busyChanged();
}

QVector<ToolDef> ChatService::toolDefinitions() const
{
    QVector<ToolDef> defs;

    {
        ToolDef t;
        t.name = QStringLiteral("list_sections");
        t.description = QStringLiteral(
            "Return the table of contents (TOC) of the open paper, with each "
            "section's id, level, title, and starting page. Returns an empty "
            "list when no TOC has been generated yet.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject{};
        t.inputSchema = schema;
        defs.append(t);
    }

    {
        ToolDef t;
        t.name = QStringLiteral("read_page");
        t.description = QStringLiteral(
            "Return the extracted text of a single page (1-indexed). "
            "Use this to fetch source content the user is asking about.");
        QJsonObject pageProp;
        pageProp[QStringLiteral("type")] = QStringLiteral("integer");
        pageProp[QStringLiteral("description")] =
            QStringLiteral("1-indexed page number.");
        QJsonObject props;
        props[QStringLiteral("page")] = pageProp;
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] =
            QJsonArray{ QStringLiteral("page") };
        t.inputSchema = schema;
        defs.append(t);
    }

    return defs;
}

QString ChatService::runTool(const ToolCall &call) const
{
    if (call.name == QLatin1String("list_sections"))
        return runListSections();
    if (call.name == QLatin1String("read_page")) {
        const int page = call.input.value(QStringLiteral("page")).toInt(-1);
        return runReadPage(page);
    }
    return QStringLiteral("Error: unknown tool '%1'.").arg(call.name);
}

QString ChatService::runListSections() const
{
    if (!m_toc) return QStringLiteral("Error: TOC service unavailable.");
    TocModel *tm = const_cast<TocService *>(m_toc.data())->sections();
    if (tm->sectionCount() == 0)
        return QStringLiteral(
            "TOC has not been generated yet. Use read_page to fetch content "
            "by page number, or ask the user to click 'Generate' in the TOC "
            "sidebar first.");

    QJsonArray arr;
    for (int row = 0; row < tm->sectionCount(); ++row) {
        const QModelIndex idx = tm->index(row);
        QJsonObject o;
        o[QStringLiteral("id")] = tm->data(idx, TocModel::IdRole).toString();
        o[QStringLiteral("level")] = tm->data(idx, TocModel::LevelRole).toInt();
        o[QStringLiteral("title")] = tm->data(idx, TocModel::TitleRole).toString();
        o[QStringLiteral("page")]  = tm->data(idx, TocModel::StartPageRole).toInt() + 1;
        arr.append(o);
    }
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ChatService::runReadPage(int page) const
{
    if (page < 1 || !m_paper || !m_paper->blocks())
        return QStringLiteral("Error: invalid page %1.").arg(page);
    const int pageIdx = page - 1;  // model is 0-indexed
    BlockListModel *bm = m_paper->blocks();
    if (pageIdx >= m_paper->pageCount())
        return QStringLiteral("Error: page %1 is out of range (paper has %2 pages).")
            .arg(page).arg(m_paper->pageCount());

    QString out;
    bool any = false;
    for (int row = 0; row < bm->blockCount(); ++row) {
        const Block *b = bm->blockAt(row);
        if (!b || b->page != pageIdx) continue;
        any = true;
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
    if (!any)
        return QStringLiteral("Page %1 has no extracted text. The page may "
                              "be image-only; consider the vision-read tool.")
            .arg(page);
    return out.trimmed();
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
