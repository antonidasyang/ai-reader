#pragma once

#include "TocModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class BlockListModel;
class LlmClient;
class LlmReply;
class Settings;

class TocService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(TocModel *sections   READ sections   CONSTANT)
    Q_PROPERTY(Status   status      READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString  lastError   READ lastError  NOTIFY statusChanged)
    Q_PROPERTY(int      sectionCount READ sectionCount NOTIFY sectionsChanged)
    Q_PROPERTY(QString  defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    enum Status { Idle, Generating, Done, Failed };
    Q_ENUM(Status)

    explicit TocService(Settings *settings,
                        BlockListModel *blocks,
                        QObject *parent = nullptr);
    ~TocService() override;

    TocModel *sections()    { return &m_model; }
    Status   status()       const { return m_status; }
    QString  lastError()    const { return m_lastError; }
    int      sectionCount() const { return m_model.sectionCount(); }
    QString  defaultSystemPrompt() const;

public slots:
    void generate();
    void cancel();
    void clear();

signals:
    void statusChanged();
    void sectionsChanged();
    // Emitted when user clicks a section (forwarded by QML).
    void navigationRequested(int blockId, int page);

private:
    void onModelReset();
    QString systemPrompt() const;
    QString userPrompt() const;
    void parseResponse(const QString &text);
    void setStatus(Status s, const QString &err = {});

    QPointer<Settings> m_settings;
    QPointer<BlockListModel> m_blocks;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;
    TocModel m_model;

    // Map block_id → page recorded when we sent the heading list, so we can
    // resolve the LLM's `start_block` references back to a navigable page.
    QHash<int, int> m_blockIdToPage;

    QString m_buffer;
    Status m_status = Idle;
    QString m_lastError;
};
