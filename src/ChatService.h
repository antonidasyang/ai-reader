#pragma once

#include "ChatHistoryCache.h"
#include "ChatModel.h"
#include "ChatSession.h"
#include "ChatSessionListModel.h"
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

    Q_PROPERTY(ChatModel             *messages         READ messages          CONSTANT)
    Q_PROPERTY(ChatSessionListModel  *sessions         READ sessionsModel     CONSTANT)
    Q_PROPERTY(QString                activeSessionId  READ activeSessionId   NOTIFY activeSessionChanged)
    Q_PROPERTY(bool                   busy             READ busy              NOTIFY busyChanged)
    Q_PROPERTY(QString                lastError        READ lastError         NOTIFY lastErrorChanged)
    Q_PROPERTY(QString                defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    ChatService(Settings *settings,
                PaperController *paper,
                TocService *toc,
                QObject *parent = nullptr);
    ~ChatService() override;

    ChatModel            *messages()       { return &m_messages; }
    ChatSessionListModel *sessionsModel()  { return &m_sessionsModel; }
    QString               activeSessionId() const;
    bool                  busy()      const { return m_reply != nullptr || !m_pendingTools.isEmpty(); }
    QString               lastError() const { return m_lastError; }
    QString               defaultSystemPrompt() const;

public slots:
    void sendMessage(const QString &text);
    void cancel();
    // Empties the current session's transcript without removing the
    // session itself. Use deleteSession to drop a session entirely.
    void clear();

    // Session management (called from the chat pane's session strip).
    // newSession creates an empty session and switches to it; activate
    // is a no-op when `id` already names the active session; delete
    // removes the session and falls back to a neighbour or a fresh
    // session when the last one is deleted.
    void newSession();
    void activateSession(const QString &id);
    void deleteSession(const QString &id);
    void renameSession(const QString &id, const QString &name);

signals:
    void busyChanged();
    void lastErrorChanged();
    void activeSessionChanged();

private:
    void onPaperChanged();
    void rehydrateFromCache();
    void persistHistory();
    void refreshSessionsModel();
    void syncActiveToSession();
    void loadSessionToActive();
    void touchActiveSession();
    void ensureAtLeastOneSession();
    void maybeAutoNameActiveSession();
    QString defaultSessionName() const;

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
    QString runSearchPaper(const QString &query, int topK) const;
    QString runGetFigureCaption(const QString &label) const;
    void dispatchTool(int slotIndex, const ToolCall &call);
    void onToolResolved(int slotIndex, const QString &result);
    void runReadPageVisualAsync(int slotIndex,
                                int page,
                                const QString &question);
    void cleanupAfterFinal();

    struct PendingTool {
        ToolCall call;
        QString  result;
        bool     resolved = false;
    };

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<TocService> m_toc;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;

    ChatModel m_messages;
    ChatSessionListModel m_sessionsModel;
    QVector<ChatSession> m_sessions;
    int m_activeIndex = -1;

    ChatHistoryCache m_cache;
    QString m_lastError;

    // Conversation state sent to the API for the active session
    // (includes tool_use / tool_result round-trips that aren't surfaced
    // in m_messages).
    QVector<LlmClient::Message> m_apiMessages;
    QVector<PendingTool> m_pendingTools;
    QList<QPointer<LlmReply>> m_toolReplies;
    int m_iterations = 0;
};
