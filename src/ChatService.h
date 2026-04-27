#pragma once

#include "ChatHistoryCache.h"
#include "ChatModel.h"
#include "LlmClient.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class PaperController;
class Settings;
class TocService;

class ChatService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(ChatModel *messages   READ messages   CONSTANT)
    Q_PROPERTY(bool       busy       READ busy       NOTIFY busyChanged)
    Q_PROPERTY(QString    lastError  READ lastError  NOTIFY lastErrorChanged)
    Q_PROPERTY(QString    defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    ChatService(Settings *settings,
                PaperController *paper,
                TocService *toc,
                QObject *parent = nullptr);
    ~ChatService() override;

    ChatModel *messages() { return &m_messages; }
    bool       busy()      const { return m_reply != nullptr; }
    QString    lastError() const { return m_lastError; }
    QString    defaultSystemPrompt() const;

public slots:
    void sendMessage(const QString &text);
    void cancel();
    void clear();

signals:
    void busyChanged();
    void lastErrorChanged();

private:
    void onPaperChanged();
    void rehydrateFromCache();
    void persistHistory();
    QString systemPrompt() const;
    void setLastError(const QString &err);

    void runTurn();
    void onTurnFinished();
    QVector<ToolDef> toolDefinitions() const;
    QString runTool(const ToolCall &call) const;
    QString runListSections() const;
    QString runReadPage(int page) const;
    QString runReadSection(const QString &sectionId) const;
    QString runGetUserSelection() const;
    void cleanupAfterFinal();

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<TocService> m_toc;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;

    ChatModel m_messages;
    ChatHistoryCache m_cache;
    QString m_lastError;

    // Conversation state sent to the API (includes tool_use / tool_result
    // round-trips that aren't surfaced in m_messages).
    QVector<LlmClient::Message> m_apiMessages;
    int m_iterations = 0;
};
