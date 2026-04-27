#pragma once

#include "SummaryCache.h"

#include <QObject>
#include <QPointer>
#include <QString>

class BlockListModel;
class LlmClient;
class LlmReply;
class PaperController;
class Settings;

class SummaryService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString text       READ text       NOTIFY textChanged)
    Q_PROPERTY(Status  status     READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString lastError  READ lastError  NOTIFY statusChanged)
    Q_PROPERTY(QString paperTitle READ paperTitle WRITE setPaperTitle NOTIFY paperTitleChanged)
    Q_PROPERTY(QString defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    enum Status { Idle, Generating, Done, Failed };
    Q_ENUM(Status)

    SummaryService(Settings *settings,
                   PaperController *paper,
                   QObject *parent = nullptr);
    ~SummaryService() override;

    QString text()       const { return m_text; }
    Status  status()     const { return m_status; }
    QString lastError()  const { return m_lastError; }
    QString paperTitle() const { return m_paperTitle; }
    QString defaultSystemPrompt() const;

    void setPaperTitle(const QString &title);

public slots:
    void generate();
    void cancel();
    void clear();

signals:
    void textChanged();
    void statusChanged();
    void paperTitleChanged();

private:
    void onPaperChanged();
    void rehydrateFromCache();
    QString systemPrompt() const;
    QString userPrompt() const;
    void setStatus(Status s, const QString &err = {});

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<BlockListModel> m_blocks;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;
    SummaryCache m_cache;

    QString m_text;
    QString m_paperTitle;
    QString m_lastError;
    Status m_status = Idle;
};
