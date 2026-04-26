#include "OpenAiClient.h"

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

void parseSseChunk(const QByteArray &payload, LlmReply *reply)
{
    if (payload == "[DONE]") {
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

    if (!choice.value(QStringLiteral("finish_reason")).isNull())
        reply->markFinished();
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
        QJsonObject mo;
        mo[QStringLiteral("role")] = m.role;
        mo[QStringLiteral("content")] = m.content;
        messages.append(mo);
    }

    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("temperature")] = req.temperature;
    body[QStringLiteral("max_tokens")] = req.maxTokens;
    body[QStringLiteral("stream")] = req.stream;

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
                for (const QByteArray &line : event.split('\n')) {
                    if (line.startsWith("data: "))
                        parseSseChunk(line.mid(6).trimmed(), reply);
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
                const QString text = choices.first().toObject()
                    .value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("content")).toString();
                reply->appendChunk(text);
            }
            reply->markFinished();
        } else {
            reply->markFinished();
        }
        netReply->deleteLater();
    });

    return reply;
}
