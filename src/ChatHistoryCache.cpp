#include "ChatHistoryCache.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

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

QJsonArray messagesToJson(const QVector<ChatMessage> &messages)
{
    QJsonArray arr;
    for (const ChatMessage &m : messages) {
        QJsonObject o;
        o[QStringLiteral("role")] = m.role;
        o[QStringLiteral("content")] = m.content;
        o[QStringLiteral("status")] = int(m.status);
        if (!m.error.isEmpty())
            o[QStringLiteral("error")] = m.error;
        arr.append(o);
    }
    return arr;
}

QVector<ChatMessage> messagesFromJson(const QJsonArray &arr)
{
    QVector<ChatMessage> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ChatMessage m;
        m.role = o.value(QStringLiteral("role")).toString();
        m.content = o.value(QStringLiteral("content")).toString();
        const int s = o.value(QStringLiteral("status")).toInt(int(ChatMessage::Done));
        m.status = (s == int(ChatMessage::Failed))
                       ? ChatMessage::Failed
                       : ChatMessage::Done;
        m.error = o.value(QStringLiteral("error")).toString();
        out.append(m);
    }
    return out;
}

QJsonArray apiToJson(const QVector<LlmClient::Message> &api)
{
    QJsonArray arr;
    for (const LlmClient::Message &m : api)
        arr.append(apiMessageToJson(m));
    return arr;
}

QVector<LlmClient::Message> apiFromJson(const QJsonArray &arr)
{
    QVector<LlmClient::Message> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(apiMessageFromJson(v.toObject()));
    return out;
}

QJsonObject sessionToJson(const ChatSession &s)
{
    QJsonObject o;
    o[QStringLiteral("id")] = s.id;
    o[QStringLiteral("name")] = s.name;
    o[QStringLiteral("autoNamed")] = s.autoNamed;
    o[QStringLiteral("createdAt")] = s.createdAt.toString(Qt::ISODate);
    o[QStringLiteral("updatedAt")] = s.updatedAt.toString(Qt::ISODate);
    o[QStringLiteral("messages")] = messagesToJson(s.messages);
    o[QStringLiteral("api")] = apiToJson(s.apiMessages);
    return o;
}

ChatSession sessionFromJson(const QJsonObject &o)
{
    ChatSession s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.autoNamed = o.value(QStringLiteral("autoNamed")).toBool(true);
    s.createdAt = QDateTime::fromString(
        o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    s.updatedAt = QDateTime::fromString(
        o.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);
    s.messages = messagesFromJson(
        o.value(QStringLiteral("messages")).toArray());
    s.apiMessages = apiFromJson(
        o.value(QStringLiteral("api")).toArray());
    if (s.id.isEmpty())
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!s.createdAt.isValid())
        s.createdAt = QDateTime::currentDateTime();
    if (!s.updatedAt.isValid())
        s.updatedAt = s.createdAt;
    return s;
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
    m_pendingSessions.clear();
    m_pendingActiveId.clear();
}

ChatHistoryCache::Snapshot ChatHistoryCache::load() const
{
    Snapshot snap;
    const QString path = filePath();
    if (path.isEmpty()) return snap;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return snap;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return snap;
    const QJsonObject root = doc.object();

    // New format: a "sessions" array plus "activeSessionId".
    if (root.contains(QStringLiteral("sessions"))) {
        const QJsonArray arr = root.value(QStringLiteral("sessions")).toArray();
        snap.sessions.reserve(arr.size());
        for (const QJsonValue &v : arr)
            snap.sessions.append(sessionFromJson(v.toObject()));
        snap.activeId = root.value(QStringLiteral("activeSessionId")).toString();
        return snap;
    }

    // Legacy format: top-level "messages"/"api". Migrate to a single
    // session so existing chat history isn't lost on upgrade.
    if (root.contains(QStringLiteral("messages"))
        || root.contains(QStringLiteral("api"))) {
        ChatSession s;
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.name = tr("Chat");
        s.autoNamed = true;
        s.createdAt = QDateTime::currentDateTime();
        s.updatedAt = s.createdAt;
        s.messages = messagesFromJson(
            root.value(QStringLiteral("messages")).toArray());
        s.apiMessages = apiFromJson(
            root.value(QStringLiteral("api")).toArray());
        snap.sessions.append(s);
        snap.activeId = s.id;
    }
    return snap;
}

void ChatHistoryCache::save(const QVector<ChatSession> &sessions,
                            const QString &activeId)
{
    if (m_paperId.isEmpty()) return;
    m_pendingSessions = sessions;
    m_pendingActiveId = activeId;
    m_havePending = true;
    scheduleSave();
}

void ChatHistoryCache::clear()
{
    if (m_paperId.isEmpty()) return;
    m_pendingSessions.clear();
    m_pendingActiveId.clear();
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

    if (m_pendingSessions.isEmpty()) {
        QFile::remove(path);
        m_havePending = false;
        return;
    }

    QJsonArray sessions;
    for (const ChatSession &s : m_pendingSessions)
        sessions.append(sessionToJson(s));

    QJsonObject root;
    root[QStringLiteral("paperId")] = m_paperId;
    root[QStringLiteral("version")] = 2;
    root[QStringLiteral("activeSessionId")] = m_pendingActiveId;
    root[QStringLiteral("sessions")] = sessions;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("ChatHistoryCache: cannot open %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning("ChatHistoryCache: commit failed for %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f.errorString()));
    m_havePending = false;
}
