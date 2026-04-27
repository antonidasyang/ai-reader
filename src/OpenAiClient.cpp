#include "OpenAiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>

namespace {

QUrl chatEndpoint(QUrl base)
{
    QString path = base.path();
    if (path.endsWith(QChar('/')))
        path.chop(1);
    base.setPath(path + QStringLiteral("/v1/chat/completions"));
    return base;
}

// Streaming state: OpenAI emits tool_calls as deltas — each delta carries
// the call's `index` and a partial slice of {id, function.name,
// function.arguments}. We accumulate per-index until finish_reason
// flips to a non-null value.
struct ToolCallBuilder {
    QString id;
    QString name;
    QString argsBuffer;
};
struct StreamState {
    QHash<int, ToolCallBuilder> active;
};

void emitPendingToolCalls(StreamState &state, LlmReply *reply)
{
    QList<int> indices = state.active.keys();
    std::sort(indices.begin(), indices.end());
    for (int idx : indices) {
        const ToolCallBuilder &b = state.active.value(idx);
        ToolCall call;
        call.id = b.id;
        call.name = b.name;
        QJsonParseError jerr{};
        const QByteArray buf = b.argsBuffer.isEmpty()
                               ? QByteArrayLiteral("{}")
                               : b.argsBuffer.toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(buf, &jerr);
        if (jerr.error == QJsonParseError::NoError && doc.isObject())
            call.input = doc.object();
        reply->appendToolCall(call);
    }
    state.active.clear();
}

void parseSseChunk(const QByteArray &payload,
                   LlmReply *reply,
                   StreamState &state)
{
    if (payload == "[DONE]") {
        emitPendingToolCalls(state, reply);
        reply->markFinished();
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonArray choices =
        doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    const QString content = delta.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
        reply->appendChunk(content);

    const QJsonValue toolCallsVal =
        delta.value(QStringLiteral("tool_calls"));
    if (toolCallsVal.isArray()) {
        for (const QJsonValue &tcv : toolCallsVal.toArray()) {
            const QJsonObject tc = tcv.toObject();
            const int idx = tc.value(QStringLiteral("index")).toInt();
            ToolCallBuilder &b = state.active[idx];
            const QString id = tc.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) b.id = id;
            const QJsonObject fn =
                tc.value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) b.name = name;
            const QString args =
                fn.value(QStringLiteral("arguments")).toString();
            if (!args.isEmpty()) b.argsBuffer += args;
        }
    }

    const QJsonValue finishReason =
        choice.value(QStringLiteral("finish_reason"));
    if (!finishReason.isNull() && !finishReason.isUndefined()) {
        emitPendingToolCalls(state, reply);
        reply->markFinished();
    }
}

// Convert a structured Message (with parts) into one OR MORE OpenAI
// messages. tool_result parts become separate `role: "tool"` messages
// (per OpenAI's conversation shape, which doesn't allow tool_result
// blocks inline).
QJsonArray serializeMessage(const LlmClient::Message &m)
{
    QJsonArray out;

    if (m.parts.isEmpty() && m.images.isEmpty()) {
        QJsonObject mo;
        mo[QStringLiteral("role")] = m.role;
        mo[QStringLiteral("content")] = m.content;
        out.append(mo);
        return out;
    }

    if (m.parts.isEmpty()) {
        // Legacy multimodal: image_url blocks first, then a text block.
        QJsonArray content;
        for (const QByteArray &png : m.images) {
            const QString dataUri = QStringLiteral("data:image/png;base64,")
                                  + QString::fromUtf8(png.toBase64());
            QJsonObject url;
            url[QStringLiteral("url")] = dataUri;
            QJsonObject block;
            block[QStringLiteral("type")] = QStringLiteral("image_url");
            block[QStringLiteral("image_url")] = url;
            content.append(block);
        }
        if (!m.content.isEmpty()) {
            QJsonObject text;
            text[QStringLiteral("type")] = QStringLiteral("text");
            text[QStringLiteral("text")] = m.content;
            content.append(text);
        }
        QJsonObject mo;
        mo[QStringLiteral("role")] = m.role;
        mo[QStringLiteral("content")] = content;
        out.append(mo);
        return out;
    }

    // Structured parts.
    if (m.role == QLatin1String("assistant")) {
        QString text;
        QJsonArray toolCalls;
        for (const ContentPart &p : m.parts) {
            switch (p.type) {
            case ContentPart::Text:
                text += p.text;
                break;
            case ContentPart::ToolUse: {
                QJsonObject fn;
                fn[QStringLiteral("name")] = p.toolName;
                fn[QStringLiteral("arguments")] = QString::fromUtf8(
                    QJsonDocument(p.toolInput).toJson(QJsonDocument::Compact));
                QJsonObject tc;
                tc[QStringLiteral("id")]   = p.toolId;
                tc[QStringLiteral("type")] = QStringLiteral("function");
                tc[QStringLiteral("function")] = fn;
                toolCalls.append(tc);
                break;
            }
            default:
                break;  // image / tool_result not valid on assistant
            }
        }
        QJsonObject mo;
        mo[QStringLiteral("role")] = QStringLiteral("assistant");
        if (text.isEmpty())
            mo[QStringLiteral("content")] = QJsonValue(QJsonValue::Null);
        else
            mo[QStringLiteral("content")] = text;
        if (!toolCalls.isEmpty())
            mo[QStringLiteral("tool_calls")] = toolCalls;
        out.append(mo);
        return out;
    }

    // role: "user" with structured parts. tool_result parts each become a
    // standalone role:tool message; the rest collapse into one user
    // message with a content array.
    QJsonArray userContent;
    for (const ContentPart &p : m.parts) {
        switch (p.type) {
        case ContentPart::ToolResult: {
            QJsonObject toolMsg;
            toolMsg[QStringLiteral("role")] = QStringLiteral("tool");
            toolMsg[QStringLiteral("tool_call_id")] = p.toolId;
            toolMsg[QStringLiteral("content")] = p.text;
            out.append(toolMsg);
            break;
        }
        case ContentPart::Text: {
            QJsonObject block;
            block[QStringLiteral("type")] = QStringLiteral("text");
            block[QStringLiteral("text")] = p.text;
            userContent.append(block);
            break;
        }
        case ContentPart::Image: {
            const QString dataUri = QStringLiteral("data:image/png;base64,")
                                  + QString::fromUtf8(p.imagePng.toBase64());
            QJsonObject url;
            url[QStringLiteral("url")] = dataUri;
            QJsonObject block;
            block[QStringLiteral("type")] = QStringLiteral("image_url");
            block[QStringLiteral("image_url")] = url;
            userContent.append(block);
            break;
        }
        default:
            break;  // tool_use isn't valid on user
        }
    }
    if (!userContent.isEmpty()) {
        QJsonObject mo;
        mo[QStringLiteral("role")] = QStringLiteral("user");
        mo[QStringLiteral("content")] = userContent;
        out.append(mo);
    }
    return out;
}

} // namespace

