#pragma once

#include "TranslationCache.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>

class BlockListModel;
class LlmClient;
class LlmReply;
class PaperController;
class Settings;

class TranslationService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy             READ busy             NOTIFY busyChanged)
    Q_PROPERTY(int  doneCount        READ doneCount        NOTIFY progressChanged)
    Q_PROPERTY(int  totalCount       READ totalCount       NOTIFY progressChanged)
    Q_PROPERTY(int  failedCount      READ failedCount      NOTIFY progressChanged)
    Q_PROPERTY(QString lastError     READ lastError        NOTIFY lastErrorChanged)
    Q_PROPERTY(QString defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    TranslationService(Settings *settings,
                       PaperController *paper,
                       QObject *parent = nullptr);
    ~TranslationService() override;

    bool busy() const { return m_inflight > 0 || !m_pending.isEmpty(); }
    int doneCount()   const { return m_done; }
    int totalCount()  const { return m_total; }
    int failedCount() const { return m_failed; }
    QString lastError() const { return m_lastError; }
    QString defaultSystemPrompt() const;

public slots:
    void translateAll();
    void retryFailed();
    void cancel();
    // Translate a single paragraph (used by the BlockList right-click
    // menu). No-op when the row is invalid, the LLM isn't configured,
    // or the row is already translating.
    Q_INVOKABLE void translateBlock(int row);

signals:
    void busyChanged();
    void progressChanged();
    void lastErrorChanged();

private:
    void onPaperChanged();
    void rehydrateFromCache();
    void scheduleNext();
    void translateRow(int row);
    bool shouldSkip(const QString &text) const;
    QString systemPrompt() const;
    void setLastError(const QString &err);

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<BlockListModel> m_model;
    QPointer<LlmClient> m_client;
    TranslationCache m_cache;

    QQueue<int> m_pending;
    QHash<LlmReply *, int> m_replyToRow;
    int m_inflight = 0;
    int m_maxInflight = 2;
    int m_done = 0;
    int m_failed = 0;
    int m_total = 0;
    QString m_lastError;
};
