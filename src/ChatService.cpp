#include "ChatService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "PaperController.h"
#include "Settings.h"
#include "TocModel.h"
#include "TocService.h"

#include <algorithm>
#include <climits>
#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

ChatSession makeSession(const QString &name)
{
    ChatSession s;
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.name = name;
    s.autoNamed = true;
    s.createdAt = QDateTime::currentDateTime();
    s.updatedAt = s.createdAt;
    return s;
}

QString deriveTitle(const QString &firstUserMessage)
{
    // Take the first non-empty line, strip blockquote / "About this
    // passage:" prefilled prefixes, and clip to ~24 chars so the tab
    // strip stays compact.
    const QStringList lines = firstUserMessage.split(QChar('\n'));
    QString line;
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith(QStringLiteral("> "))) continue;
        if (t.startsWith(QStringLiteral("About this passage"))) continue;
        line = t;
        break;
    }
    if (line.isEmpty()) line = firstUserMessage.trimmed();
    if (line.isEmpty()) return {};
    constexpr int kMax = 24;
    if (line.size() > kMax)
        line = line.left(kMax).trimmed() + QStringLiteral("…");
    return line;
}

} // namespace

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
        connect(m_paper, &PaperController::blocksChanged,
                this, &ChatService::onPaperChanged);
    }
    // Start with one empty session so QML always has something to bind
    // to even before the first paper is opened.
    ensureAtLeastOneSession();
}

ChatService::~ChatService() = default;

QString ChatService::activeSessionId() const
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) return {};
    return m_sessions.at(m_activeIndex).id;
}

QString ChatService::defaultSessionName() const
{
    return tr("New chat");
}

void ChatService::ensureAtLeastOneSession()
{
    if (!m_sessions.isEmpty() && m_activeIndex >= 0
        && m_activeIndex < m_sessions.size())
        return;
    if (m_sessions.isEmpty())
        m_sessions.append(makeSession(defaultSessionName()));
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size())
        m_activeIndex = 0;
    refreshSessionsModel();
    emit activeSessionChanged();
}

void ChatService::onPaperChanged()
{
    cancel();
    m_messages.clear();
    m_apiMessages.clear();
    m_iterations = 0;
    setLastError({});
    m_sessions.clear();
    m_activeIndex = -1;

    m_cache.setPaperId(m_paper ? m_paper->paperId() : QString());
    rehydrateFromCache();
    ensureAtLeastOneSession();
    loadSessionToActive();
    refreshSessionsModel();
    emit activeSessionChanged();
}

void ChatService::rehydrateFromCache()
{
    if (m_cache.paperId().isEmpty()) return;
    ChatHistoryCache::Snapshot snap = m_cache.load();
    if (snap.sessions.isEmpty()) return;

    // Any turn that was mid-stream when the app last quit is no longer
    // recoverable — surface it as failed rather than a frozen Streaming.
    for (ChatSession &s : snap.sessions) {
        for (ChatMessage &m : s.messages) {
            if (m.status == ChatMessage::Streaming) {
                m.status = ChatMessage::Failed;
                if (m.error.isEmpty())
                    m.error = tr("Interrupted.");
            }
        }
    }

    m_sessions = std::move(snap.sessions);
    m_activeIndex = 0;
    if (!snap.activeId.isEmpty()) {
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i).id == snap.activeId) {
                m_activeIndex = i;
                break;
            }
        }
    }
}

void ChatService::syncActiveToSession()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) return;
    ChatSession &s = m_sessions[m_activeIndex];
    s.messages = m_messages.messages();
    s.apiMessages = m_apiMessages;
}

void ChatService::loadSessionToActive()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) {
        m_messages.clear();
        m_apiMessages.clear();
        return;
    }
    const ChatSession &s = m_sessions.at(m_activeIndex);
    m_messages.setMessages(s.messages);
    m_apiMessages = s.apiMessages;
}

