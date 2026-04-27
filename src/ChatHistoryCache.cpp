#include "ChatHistoryCache.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QJsonObject contentPartToJson(const ContentPart &p)
{
    QJsonObject o;
    switch (p.type) {
    case ContentPart::Text:
        o[QStringLiteral("type")] = QStringLiteral("text");
        o[QStringLiteral("text")] = p.text;
        break;
    case ContentPart::Image:
        o[QStringLiteral("type")] = QStringLiteral("image");
        o[QStringLiteral("imagePng")] =
            QString::fromLatin1(p.imagePng.toBase64());
        break;
    case ContentPart::ToolUse:
        o[QStringLiteral("type")] = QStringLiteral("tool_use");
        o[QStringLiteral("id")] = p.toolId;
        o[QStringLiteral("name")] = p.toolName;
        o[QStringLiteral("input")] = p.toolInput;
        break;
    case ContentPart::ToolResult:
        o[QStringLiteral("type")] = QStringLiteral("tool_result");
        o[QStringLiteral("id")] = p.toolId;
        o[QStringLiteral("text")] = p.text;
        break;
    }
    return o;
}

ContentPart contentPartFromJson(const QJsonObject &o)
{
    ContentPart p;
    const QString t = o.value(QStringLiteral("type")).toString();
    if (t == QLatin1String("text")) {
        p.type = ContentPart::Text;
        p.text = o.value(QStringLiteral("text")).toString();
    } else if (t == QLatin1String("image")) {
        p.type = ContentPart::Image;
        p.imagePng = QByteArray::fromBase64(
            o.value(QStringLiteral("imagePng")).toString().toLatin1());
    } else if (t == QLatin1String("tool_use")) {
        p.type = ContentPart::ToolUse;
        p.toolId = o.value(QStringLiteral("id")).toString();
        p.toolName = o.value(QStringLiteral("name")).toString();
        p.toolInput = o.value(QStringLiteral("input")).toObject();
    } else if (t == QLatin1String("tool_result")) {
        p.type = ContentPart::ToolResult;
        p.toolId = o.value(QStringLiteral("id")).toString();
        p.text = o.value(QStringLiteral("text")).toString();
    }
    return p;
}

QJsonObject apiMessageToJson(const LlmClient::Message &m)
{
    QJsonObject o;
    o[QStringLiteral("role")] = m.role;
    if (!m.parts.isEmpty()) {
        QJsonArray arr;
        for (const ContentPart &p : m.parts)
            arr.append(contentPartToJson(p));
        o[QStringLiteral("parts")] = arr;
    } else {
        o[QStringLiteral("content")] = m.content;
    }
    return o;
}

LlmClient::Message apiMessageFromJson(const QJsonObject &o)
{
    LlmClient::Message m;
    m.role = o.value(QStringLiteral("role")).toString();
    if (o.contains(QStringLiteral("parts"))) {
        const QJsonArray arr = o.value(QStringLiteral("parts")).toArray();
        for (const QJsonValue &v : arr)
            m.parts.append(contentPartFromJson(v.toObject()));
    } else {
        m.content = o.value(QStringLiteral("content")).toString();
    }
    return m;
}

} // namespace

ChatHistoryCache::ChatHistoryCache(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/cache/chat");
    QDir().mkpath(m_cacheDir);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatHistoryCache::saveNow);
}

QString ChatHistoryCache::filePath() const
{
    if (m_paperId.isEmpty()) return {};
    return m_cacheDir + QChar('/') + m_paperId + QStringLiteral(".json");
}

void ChatHistoryCache::setPaperId(const QString &paperId)
{
    if (paperId == m_paperId) return;
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
    m_paperId = paperId;
    m_havePending = false;
    m_pendingMessages.clear();
    m_pendingApi.clear();
}

ChatHistoryCache::History ChatHistoryCache::load() const
{
    History h;
    const QString path = filePath();
    if (path.isEmpty()) return h;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return h;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return h;
    const QJsonObject root = doc.object();

    const QJsonArray msgs = root.value(QStringLiteral("messages")).toArray();
    h.messages.reserve(msgs.size());
    for (const QJsonValue &v : msgs) {
        const QJsonObject o = v.toObject();
        ChatMessage m;
        m.role = o.value(QStringLiteral("role")).toString();
        m.content = o.value(QStringLiteral("content")).toString();
        const int s = o.value(QStringLiteral("status")).toInt(int(ChatMessage::Done));
        m.status = (s == int(ChatMessage::Failed))
                       ? ChatMessage::Failed
                       : ChatMessage::Done;
        m.error = o.value(QStringLiteral("error")).toString();
        h.messages.append(m);
    }

    const QJsonArray api = root.value(QStringLiteral("api")).toArray();
    h.apiMessages.reserve(api.size());
    for (const QJsonValue &v : api)
        h.apiMessages.append(apiMessageFromJson(v.toObject()));

    return h;
}

void ChatHistoryCache::save(const QVector<ChatMessage> &messages,
                            const QVector<LlmClient::Message> &apiMessages)
{
    if (m_paperId.isEmpty()) return;
    m_pendingMessages = messages;
    m_pendingApi = apiMessages;
    m_havePending = true;
    scheduleSave();
}

void ChatHistoryCache::clear()
{
    if (m_paperId.isEmpty()) return;
    m_pendingMessages.clear();
    m_pendingApi.clear();
    m_havePending = true;
    if (m_saveTimer.isActive())
        m_saveTimer.stop();
    saveNow();
}

void ChatHistoryCache::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void ChatHistoryCache::saveNow()
{
    const QString path = filePath();
    if (path.isEmpty() || !m_havePending) return;

    if (m_pendingMessages.isEmpty() && m_pendingApi.isEmpty()) {
        QFile::remove(path);
        m_havePending = false;
        return;
    }

    QJsonArray msgs;
    for (const ChatMessage &m : m_pendingMessages) {
        QJsonObject o;
        o[QStringLiteral("role")] = m.role;
        o[QStringLiteral("content")] = m.content;
        o[QStringLiteral("status")] = int(m.status);
        if (!m.error.isEmpty())
            o[QStringLiteral("error")] = m.error;
        msgs.append(o);
    }

    QJsonArray api;
    for (const LlmClient::Message &m : m_pendingApi)
        api.append(apiMessageToJson(m));

    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("messages")] = msgs;
    root[QStringLiteral("api")] = api;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
    m_havePending = false;
}
