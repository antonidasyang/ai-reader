#include "AnthropicClient.h"

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

void parseSseEvent(const QByteArray &payload, LlmReply *reply)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
        if (delta.value(QStringLiteral("type")).toString() == QLatin1String("text_delta"))
            reply->appendChunk(delta.value(QStringLiteral("text")).toString());
    } else if (type == QLatin1String("message_stop")) {
        reply->markFinished();
    } else if (type == QLatin1String("error")) {
        const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
        reply->setError(err.value(QStringLiteral("message")).toString(
            QStringLiteral("Anthropic error")));
    }
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
        if (m.images.isEmpty()) {
            mo[QStringLiteral("content")] = m.content;
        } else {
            // Multimodal: image blocks first, then a text block.
            QJsonArray content;
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
            mo[QStringLiteral("content")] = content;
        }
        messages.append(mo);
    }
    body[QStringLiteral("messages")] = messages;

    QNetworkReply *netReply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->attachNetworkReply(netReply);

    if (req.stream) {
        auto buffer = QSharedPointer<QByteArray>::create();
        QObject::connect(netReply, &QNetworkReply::readyRead, reply,
                         [netReply, reply, buffer]() {
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
                    parseSseEvent(data, reply);
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
                if (part.value(QStringLiteral("type")).toString()
                    == QLatin1String("text"))
                    text += part.value(QStringLiteral("text")).toString();
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