void ChatService::touchActiveSession()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) return;
    m_sessions[m_activeIndex].updatedAt = QDateTime::currentDateTime();
}

void ChatService::refreshSessionsModel()
{
    QVector<ChatSessionListModel::Row> rows;
    rows.reserve(m_sessions.size());
    for (int i = 0; i < m_sessions.size(); ++i) {
        const ChatSession &s = m_sessions.at(i);
        ChatSessionListModel::Row r;
        r.id = s.id;
        r.name = s.name.isEmpty() ? defaultSessionName() : s.name;
        r.updatedAt = s.updatedAt;
        r.isActive = (i == m_activeIndex);
        // Active session's count comes from the live ChatModel (it may
        // be ahead of the session record between syncs).
        r.messageCount = (i == m_activeIndex)
                             ? m_messages.messageCount()
                             : s.messages.size();
        rows.append(r);
    }
    m_sessionsModel.resetRows(std::move(rows));
}

void ChatService::persistHistory()
{
    if (m_cache.paperId().isEmpty()) return;
    syncActiveToSession();
    refreshSessionsModel();
    m_cache.save(m_sessions, activeSessionId());
}

void ChatService::clear()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) {
        m_messages.clear();
        m_apiMessages.clear();
        m_iterations = 0;
        setLastError({});
        return;
    }
    m_messages.clear();
    m_apiMessages.clear();
    m_iterations = 0;
    setLastError({});
    ChatSession &s = m_sessions[m_activeIndex];
    s.messages.clear();
    s.apiMessages.clear();
    s.autoNamed = true;
    s.name = defaultSessionName();
    s.updatedAt = QDateTime::currentDateTime();
    refreshSessionsModel();
    if (!m_cache.paperId().isEmpty())
        m_cache.save(m_sessions, activeSessionId());
}

void ChatService::cancel()
{
    const bool wasBusy = busy();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    for (const QPointer<LlmReply> &r : m_toolReplies) {
        if (r) {
            r->disconnect(this);
            r->abort();
            r->deleteLater();
        }
    }
    m_toolReplies.clear();
    m_pendingTools.clear();
    if (wasBusy) {
        m_messages.setLastStatus(ChatMessage::Failed, tr("Cancelled."));
        m_iterations = 0;
        touchActiveSession();
        persistHistory();
        emit busyChanged();
    }
}

void ChatService::sendMessage(const QString &text)
{
    if (!m_settings) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    if (busy()) return;
    if (m_activeIndex < 0)
        ensureAtLeastOneSession();

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

    touchActiveSession();
    refreshSessionsModel();

    m_iterations = 0;
    runTurn();
}

void ChatService::newSession()
{
    syncActiveToSession();
    cancel();
    ChatSession s = makeSession(defaultSessionName());
    m_sessions.append(s);
    m_activeIndex = m_sessions.size() - 1;
    loadSessionToActive();
    refreshSessionsModel();
    if (!m_cache.paperId().isEmpty())
        m_cache.save(m_sessions, activeSessionId());
    emit activeSessionChanged();
}

void ChatService::activateSession(const QString &id)
{
    if (id.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).id == id) { idx = i; break; }
    }
    if (idx < 0 || idx == m_activeIndex) return;
    syncActiveToSession();
    cancel();
    m_activeIndex = idx;
    loadSessionToActive();
    refreshSessionsModel();
    if (!m_cache.paperId().isEmpty())
        m_cache.save(m_sessions, activeSessionId());
    emit activeSessionChanged();
}

void ChatService::deleteSession(const QString &id)
{
    if (id.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).id == id) { idx = i; break; }
    }
    if (idx < 0) return;

    const bool wasActive = (idx == m_activeIndex);
    if (wasActive) cancel();

    m_sessions.remove(idx);

    if (m_sessions.isEmpty()) {
        // Always keep at least one session so the chat pane stays usable.
        m_sessions.append(makeSession(defaultSessionName()));
        m_activeIndex = 0;
        loadSessionToActive();
    } else if (wasActive) {
        m_activeIndex = qMin(idx, m_sessions.size() - 1);
        loadSessionToActive();
    } else if (idx < m_activeIndex) {
        --m_activeIndex;
    }

    refreshSessionsModel();
    if (!m_cache.paperId().isEmpty())
        m_cache.save(m_sessions, activeSessionId());
    emit activeSessionChanged();
}