OpenAiClient::OpenAiClient(QObject *parent)
    : LlmClient(parent)
{
    m_baseUrl = QUrl(QStringLiteral("https://api.openai.com"));
}

LlmReply *OpenAiClient::send(const Request &req)
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

    QNetworkRequest request(chatEndpoint(m_baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    if (req.stream)
        request.setRawHeader("Accept", "text/event-stream");

    QJsonArray messages;
    if (!req.system.isEmpty()) {
        QJsonObject sys;
        sys[QStringLiteral("role")] = QStringLiteral("system");
        sys[QStringLiteral("content")] = req.system;
        messages.append(sys);
    }
    for (const Message &m : req.messages) {
        const QJsonArray parts = serializeMessage(m);
        for (const QJsonValue &v : parts)
            messages.append(v);
    }

    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("temperature")] = req.temperature;
    body[QStringLiteral("max_tokens")] = req.maxTokens;
    body[QStringLiteral("stream")] = req.stream;

    if (!req.tools.isEmpty()) {
        QJsonArray tools;
        for (const ToolDef &t : req.tools) {
            QJsonObject fn;
            fn[QStringLiteral("name")] = t.name;
            fn[QStringLiteral("description")] = t.description;
            fn[QStringLiteral("parameters")] = t.inputSchema;
            QJsonObject tool;
            tool[QStringLiteral("type")] = QStringLiteral("function");
            tool[QStringLiteral("function")] = fn;
            tools.append(tool);
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
                for (const QByteArray &line : event.split('\n')) {
                    if (line.startsWith("data: "))
                        parseSseChunk(line.mid(6).trimmed(), reply, *state);
                }
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
            const QJsonArray choices =
                doc.object().value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                const QJsonObject msg = choices.first().toObject()
                    .value(QStringLiteral("message")).toObject();
                const QString text =
                    msg.value(QStringLiteral("content")).toString();
                if (!text.isEmpty())
                    reply->appendChunk(text);
                const QJsonArray tcs =
                    msg.value(QStringLiteral("tool_calls")).toArray();
                for (const QJsonValue &tcv : tcs) {
                    const QJsonObject tc = tcv.toObject();
                    ToolCall call;
                    call.id = tc.value(QStringLiteral("id")).toString();
                    const QJsonObject fn =
                        tc.value(QStringLiteral("function")).toObject();
                    call.name =
                        fn.value(QStringLiteral("name")).toString();
                    const QString args =
                        fn.value(QStringLiteral("arguments")).toString();
                    QJsonParseError jerr{};
                    const QJsonDocument inputDoc =
                        QJsonDocument::fromJson(args.toUtf8(), &jerr);
                    if (jerr.error == QJsonParseError::NoError
                        && inputDoc.isObject())
                        call.input = inputDoc.object();
                    reply->appendToolCall(call);
                }
            }
            reply->markFinished();
        } else {
            reply->markFinished();
        }
        netReply->deleteLater();
    });

    return reply;
}
