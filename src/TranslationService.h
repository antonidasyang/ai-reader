#pragma once

#include "LlmClientCache.h"
#include "Block.h"
#include "SnippetModel.h"
#include "TranslationCache.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <initializer_list>

class BlockListModel;
class LlmClient;
class LlmReply;
class PaperController;
class Settings;
class TaskManager;

class TranslationService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy             READ busy             NOTIFY busyChanged)
    Q_PROPERTY(int  doneCount        READ doneCount        NOTIFY progressChanged)
    Q_PROPERTY(int  totalCount       READ totalCount       NOTIFY progressChanged)
    Q_PROPERTY(int  failedCount      READ failedCount      NOTIFY progressChanged)
    Q_PROPERTY(QString lastError     READ lastError        NOTIFY lastErrorChanged)
    // Papers other than the one on screen that still have work running. A
    // run belongs to the paper it was started on and keeps going when the
    // reader moves to another tab, so the pane says how much is in the air.
    Q_PROPERTY(int backgroundPapers READ backgroundPapers NOTIFY progressChanged)
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

    // All four describe the paper on screen. They are counted off the
    // paragraphs themselves rather than tallied as work goes by: a tally that
    // both the finishing jobs and the cache-rehydrate added to could count the
    // same paragraph twice, which is how "translating 419/382" happened.
    // Another paper's run moves none of these — it has no rows here.
    bool busy() const;
    int doneCount()   const;
    int totalCount()  const;
    int failedCount() const;
    int backgroundPapers() const;
    QString lastError() const { return m_lastError; }
    QString defaultSystemPrompt() const;

    QAbstractListModel *snippets() { return &m_snippets; }

    // The translation cache behind the current paper. PaperSyncService fills
    // it from (and publishes it to) the project; nothing else touches it.
    TranslationCache *cache() { return &m_cache; }

    // Stop the run for one paper wherever it is — used when its tab closes,
    // since otherwise a paper nobody has open keeps burning tokens.
    void cancelPaper(const QString &paperId);

    // The queue every run goes through. Optional: with no manager each
    // entry point below does the work directly, exactly as it always did,
    // which is what the harnesses rely on.
    void setTasks(TaskManager *tasks);

public slots:
    // Translate the paragraphs that have no translation yet.
    void translateAll();
    // Throw away what this paper already has and translate all of it again.
    // The cache is overwritten as the new results land, so this really does
    // re-ask the model rather than serving what is on disk.
    Q_INVOKABLE void retranslateAll();
    // How the pane decides whether the two are even different: with nothing
    // translated there is nothing to ask the reader about.
    Q_INVOKABLE int translatedParagraphs() const;
    Q_INVOKABLE int untranslatedParagraphs() const;
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
    // What a whole-paper run is asked to do. One task covers the run from
    // end to end — the paragraphs inside it are its steps, not tasks of
    // their own, and the run parallelises them under its own cap.
    enum class RunMode {
        All,          // everything that has no translation yet
        Retranslate,  // throw away what is there and ask again
        Failed,       // only the paragraphs that failed
    };
    // Submit a run as a task, or — with no manager — run it straight away.
    void startRun(RunMode mode);
    // The run itself, without the queue around it: the direct path and the
    // task's start callback both land here. The paper is carried in rather
    // than read back off the screen, so a body that has to give up settles
    // the task it was started for and no other.
    void runRun(RunMode mode, const QString &paperId);
    void runTranslateAll(const QString &paperId);
    void runRetryFailed(const QString &paperId);
    // Drop every translation this paper has, so a run really re-asks the
    // model instead of skipping what is already there.
    void clearTranslations();
    // How many paragraphs `mode` is about to send. The run's steps.
    int plannedSteps(RunMode mode) const;
    // cancelPaper() without settling the task around it — what a task's
    // own body uses to clear the way before it starts.
    void cancelPaperJobs(const QString &paperId);
    // One paragraph landed, one step done.
    void noteRunProgress(const QString &paperId, bool failed);
    void finishRun(const QString &paperId, bool ok,
                   const QString &error = QString());
    // The run was stopped by the user — the Cancel button, the tab closing,
    // a paragraph edited out from under it. Nothing went wrong, so the row
    // ends Canceled rather than Failed with a message that reads like one.
    void cancelRun(const QString &paperId);

    void onPaperChanged();
    void rehydrateFromCache();
    void scheduleNext();
    void applyConcurrency();
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

    // One paragraph to translate, described so it survives the reader moving
    // to another paper: the row index is a view detail that goes stale the
    // moment the shared BlockListModel is refilled, so everything the result
    // needs — which paper, which block, and the cache key it was requested
    // under — is captured up front. `row` is re-derived on every paper switch
    // and is -1 while the job's paper is not the one on screen.
    struct Job {
        QString paperId;
        int blockId = -1;
        QString text;
        QString model;
        QString promptHash;
        QString lang;
        int row = -1;
        QString out;        // what has streamed back so far
    };

    // The cache to write a finished paragraph into: the live one when the
    // paper is on screen, otherwise a background instance for that paper,
    // created on demand and retired when its last job lands.
    TranslationCache *cacheFor(const QString &paperId);
    void retireBackgroundCache(const QString &paperId);
    // Point every job's `row` at the current model, or -1 for jobs belonging
    // to another paper. Called whenever the block list changes underneath.
    void rebindRows();
    int rowOfBlockId(int blockId) const;
    bool hasWorkFor(const QString &paperId) const;
    // Paragraphs of the current paper in any of `want`.
    int countRows(std::initializer_list<Block::TranslationStatus> want) const;
    QString currentPaperId() const;
    void startJob(Job job);
    // Build a job for `row` of the current paper, or a job with an empty
    // paperId when the row can't be translated.
    Job jobForRow(int row) const;
    // Pull the next job to run, sharing the slots out across papers instead
    // of draining the queue in arrival order. Returns false when nothing is
    // waiting.
    bool takeNextJob(Job &out);

    // A run in the queue's terms: which task covers this paper, how many
    // paragraphs it set out to do and how many have landed. Counted here
    // rather than off the rows, because a paper the reader has moved on
    // from has no rows to count.
    struct Run {
        QString taskId;
        int total  = 0;
        int done   = 0;
        int failed = 0;
    };

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<BlockListModel> m_model;
    QPointer<TaskManager> m_tasks;
    // paperId → the task covering its run. Only ever holds papers whose
    // run was submitted; a single right-click translation is not a run.
    QHash<QString, Run> m_runs;
    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    TranslationCache m_cache;
    // Papers being translated in the background, one cache each. Never holds
    // the paper on screen — that one is m_cache.
    QHash<QString, TranslationCache *> m_bgCaches;

    QQueue<Job> m_pending;
    QHash<LlmReply *, Job> m_inflightJobs;
    int m_inflight = 0;
    // Mirrors Settings::translationConcurrency; the cap is global, since what
    // it protects is the provider's rate limit, not any one paper.
    int m_maxInflight = 2;
    QString m_lastError;

    // Open selection cards, plus the ad-hoc requests feeding them
    // (paragraph cards are fed by m_inflightJobs instead).
    SnippetModel m_snippets;
    QHash<LlmReply *, int> m_snippetReplies;   // reply → snippet id
};