void ChatService::renameSession(const QString &id, const QString &name)
{
    int idx = -1;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).id == id) { idx = i; break; }
    }
    if (idx < 0) return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_sessions.at(idx).name == trimmed) return;
    m_sessions[idx].name = trimmed;
    m_sessions[idx].autoNamed = false;
    m_sessions[idx].updatedAt = QDateTime::currentDateTime();
    refreshSessionsModel();
    if (!m_cache.paperId().isEmpty())
        m_cache.save(m_sessions, activeSessionId());
}

void ChatService::maybeAutoNameActiveSession()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_sessions.size()) return;
    ChatSession &s = m_sessions[m_activeIndex];
    if (!s.autoNamed) return;
    const auto &msgs = m_messages.messages();
    // Wait until we've seen at least the first user → assistant → user
    // exchange (or 3 messages of any shape) so the title reflects the
    // actual topic instead of a one-word opener.
    if (msgs.size() < 3) return;
    QString firstUser;
    for (const ChatMessage &m : msgs) {
        if (m.role == QStringLiteral("user") && !m.content.trimmed().isEmpty()) {
            firstUser = m.content;
            break;
        }
    }
    const QString derived = deriveTitle(firstUser);
    if (derived.isEmpty()) return;
    if (s.name == derived) return;
    s.name = derived;
}

