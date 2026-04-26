#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkReply;

struct ToolDef {
    QString name;
    QString description;
    QJsonObject inputSchema;   // JSON Schema for the tool's input object.
};

struct ToolCall {
    QString id;
    QString name;
    QJsonObject input;
};

// One element of a multimodal/tool-aware message body. Use ContentPart-based
// `Message::parts` for any message that includes images, tool_use, or
// tool_result blocks; the simple `Message::content` string is still fine
// for plain-text turns.
struct ContentPart {
    enum Type { Text, Image, ToolUse, ToolResult };
    Type        type = Text;
    QString     text;          // for Text and ToolResult.content
    QByteArray  imagePng;      // for Image
    QString     toolId;        // for ToolUse and ToolResult (tool_use_id)
    QString     toolName;      // for ToolUse only
    QJsonObject toolInput;     // for ToolUse only
};

class LlmReply : public QObject
{
    Q_OBJECT
public:
    explicit LlmReply(QObject *parent = nullptr);
    ~LlmReply() override;

    QString text() const { return m_text; }
    bool isFinished() const { return m_finished; }
    QString errorString() const { return m_error; }
    QList<ToolCall> toolCalls() const { return m_toolCalls; }

    // Used by client implementations.
    void appendChunk(const QString &chunk);
    void appendToolCall(const ToolCall &call);
    void markFinished();
    void setError(const QString &message);
    void attachNetworkReply(QNetworkReply *reply);

public slots:
    void abort();

signals:
    void chunkReceived(const QString &chunk);
    void finished();
    void errorOccurred(const QString &message);

private:
    QPointer<QNetworkReply> m_networkReply;
    QString m_text;
    QString m_error;
    QList<ToolCall> m_toolCalls;
    bool m_finished = false;
};

class LlmClient : public QObject
{
    Q_OBJECT
public:
    struct Message {
        QString role;     // "user" / "assistant"
        QString content;
        // Optional inline image attachments (raw PNG bytes). Sent before
        // the text content in providers that support multimodal input.
        QList<QByteArray> images;
        // When non-empty, this overrides `content`/`images` and lets the
        // caller send a fully structured payload (text + tool_use +
        // tool_result, in any order).
        QList<ContentPart> parts;
    };

    struct Request {
        QString system;
        QVector<Message> messages;
        QVector<ToolDef> tools;
        double temperature = 0.2;
        int maxTokens = 4096;
        bool stream = true;
    };

    explicit LlmClient(QObject *parent = nullptr);
    ~LlmClient() override;

    virtual LlmReply *send(const Request &req) = 0;

    QString apiKey() const { return m_apiKey; }
    QUrl baseUrl() const { return m_baseUrl; }
    QString model() const { return m_model; }

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }

protected:
    QNetworkAccessManager *m_nam;
    QString m_apiKey;
    QUrl m_baseUrl;
    QString m_model;
};
