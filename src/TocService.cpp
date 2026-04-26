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
        "Input is a JSON list of headings, each with `block_id`, `page`, and "
        "`text`. Output JSON ONLY in this exact shape:\n"
        "\n"
        "{\n"
        "  \"sections\": [\n"
        "    {\"id\": \"s1\", \"level\": 1, \"title\": \"Introduction\",\n"
        "     \"start_block\": 12, \"children\": [\n"
        "       {\"id\": \"s1.1\", \"level\": 2, \"title\": \"Motivation\",\n"
        "        \"start_block\": 14, \"children\": []}\n"
        "     ]}\n"
        "  ]\n"
        "}\n"
        "\n"
        "Rules:\n"
        "- INCLUDE every plausible section heading from the input. When in "
        "doubt, include it.\n"
        "- Use the heading's exact `block_id` from the input as `start_block`.\n"
        "- Title may be cleaned (strip leading numbering like \"1\" or \"1.1\").\n"
        "- Infer level from numbering (\"1.1\" → level 2; bare title → level 1).\n"
        "- You may skip obvious page headers/footers / running titles, but "
        "DO NOT return an empty sections array if any real headings exist.\n"
        "- Use the EXACT key names: `sections`, `id`, `level`, `title`, "
        "`start_block`, `children`.\n"
        "- Output JSON ONLY. No prose. No Markdown fences. No explanation.");
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
    auto snippet = [&text]() {
        QString s = text.trimmed();
        if (s.size() > 320) s = s.left(320) + QStringLiteral("…");
        return s;
    };

    const QByteArray jsonBytes = extractJsonObject(text);
    if (jsonBytes.isEmpty()) {
        setStatus(Failed,
                  tr("LLM returned no JSON object. Raw output: %1").arg(snippet()));
        return;
    }

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &jerr);
    if (jerr.error != QJsonParseError::NoError || doc.isNull()) {
        setStatus(Failed,
                  tr("Invalid JSON: %1. Body: %2")
                      .arg(jerr.errorString(),
                           QString::fromUtf8(jsonBytes).left(320)));
        return;
    }

    // Locate the section array regardless of how the model wrapped it.
    QJsonArray topArr;
    if (doc.isArray()) {
        topArr = doc.array();
    } else {
        const QJsonObject obj = doc.object();
        for (const char *k : {"sections", "toc", "table_of_contents",
                              "contents", "items", "outline"}) {
            const QJsonValue v = obj.value(QString::fromUtf8(k));
            if (v.isArray()) { topArr = v.toArray(); break; }
        }
    }

    if (topArr.isEmpty()) {
        setStatus(Failed,
                  tr("JSON has no section array. Body: %1")
                      .arg(QString::fromUtf8(jsonBytes).left(320)));
        return;
    }

    if (m_blocks) {
        for (int row = 0; row < m_blocks->blockCount(); ++row) {
            const Block *b = m_blocks->blockAt(row);
            if (b) m_blockIdToPage.insert(b->id, b->page);
        }
    }

    auto stringField = [](const QJsonObject &o,
                          std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const QString v = o.value(QString::fromUtf8(k)).toString().trimmed();
            if (!v.isEmpty()) return v;
        }
        return QString();
    };
    auto intField = [](const QJsonObject &o,
                       std::initializer_list<const char *> keys, int def = -1) {
        for (const char *k : keys) {
            const QJsonValue v = o.value(QString::fromUtf8(k));
            if (v.isDouble()) return v.toInt(def);
            if (v.isString()) {
                bool ok = false;
                const int x = v.toString().toInt(&ok);
                if (ok) return x;
            }
        }
        return def;
    };

    QVector<Section> flat;
    int counter = 0;

    std::function<void(const QJsonArray &, int)> walk;
    walk = [&](const QJsonArray &arr, int depthHint) {
        for (const QJsonValue &v : arr) {
            // Tolerate string-only entries.
            if (v.isString()) {
                Section s;
                s.id = QStringLiteral("s%1").arg(++counter);
                s.level = depthHint;
                s.title = v.toString().trimmed();
                if (!s.title.isEmpty()) flat.append(s);
                continue;
            }
            if (!v.isObject()) continue;

            const QJsonObject obj = v.toObject();
            Section s;
            s.id = stringField(obj, {"id"});
            if (s.id.isEmpty())
                s.id = QStringLiteral("s%1").arg(++counter);
            s.level = intField(obj, {"level", "depth"}, depthHint);
            if (s.level <= 0) s.level = depthHint;
            s.title = stringField(obj, {"title", "name", "heading", "text", "label"});
            s.startBlockId = intField(obj,
                {"start_block", "block_id", "block", "startBlock", "start", "id_block"});
            s.startPage = (s.startBlockId >= 0)
                ? m_blockIdToPage.value(s.startBlockId, 0)
                : intField(obj, {"page", "start_page"}, 0);

            if (!s.title.isEmpty())
                flat.append(s);

            // Recurse into whichever child key the model chose.
            for (const char *k : {"children", "subsections", "subsection",
                                  "sections", "items", "sub"}) {
                const QJsonValue cv = obj.value(QString::fromUtf8(k));
                if (cv.isArray()) {
                    walk(cv.toArray(), s.level + 1);
                    break;
                }
            }
        }
    };
    walk(topArr, 1);

    if (flat.isEmpty()) {
        setStatus(Failed,
                  tr("Walked JSON but found no titled sections. Body: %1")
                      .arg(QString::fromUtf8(jsonBytes).left(320)));
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
