#pragma once

#include "ChatModel.h"

#include <QObject>
#include <QPointer>
#include <QString>

class LlmClient;
class LlmReply;
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
    QString systemPrompt() const;
    void setLastError(const QString &err);

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<TocService> m_toc;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;

    ChatModel m_messages;
    QString m_lastError;
};
