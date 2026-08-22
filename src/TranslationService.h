#pragma once

#include "SnippetModel.h"
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

    // ── Selection translation ─────────────────────────────────────────
    // One row per open card. Cards are pinned: they outlive the
    // selection that made them and only close on request.
    Q_PROPERTY(QAbstractListModel *snippets READ snippets CONSTANT)

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

    QAbstractListModel *snippets() { return &m_snippets; }

    // The translation cache behind the current paper. PaperSyncService fills
    // it from (and publishes it to) the project; nothing else touches it.
    TranslationCache *cache() { return &m_cache; }

public slots:
    void translateAll();
    void retryFailed();
    void cancel();
    // Translate a single paragraph (used by the BlockList right-click
    // menu). No-op when the row is invalid, the LLM isn't configured,
    // or the row is already translating.
    void translateBlock(int row);
    // Translate what the user selected in the PDF (right-click →
    // Translate) into a new card, and return that card's id. When the
    // selection can be placed inside a paragraph the app knows, that
    // paragraph is translated instead — same row, same cache, so the
    // right pane fills in too and a second look is free. Otherwise the
    // selected text is translated on its own. `page` is a hint used to
    // disambiguate identical paragraphs.
    int translateSelection(const QString &text, int page = -1);
    // Close one card, aborting its ad-hoc request if it had one. A
    // paragraph translation already under way is left running — it
    // belongs to the right pane too.
    void closeSnippet(int id);
    void closeAllSnippets();
    // Pull anything new in the cache onto the paragraphs on screen. Called
    // by PaperSyncService when a collaborator's translations land after the
    // paper was opened. Ignored while a translation run is in flight.
    void refreshFromCache();

signals:
    void busyChanged();
    void progressChanged();
    void lastErrorChanged();
    // The cache has been switched to `paperId` and loaded, and the
    // paragraphs on screen have not been rehydrated from it yet — the
    // window in which PaperSyncService merges in what the project already
    // has, so the first rehydrate already sees it.
    void translationCacheReady(const QString &paperId);

private:
    void onPaperChanged();
    void rehydrateFromCache();
    void scheduleNext();
    void translateRow(int row);
    bool shouldSkip(const QString &text) const;
    QString systemPrompt() const;
    void setLastError(const QString &err);
    // Ensures m_client exists and carries the current settings.
    void refreshClient();
    // Row whose source text contains `text`, or -1. Whitespace is
    // ignored on both sides so PDF line breaks don't defeat the match.
    int findBlockRow(const QString &text, int page) const;
    // Push a block row's text and status into every card mirroring it.
    void syncBlockRow(int row);
    void translateSelectionAdHoc(int snippetId, const QString &text);

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

    // Open selection cards, plus the ad-hoc requests feeding them
    // (paragraph cards are fed by m_replyToRow instead).
    SnippetModel m_snippets;
    QHash<LlmReply *, int> m_snippetReplies;   // reply → snippet id
};