void ChatService::runTurn()
{
    const int budget = m_settings ? m_settings->toolBudget() : 30;
    if (m_iterations >= budget) {
        m_messages.appendChunkToLast(
            QStringLiteral("\n\n_[Tool budget exhausted (%1 iterations). "
                           "Raise it in Settings if needed.]_")
                .arg(budget));
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
        touchActiveSession();
        persistHistory();
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

    // Allocate a result slot per call, surface each as an italic chip in
    // the streaming bubble, then dispatch them. Sync tools resolve
    // immediately; async tools (e.g. read_page_visual) call
    // onToolResolved later. When every slot is filled, we bundle the
    // results into one user message and continue the loop.
    m_pendingTools.clear();
    m_pendingTools.reserve(calls.size());
    for (const ToolCall &c : calls) {
        QString chip = QStringLiteral("\n\n_[tool: %1").arg(c.name);
        if (!c.input.isEmpty()) {
            chip += QStringLiteral(" ");
            chip += QString::fromUtf8(
                QJsonDocument(c.input).toJson(QJsonDocument::Compact));
        }
        chip += QStringLiteral("]_\n\n");
        m_messages.appendChunkToLast(chip);
        m_pendingTools.append({ c, QString(), false });
    }
    for (int i = 0; i < calls.size(); ++i)
        dispatchTool(i, calls[i]);
}

void ChatService::dispatchTool(int slotIndex, const ToolCall &call)
{
    if (call.name == QLatin1String("read_page_visual")) {
        const int page = call.input.value(QStringLiteral("page")).toInt(-1);
        const QString question =
            call.input.value(QStringLiteral("question")).toString();
        runReadPageVisualAsync(slotIndex, page, question);
        return;
    }
    onToolResolved(slotIndex, runTool(call));
}

void ChatService::onToolResolved(int slotIndex, const QString &result)
{
    if (slotIndex < 0 || slotIndex >= m_pendingTools.size()) return;
    if (m_pendingTools[slotIndex].resolved) return;
    m_pendingTools[slotIndex].resolved = true;
    m_pendingTools[slotIndex].result = result;
    for (const PendingTool &pt : m_pendingTools)
        if (!pt.resolved) return;

    LlmClient::Message resultMsg;
    resultMsg.role = QStringLiteral("user");
    for (const PendingTool &pt : m_pendingTools) {
        ContentPart p;
        p.type = ContentPart::ToolResult;
        p.toolId = pt.call.id;
        p.text = pt.result;
        resultMsg.parts.append(p);
    }
    m_apiMessages.append(resultMsg);
    m_pendingTools.clear();
    runTurn();
}

void ChatService::cleanupAfterFinal()
{
    m_iterations = 0;
    touchActiveSession();
    maybeAutoNameActiveSession();
    persistHistory();
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

    {
        ToolDef t;
        t.name = QStringLiteral("read_page_visual");
        t.description = QStringLiteral(
            "Send a rendered PDF page (1-indexed) to the vision-capable "
            "model and return its description. Use this when text "
            "extraction is insufficient — figures, diagrams, equations, "
            "tables, scanned pages. Optional `question` focuses the model "
            "on a specific aspect of the page.");
        QJsonObject pageProp;
        pageProp[QStringLiteral("type")] = QStringLiteral("integer");
        pageProp[QStringLiteral("description")] =
            QStringLiteral("1-indexed page number.");
        QJsonObject questionProp;
        questionProp[QStringLiteral("type")] = QStringLiteral("string");
        questionProp[QStringLiteral("description")] = QStringLiteral(
            "Optional focus question; defaults to a full description.");
        QJsonObject props;
        props[QStringLiteral("page")] = pageProp;
        props[QStringLiteral("question")] = questionProp;
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] =
            QJsonArray{ QStringLiteral("page") };
        t.inputSchema = schema;
        defs.append(t);
    }

    {
        ToolDef t;
        t.name = QStringLiteral("get_figure_caption");
        t.description = QStringLiteral(
            "Look up a figure or table caption by its label (e.g. "
            "\"Figure 3\", \"Fig. 3\", \"Table 2\"). Returns "
            "{caption, page} of the best match, or empty caption if not "
            "found. Use this to ground claims about a specific figure or "
            "table.");
        QJsonObject labelProp;
        labelProp[QStringLiteral("type")] = QStringLiteral("string");
        labelProp[QStringLiteral("description")] = QStringLiteral(
            "Caption label such as \"Figure 3\" or \"Table 2\".");
        QJsonObject props;
        props[QStringLiteral("label")] = labelProp;
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] =
            QJsonArray{ QStringLiteral("label") };
        t.inputSchema = schema;
        defs.append(t);
    }

    {
        ToolDef t;
        t.name = QStringLiteral("search_paper");
        t.description = QStringLiteral(
            "Case-insensitive substring search across the paper's blocks. "
            "Returns up to top_k matches as a JSON array of "
            "{block_id, page, snippet, score} sorted by score. Use this when "
            "the user asks where a term/phrase appears, or to locate a "
            "definition before fetching its surrounding section.");
        QJsonObject queryProp;
        queryProp[QStringLiteral("type")] = QStringLiteral("string");
        queryProp[QStringLiteral("description")] =
            QStringLiteral("The substring or term to search for.");
        QJsonObject topKProp;
        topKProp[QStringLiteral("type")] = QStringLiteral("integer");
        topKProp[QStringLiteral("description")] = QStringLiteral(
            "Maximum number of matches to return. Defaults to 10.");
        QJsonObject props;
        props[QStringLiteral("query")] = queryProp;
        props[QStringLiteral("top_k")] = topKProp;
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] =
            QJsonArray{ QStringLiteral("query") };
        t.inputSchema = schema;
        defs.append(t);
    }

    {
        ToolDef t;
        t.name = QStringLiteral("get_user_selection");
        t.description = QStringLiteral(
            "Return the text the user currently has highlighted in the PDF, "
            "along with the page it lives on. Returns an empty selection when "
            "nothing is highlighted. Use this when the user refers to "
            "'this', 'the highlighted bit', a quoted snippet, etc.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject{};
        t.inputSchema = schema;
        defs.append(t);
    }

    {
        ToolDef t;
        t.name = QStringLiteral("read_section");
        t.description = QStringLiteral(
            "Return the full text of a section identified by its TOC id "
            "(use list_sections first to discover ids). The result includes "
            "every nested subsection up to the next sibling-or-higher-level "
            "heading.");
        QJsonObject idProp;
        idProp[QStringLiteral("type")] = QStringLiteral("string");
        idProp[QStringLiteral("description")] = QStringLiteral(
            "TOC `id` field, e.g. \"s2\" or \"s3.1\".");
        QJsonObject props;
        props[QStringLiteral("section_id")] = idProp;
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] =
            QJsonArray{ QStringLiteral("section_id") };
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
    if (call.name == QLatin1String("read_section")) {
        const QString sid =
            call.input.value(QStringLiteral("section_id")).toString();
        return runReadSection(sid);
    }
    if (call.name == QLatin1String("get_user_selection"))
        return runGetUserSelection();
    if (call.name == QLatin1String("search_paper")) {
        const QString query =
            call.input.value(QStringLiteral("query")).toString();
        const int topK =
            call.input.value(QStringLiteral("top_k")).toInt(10);
        return runSearchPaper(query, topK);
    }
    if (call.name == QLatin1String("get_figure_caption")) {
        const QString label =
            call.input.value(QStringLiteral("label")).toString();
        return runGetFigureCaption(label);
    }
    return QStringLiteral("Error: unknown tool '%1'.").arg(call.name);
}

