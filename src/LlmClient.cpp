#include "LlmClient.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <QNetworkReply>

#include <chrono>

namespace {

// Streaming replies are long-lived by design, so this has to be — and is
// — an *inactivity* timeout: Qt restarts the clock on every byte that
// arrives, so a slow model that keeps trickling tokens is never cut off.
// Without it, a gateway that accepts the request, answers 200 and then
// goes silent pins the reply forever: no error, no finish, and in
// TranslationService the row keeps its slot in the two-deep queue, so
// the whole batch stalls with nothing to show the user.
constexpr std::chrono::seconds kIdleTimeout{120};

} // namespace

LlmReply::LlmReply(QObject *parent)
    : QObject(parent)
{
}

LlmReply::~LlmReply() = default;

void LlmReply::abort()
{
    if (m_networkReply && !m_networkReply->isFinished())
        m_networkReply->abort();
}

void LlmReply::appendChunk(const QString &chunk)
{
    if (chunk.isEmpty())
        return;
    m_text += chunk;
    emit chunkReceived(chunk);
}

void LlmReply::appendToolCall(const ToolCall &call)
{
    m_toolCalls.append(call);
}

void LlmReply::markFinished()
{
    if (m_finished)
        return;
    m_finished = true;
    emit finished();
}

void LlmReply::setError(const QString &message)
{
    if (m_finished)
        return;
    m_error = message;
    m_finished = true;
    emit errorOccurred(message);
}

void LlmReply::attachNetworkReply(QNetworkReply *reply)
{
    m_networkReply = reply;
}

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setTransferTimeout(kIdleTimeout);
}

LlmClient::~LlmClient() = default;

QString LlmClient::describeHttpError(const QByteArray &body, int httpStatus,
                                     const QString &fallback)
{
    QString detail;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        const QJsonValue err = o.value(QStringLiteral("error"));
        if (err.isObject())
            detail = err.toObject().value(QStringLiteral("message")).toString();
        else if (err.isString())
            detail = err.toString();
        if (detail.isEmpty())
            detail = o.value(QStringLiteral("message")).toString();
    }
    if (detail.isEmpty()) {
        // Not JSON -- an HTML error page from a proxy, most likely. Show a
        // trimmed slice rather than a wall of markup.
        detail = QString::fromUtf8(body).simplified();
        if (detail.size() > 400)
            detail = detail.left(400) + QStringLiteral("…");
    }
    if (detail.isEmpty())
        return fallback;
    return httpStatus > 0 ? tr("HTTP %1: %2").arg(httpStatus).arg(detail)
                          : detail;
}
