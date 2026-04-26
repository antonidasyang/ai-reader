#include "AnthropicClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>

namespace {

QUrl messagesEndpoint(QUrl base)
{
    QString path = base.path();
    if (path.endsWith(QChar('/')))
        path.chop(1);
    base.setPath(path + QStringLiteral("/v1/messages"));
    return base;
}

// Streaming state shared across SSE events: Anthropic emits tool_use blocks
// over content_block_start (id/name) → content_block_delta with
// input_json_delta (partial input JSON, char-by-char) → content_block_stop
// (parse the accumulated buffer and emit the tool call).
struct ToolCallBuilder {
    QString id;
    QString name;
    QString jsonBuffer;
};

struct StreamState {
    QHash<int, ToolCallBuilder> active;   // by content block index
};

void parseSseEvent(const QByteArray &payload,
                   LlmReply *reply,
                   StreamState &state)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("content_block_start")) {
        const int idx = obj.value(QStringLiteral("index")).toInt();
        const QJsonObject block =
            obj.value(QStringLiteral("content_block")).toObject();
        if (block.value(QStringLiteral("type")).toString()
            == QLatin1String("tool_use")) {
            ToolCallBuilder b;
            b.id   = block.value(QStringLiteral("id")).toString();
            b.name = block.value(QStringLiteral("name")).toString();
            state.active.insert(idx, b);
        }
    } else if (type == QLatin1String("content_block_delta")) {
        const int idx = obj.value(QStringLiteral("index")).toInt();
        const QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QLatin1String("text_delta")) {
            reply->appendChunk(delta.value(QStringLiteral("text")).toString());
        } else if (deltaType == QLatin1String("input_json_delta")) {
            auto it = state.active.find(idx);
            if (it != state.active.end())
                it->jsonBuffer += delta.value(
                    QStringLiteral("partial_json")).toString();
        }
    } else if (type == QLatin1String("content_block_stop")) {
        const int idx = obj.value(QStringLiteral("index")).toInt();
        auto it = state.active.find(idx);
        if (it != state.active.end()) {
            ToolCall call;
            call.id = it->id;
            call.name = it->name;
            QJsonParseError jerr{};
            const QByteArray buf = it->jsonBuffer.isEmpty()
                                   ? QByteArrayLiteral("{}")
                                   : it->jsonBuffer.toUtf8();
            const QJsonDocument inputDoc =
                QJsonDocument::fromJson(buf, &jerr);
            if (jerr.error == QJsonParseError::NoError && inputDoc.isObject())
                call.input = inputDoc.object();
            reply->appendToolCall(call);
            state.active.erase(it);
        }
    } else if (type == QLatin1String("message_stop")) {
        reply->markFinished();
    } else if (type == QLatin1String("error")) {
        const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
        reply->setError(err.value(QStringLiteral("message")).toString(
            QStringLiteral("Anthropic error")));
    }
}

QJsonValue serializeContent(const LlmClient::Message &m)
{
    // Plain-text shortcut: when there are no parts and no images, send the
    // bare string body. Anthropic accepts both `string` and `array` shapes.
    if (m.parts.isEmpty() && m.images.isEmpty())
        return QJsonValue(m.content);

    QJsonArray content;

    if (!m.parts.isEmpty()) {
        for (const ContentPart &p : m.parts) {
            QJsonObject block;
            switch (p.type) {
            case ContentPart::Text:
                block[QStringLiteral("type")] = QStringLiteral("text");
                block[QStringLiteral("text")] = p.text;
                break;
            case ContentPart::Image: {
                QJsonObject src;
                src[QStringLiteral("type")] = QStringLiteral("base64");
                src[QStringLiteral("media_type")] = QStringLiteral("image/png");
                src[QStringLiteral("data")] =
                    QString::fromUtf8(p.imagePng.toBase64());
                block[QStringLiteral("type")] = QStringLiteral("image");
                block[QStringLiteral("source")] = src;
                break;
            }
            case ContentPart::ToolUse:
                block[QStringLiteral("type")] = QStringLiteral("tool_use");
                block[QStringLiteral("id")]   = p.toolId;
                block[QStringLiteral("name")] = p.toolName;
                block[QStringLiteral("input")] = p.toolInput;
                break;
            case ContentPart::ToolResult:
                block[QStringLiteral("type")]        = QStringLiteral("tool_result");
                block[QStringLiteral("tool_use_id")] = p.toolId;
                block[QStringLiteral("content")]     = p.text;
                break;
            }
            content.append(block);
        }
        return content;
    }

    // Legacy multimodal path: images first, then a text block.
    for (const QByteArray &png : m.images) {
        QJsonObject src;
        src[QStringLiteral("type")] = QStringLiteral("base64");
        src[QStringLiteral("media_type")] = QStringLiteral("image/png");
        src[QStringLiteral("data")] =
            QString::fromUtf8(png.toBase64());
        QJsonObject block;
        block[QStringLiteral("type")] = QStringLiteral("image");
        block[QStringLiteral("source")] = src;
        content.append(block);
    }
    if (!m.content.isEmpty()) {
        QJsonObject text;
        text[QStringLiteral("type")] = QStringLiteral("text");
        text[QStringLiteral("text")] = m.content;
        content.append(text);
    }
    return content;
}

} // namespace

