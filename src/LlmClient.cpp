#include "LlmClient.h"

#include <QNetworkReply>

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
}

LlmClient::~LlmClient() = default;
