#pragma once

#include "ChatModel.h"
#include "LlmClient.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// On-disk JSON cache of a paper's chat conversation. One file per paper at
// <AppDataLocation>/cache/chat/<paperId>.json. Saves both the user-visible
// ChatModel turns and the structured API history (which carries tool_use /
// tool_result round-trips) so reopening the paper restores both views.
class ChatHistoryCache : public QObject
{
    Q_OBJECT
public:
    struct History {
        QVector<ChatMessage> messages;
        QVector<LlmClient::Message> apiMessages;
    };

    explicit ChatHistoryCache(QObject *parent = nullptr);

    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    History load() const;
    void save(const QVector<ChatMessage> &messages,
              const QVector<LlmClient::Message> &apiMessages);
    void clear();

private:
    void scheduleSave();
    void saveNow();
    QString filePath() const;

    QString m_paperId;
    QString m_cacheDir;

    QVector<ChatMessage> m_pendingMessages;
    QVector<LlmClient::Message> m_pendingApi;
    bool m_havePending = false;
    QTimer m_saveTimer;
};