void ChatService::runReadPageVisualAsync(int slotIndex,
                                         int page,
                                         const QString &question)
{
    if (!m_settings || !m_paper) {
        onToolResolved(slotIndex,
            QStringLiteral("Error: paper or settings unavailable."));
        return;
    }
    if (!m_settings->isConfigured()) {
        onToolResolved(slotIndex,
            QStringLiteral("Error: LLM is not configured."));
        return;
    }
    if (page < 1 || page > m_paper->pageCount()) {
        onToolResolved(slotIndex,
            QStringLiteral("Error: page %1 out of range (paper has %2 pages).")
                .arg(page).arg(m_paper->pageCount()));
        return;
    }

    const int pageIdx = page - 1;
    const QImage img = m_paper->renderPage(pageIdx, 1280);
    if (img.isNull()) {
        onToolResolved(slotIndex,
            QStringLiteral("Error: failed to render page %1.").arg(page));
        return;
    }
    QByteArray png;
    {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    if (png.isEmpty()) {
        onToolResolved(slotIndex,
            QStringLiteral("Error: PNG encode failed."));
        return;
    }

    // Fresh client for the side-channel vision call so it doesn't tangle
    // with the main chat reply. Reuses the user's chat model — must be
    // vision-capable.
    LlmClient *client = m_settings->createClient(this);

    LlmClient::Request req;
    req.system = QStringLiteral(
        "You are reading one rendered page of an academic paper. Describe "
        "figures, diagrams, charts, and tables; transcribe equations as "
        "LaTeX. Answer the user's focus question if provided, otherwise "
        "give a complete page description. Output Markdown only.");
    LlmClient::Message msg;
    msg.role = QStringLiteral("user");
    msg.images.append(png);
    msg.content = question.trimmed().isEmpty()
        ? QStringLiteral("Describe the content of this page.")
        : question.trimmed();
    req.messages.append(msg);
    req.temperature = 0.0;
    req.maxTokens = m_settings->maxTokens();
    req.stream = false;

    LlmReply *reply = client->send(req);
    m_toolReplies.append(reply);

    connect(reply, &LlmReply::finished, this,
            [this, reply, client, slotIndex]() {
        const QString text = reply->text();
        m_toolReplies.removeAll(reply);
        reply->deleteLater();
        client->deleteLater();
        onToolResolved(slotIndex, text.isEmpty()
            ? QStringLiteral("(vision returned empty response)")
            : text);
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply, client, slotIndex](const QString &err) {
        m_toolReplies.removeAll(reply);
        reply->deleteLater();
        client->deleteLater();
        onToolResolved(slotIndex,
            QStringLiteral("Error from vision call: %1").arg(err));
    });
}

QString ChatService::runGetFigureCaption(const QString &label) const
{
    if (label.trimmed().isEmpty())
        return QStringLiteral("Error: label is required.");
    if (!m_paper || !m_paper->blocks())
        return QStringLiteral("Error: no paper loaded.");

    BlockListModel *bm = m_paper->blocks();
    const QString needle = label.trimmed();
    // "Figure 3" should also match "Fig. 3"; build a small set of variants.
    QStringList variants{ needle };
    if (needle.startsWith(QStringLiteral("Figure "), Qt::CaseInsensitive))
        variants << QStringLiteral("Fig. ") + needle.mid(7)
                 << QStringLiteral("Fig ")  + needle.mid(7);
    else if (needle.startsWith(QStringLiteral("Fig. "), Qt::CaseInsensitive))
        variants << QStringLiteral("Figure ") + needle.mid(5)
                 << QStringLiteral("Fig ")    + needle.mid(5);
    else if (needle.startsWith(QStringLiteral("Fig "), Qt::CaseInsensitive))
        variants << QStringLiteral("Figure ") + needle.mid(4)
                 << QStringLiteral("Fig. ")   + needle.mid(4);

    auto matches = [&](const QString &text) {
        for (const QString &v : variants) {
            if (text.startsWith(v, Qt::CaseInsensitive)) return true;
            if (text.startsWith(v + QStringLiteral(":"), Qt::CaseInsensitive)) return true;
            if (text.startsWith(v + QStringLiteral("."), Qt::CaseInsensitive)) return true;
        }
        return false;
    };

    // Pass 1: prefer Caption-kind blocks.
    for (int row = 0; row < bm->blockCount(); ++row) {
        const Block *b = bm->blockAt(row);
        if (!b || b->kind != Block::Caption) continue;
        if (matches(b->text)) {
            QJsonObject o;
            o[QStringLiteral("caption")] = b->text;
            o[QStringLiteral("page")] = b->page + 1;
            return QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact));
        }
    }

    // Pass 2: any block that begins with the label (handles missed-caption
    // classifications by the clusterer).
    for (int row = 0; row < bm->blockCount(); ++row) {
        const Block *b = bm->blockAt(row);
        if (!b) continue;
        if (matches(b->text)) {
            QJsonObject o;
            o[QStringLiteral("caption")] = b->text;
            o[QStringLiteral("page")] = b->page + 1;
            return QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact));
        }
    }

    QJsonObject o;
    o[QStringLiteral("caption")] = QString();
    o[QStringLiteral("page")] = 0;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString ChatService::runSearchPaper(const QString &query, int topK) const
{
    if (query.trimmed().isEmpty())
        return QStringLiteral("Error: query is required.");
    if (!m_paper || !m_paper->blocks())
        return QStringLiteral("Error: no paper loaded.");

    if (topK <= 0) topK = 10;
    if (topK > 50) topK = 50;

    BlockListModel *bm = m_paper->blocks();
    const QString needle = query.trimmed();

    struct Hit { int row; int score; int firstAt; };
    QVector<Hit> hits;
    hits.reserve(bm->blockCount());

    for (int row = 0; row < bm->blockCount(); ++row) {
        const Block *b = bm->blockAt(row);
        if (!b || b->text.isEmpty()) continue;
        int score = 0;
        int from = 0;
        int firstAt = -1;
        while (true) {
            const int at = b->text.indexOf(needle, from, Qt::CaseInsensitive);
            if (at < 0) break;
            if (firstAt < 0) firstAt = at;
            ++score;
            from = at + needle.size();
        }
        if (score > 0)
            hits.append({ row, score, firstAt });
    }

    if (hits.isEmpty())
        return QStringLiteral("[]");

    std::sort(hits.begin(), hits.end(),
              [](const Hit &a, const Hit &b) { return a.score > b.score; });

    constexpr int kSnippetRadius = 60;
    QJsonArray arr;
    for (int i = 0; i < hits.size() && i < topK; ++i) {
        const Hit &h = hits[i];
        const Block *b = bm->blockAt(h.row);
        if (!b) continue;
        const int start = qMax(0, h.firstAt - kSnippetRadius);
        const int end = qMin(b->text.size(),
                             h.firstAt + needle.size() + kSnippetRadius);
        QString snippet = b->text.mid(start, end - start);
        if (start > 0) snippet.prepend(QStringLiteral("..."));
        if (end < b->text.size()) snippet.append(QStringLiteral("..."));

        QJsonObject o;
        o[QStringLiteral("block_id")] = b->id;
        o[QStringLiteral("page")] = b->page + 1;
        o[QStringLiteral("snippet")] = snippet;
        o[QStringLiteral("score")] = h.score;
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ChatService::runGetUserSelection() const
{
    if (!m_paper)
        return QStringLiteral("{\"text\":\"\",\"page\":0}");
    const QString text = m_paper->currentSelection();
    const int page = m_paper->currentSelectionPage();
    QJsonObject o;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("page")] = (text.isEmpty() || page < 0) ? 0 : page + 1;
    return QString::fromUtf8(
        QJsonDocument(o).toJson(QJsonDocument::Compact));
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

QString ChatService::runReadSection(const QString &sectionId) const
{
    if (sectionId.isEmpty())
        return QStringLiteral("Error: section_id is required.");
    if (!m_toc || !m_paper || !m_paper->blocks())
        return QStringLiteral("Error: paper or TOC unavailable.");
    TocModel *tm = const_cast<TocService *>(m_toc.data())->sections();
    if (tm->sectionCount() == 0)
        return QStringLiteral(
            "Error: TOC has not been generated. Ask the user to click "
            "'Generate' in the TOC sidebar, or use read_page instead.");

    // Locate this section.
    int thisRow = -1;
    int thisStartBlock = -1;
    int thisLevel = -1;
    QString title;
    for (int row = 0; row < tm->sectionCount(); ++row) {
        const QModelIndex idx = tm->index(row);
        if (tm->data(idx, TocModel::IdRole).toString() == sectionId) {
            thisStartBlock = tm->data(idx, TocModel::StartBlockRole).toInt();
            thisLevel      = tm->data(idx, TocModel::LevelRole).toInt();
            title          = tm->data(idx, TocModel::TitleRole).toString();
            thisRow = row;
            break;
        }
    }
    if (thisRow < 0)
        return QStringLiteral("Error: section '%1' not found in the TOC.")
            .arg(sectionId);
    if (thisStartBlock < 0)
        return QStringLiteral("Error: section '%1' has no start block.")
            .arg(sectionId);

    // Find the next sibling-or-higher-level entry; its start_block bounds
    // this section. Subsections (deeper levels) are part of THIS section.
    int nextStartBlock = INT_MAX;
    for (int row = thisRow + 1; row < tm->sectionCount(); ++row) {
        const QModelIndex idx = tm->index(row);
        const int level = tm->data(idx, TocModel::LevelRole).toInt();
        if (level <= thisLevel) {
            const int sb = tm->data(idx, TocModel::StartBlockRole).toInt();
            if (sb >= 0) nextStartBlock = sb;
            break;
        }
    }

    BlockListModel *bm = m_paper->blocks();
    QString out = QStringLiteral("# %1\n").arg(title);
    int currentPage = -1;
    for (int row = 0; row < bm->blockCount(); ++row) {
        const Block *b = bm->blockAt(row);
        if (!b) continue;
        if (b->id < thisStartBlock) continue;
        if (b->id >= nextStartBlock) break;
        if (b->page != currentPage) {
            out += QStringLiteral("\n[page %1]\n").arg(b->page + 1);
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
    return out.trimmed();
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