AnthropicClient::AnthropicClient(QObject *parent)
    : LlmClient(parent)
{
    m_baseUrl = QUrl(QStringLiteral("https://api.anthropic.com"));
}

LlmReply *AnthropicClient::send(const Request &req)
{
    auto *reply = new LlmReply(this);

    if (m_apiKey.isEmpty()) {
        reply->setError(tr("No API key configured."));
        return reply;
    }
    if (m_model.isEmpty()) {
        reply->setError(tr("No model configured."));
        return reply;
    }

    QNetworkRequest request(messagesEndpoint(m_baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (req.stream)
        request.setRawHeader("accept", "text/event-stream");

    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("max_tokens")] = req.maxTokens;
    body[QStringLiteral("temperature")] = req.temperature;
    body[QStringLiteral("stream")] = req.stream;
    if (!req.system.isEmpty())
        body[QStringLiteral("system")] = req.system;

    QJsonArray messages;
    for (const Message &m : req.messages) {
        QJsonObject mo;
        mo[QStringLiteral("role")] = m.role;
        mo[QStringLiteral("content")] = serializeContent(m);
        messages.append(mo);
    }
    body[QStringLiteral("messages")] = messages;

    if (!req.tools.isEmpty()) {
        QJsonArray tools;
        for (const ToolDef &t : req.tools) {
            QJsonObject to;
            to[QStringLiteral("name")] = t.name;
            to[QStringLiteral("description")] = t.description;
            to[QStringLiteral("input_schema")] = t.inputSchema;
            tools.append(to);
        }
        body[QStringLiteral("tools")] = tools;
    }

    QNetworkReply *netReply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->attachNetworkReply(netReply);

    if (req.stream) {
        auto buffer = QSharedPointer<QByteArray>::create();
        auto state  = QSharedPointer<StreamState>::create();
        QObject::connect(netReply, &QNetworkReply::readyRead, reply,
                         [netReply, reply, buffer, state]() {
            *buffer += netReply->readAll();
            int sep;
            while ((sep = buffer->indexOf("\n\n")) != -1) {
                const QByteArray event = buffer->left(sep);
                *buffer = buffer->mid(sep + 2);
                QByteArray data;
                for (const QByteArray &line : event.split('\n')) {
                    if (line.startsWith("data: "))
                        data = line.mid(6);
                }
                if (!data.isEmpty())
                    parseSseEvent(data, reply, *state);
            }
        });
    }

    QObject::connect(netReply, &QNetworkReply::finished, reply,
                     [netReply, reply, stream = req.stream]() {
        if (netReply->error() != QNetworkReply::NoError) {
            QString msg = QString::fromUtf8(netReply->readAll());
            if (msg.isEmpty())
                msg = netReply->errorString();
            reply->setError(msg);
        } else if (!stream) {
            const QByteArray data = netReply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonArray content =
                doc.object().value(QStringLiteral("content")).toArray();
            QString text;
            for (const QJsonValue &v : content) {
                const QJsonObject part = v.toObject();
                const QString partType =
                    part.value(QStringLiteral("type")).toString();
                if (partType == QLatin1String("text")) {
                    text += part.value(QStringLiteral("text")).toString();
                } else if (partType == QLatin1String("tool_use")) {
                    ToolCall call;
                    call.id   = part.value(QStringLiteral("id")).toString();
                    call.name = part.value(QStringLiteral("name")).toString();
                    call.input =
                        part.value(QStringLiteral("input")).toObject();
                    reply->appendToolCall(call);
                }
            }
            reply->appendChunk(text);
            reply->markFinished();
        } else {
            reply->markFinished();
        }
        netReply->deleteLater();
    });

    return reply;
}
