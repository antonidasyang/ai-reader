#include "TocService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "Settings.h"

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// Extracts the substring between the first '{' and the last '}', so we can
// tolerate ```json fences or stray prose around the JSON body.
QByteArray extractJsonObject(const QString &text)
{
    const int first = text.indexOf(QChar('{'));
    const int last  = text.lastIndexOf(QChar('}'));
    if (first < 0 || last <= first)
        return {};
    return text.mid(first, last - first + 1).toUtf8();
}

} // namespace

TocService::TocService(Settings *settings,
                       BlockListModel *blocks,
                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_blocks(blocks)
{
    if (m_blocks) {
        connect(m_blocks, &QAbstractItemModel::modelReset,
                this, &TocService::onModelReset);
    }
}

TocService::~TocService() = default;

void TocService::onModelReset()
{
    cancel();
    clear();
}

void TocService::clear()
{
    m_buffer.clear();
    m_blockIdToPage.clear();
    if (m_model.sectionCount() == 0 && m_status == Idle && m_lastError.isEmpty())
        return;
    m_model.clear();
    m_lastError.clear();
    m_status = Idle;
    emit statusChanged();
    emit sectionsChanged();
}

void TocService::cancel()
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

void TocService::generate()
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
    m_buffer.clear();
    m_blockIdToPage.clear();
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
    req.temperature = 0.0;
    req.maxTokens = 4096;
    req.stream = true;

    m_reply = m_client->send(req);

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) { m_buffer += chunk; });
    connect(m_reply, &LlmReply::finished, this, [this]() {
        const QString text = m_buffer;
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        if (m_status != Generating)
            return;
        parseResponse(text);
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        setStatus(Failed, message);
    });
}

QString TocService::systemPrompt() const
{
    return QStringLiteral(
        "You organize paper headings into a hierarchical table of contents.\n"
        "\n"
        "Input is a list of headings with block_id and page. Output JSON ONLY, "
        "in this exact shape:\n"
        "{\n"
        "  \"sections\": [\n"
        "    {\"id\": \"s1\", \"level\": 1, \"title\": \"Introduction\", "
        "\"start_block\": 12, \"children\": [\n"
        "      {\"id\": \"s1.1\", \"level\": 2, \"title\": \"Motivation\", "
        "\"start_block\": 14, \"children\": []}\n"
        "    ]}\n"
        "  ]\n"
        "}\n"
        "\n"
        "Rules:\n"
        "- Use the heading's exact `block_id` as `start_block`.\n"
        "- Title can be cleaned up (strip leading numbering like \"1.1\").\n"
        "- Infer level from heading numbering or context (1 = top-level section).\n"
        "- Skip junk such as page headers/footers, copyright notices, "
        "running titles.\n"
        "- Output ONLY the JSON. No prose, no Markdown fences, no explanation.");
}

QString TocService::userPrompt() const
{
    QJsonArray headings;
    for (int row = 0; row < m_blocks->blockCount(); ++row) {
        const Block *b = m_blocks->blockAt(row);
        if (!b) continue;
        if (b->kind != Block::Heading) continue;
        QJsonObject o;
        o[QStringLiteral("block_id")] = b->id;
        o[QStringLiteral("page")] = b->page + 1;
        o[QStringLiteral("text")] = b->text;
        headings.append(o);
    }

    // If we found very few headings, fall back to including short blocks too.
    if (headings.size() < 3) {
        for (int row = 0; row < m_blocks->blockCount(); ++row) {
            const Block *b = m_blocks->blockAt(row);
            if (!b || b->kind == Block::Heading) continue;
            if (b->text.size() <= 90) {
                QJsonObject o;
                o[QStringLiteral("block_id")] = b->id;
                o[QStringLiteral("page")] = b->page + 1;
                o[QStringLiteral("text")] = b->text;
                o[QStringLiteral("hint")] = QStringLiteral("short_block");
                headings.append(o);
            }
        }
    }

    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"headings", headings}})
            .toJson(QJsonDocument::Indented));
}

void TocService::parseResponse(const QString &text)
{
    const QByteArray jsonBytes = extractJsonObject(text);
    if (jsonBytes.isEmpty()) {
        setStatus(Failed, tr("LLM returned no JSON object."));
        return;
    }

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatus(Failed, tr("Invalid JSON: %1").arg(jerr.errorString()));
        return;
    }

    // Build block_id → page lookup now that we need it for navigation.
    if (m_blocks) {
        for (int row = 0; row < m_blocks->blockCount(); ++row) {
            const Block *b = m_blocks->blockAt(row);
            if (b) m_blockIdToPage.insert(b->id, b->page);
        }
    }

    QVector<Section> flat;
    int counter = 0;

    std::function<void(const QJsonArray &, int)> walk;
    walk = [&](const QJsonArray &arr, int depthHint) {
        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            Section s;
            s.id = obj.value(QStringLiteral("id")).toString(
                QStringLiteral("s%1").arg(++counter));
            s.level = obj.value(QStringLiteral("level")).toInt(depthHint);
            if (s.level <= 0) s.level = depthHint;
            s.title = obj.value(QStringLiteral("title")).toString().trimmed();
            s.startBlockId = obj.value(QStringLiteral("start_block")).toInt(-1);
            s.startPage = m_blockIdToPage.value(s.startBlockId, 0);
            if (!s.title.isEmpty())
                flat.append(s);
            walk(obj.value(QStringLiteral("children")).toArray(), s.level + 1);
        }
    };
    walk(doc.object().value(QStringLiteral("sections")).toArray(), 1);

    if (flat.isEmpty()) {
        setStatus(Failed, tr("LLM returned no sections."));
        return;
    }

    m_model.setSections(std::move(flat));
    emit sectionsChanged();
    setStatus(Done);
}

void TocService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit statusChanged();
}
