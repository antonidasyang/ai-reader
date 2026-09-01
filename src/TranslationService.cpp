#include "TranslationService.h"
#include "Stall.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"
#include "TaskManager.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>

namespace {

QString resolveLanguageName(const QString &code)
{
    if (code.isEmpty() || code.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Simplified Chinese (zh-CN)");
    if (code.compare(QStringLiteral("zh-TW"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Traditional Chinese (zh-TW)");
    if (code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0
        || code.compare(QStringLiteral("en-US"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("English");
    if (code.compare(QStringLiteral("ja"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Japanese");
    return code;
}

// Whitespace-free copy, for matching a PDF selection against a block's
// text: the two agree on words but not on where the lines broke.
QString squeezed(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (!c.isSpace())
            out.append(c);
    }
    return out;
}

// Cache slot for ad-hoc selection translations. Real blocks use their
// own ids (positive), so a negative one can't collide; the source-text
// hash in the composite key keeps different selections apart.
constexpr int kSnippetBlockId = -1;

// Below this many non-space characters a selection is a term or half a
// sentence — too little to place inside a paragraph with confidence, so
// it gets translated on its own.
constexpr int kMinMatchChars = 12;

} // namespace

TranslationService::TranslationService(Settings *settings,
                                       PaperController *paper,
                                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_model(paper ? paper->blocks() : nullptr)
    , m_clients(settings, this)
{
    if (m_paper) {
        connect(m_paper, &PaperController::blocksChanged,
                this, &TranslationService::onPaperChanged);
    }
    if (m_settings) {
        applyConcurrency();
        connect(m_settings, &Settings::translationConcurrencyChanged,
                this, &TranslationService::applyConcurrency);
    }
    if (m_model) {
        // Splitting/merging/deleting paragraphs renumbers rows, which
        // would leave a pinned card mirroring somebody else's text. The
        // cards stay open with the text they already have; they just
        // stop following a row.
        connect(m_model, &BlockListModel::blocksMutated, this, [this] {
            m_snippets.detachBlockRows();
        });
    }
}

TranslationService::~TranslationService() = default;

void TranslationService::onPaperChanged()
{
    // PaperController re-emits blocksChanged for paragraph edits too, so
    // "the blocks changed" is not the same as "a different paper". Cards
    // belong to the paper they were opened on and close with it; an edit
    // to the current paper only costs them their row (see the
    // blocksMutated hookup in the constructor).
    const QString paperId = m_paper ? m_paper->paperId() : QString();
    const QString previous = m_cache.paperId();
    const bool switched = (paperId != previous);

    if (switched) {
        closeAllSnippets();
        // The paper being left may still have work in the air. It keeps its
        // own cache from here on so its results have somewhere to land.
        if (!previous.isEmpty() && hasWorkFor(previous))
            cacheFor(previous);
        // ...and the paper being opened must not be held by two caches at
        // once. Retiring flushes it, so m_cache loads a settled file.
        retireBackgroundCache(paperId);
    }

    // Switch the cache to the new paper and rehydrate any matching rows
    // straight into the BlockListModel — translations the user already
    // paid for show up instantly without another API call.
    m_cache.setPaperId(paperId);
    if (!m_cache.paperId().isEmpty())
        emit translationCacheReady(m_cache.paperId());
    rehydrateFromCache();
    // Jobs for this paper (started before the reader wandered off, or just
    // now coming back into view) get their row indices back — and jobs whose
    // paragraph was edited out from under them are dropped.
    rebindRows();
    emit progressChanged();
    emit busyChanged();
}

QString TranslationService::currentPaperId() const
{
    return m_paper ? m_paper->paperId() : QString();
}

bool TranslationService::busy() const
{
    const QString cur = currentPaperId();
    if (cur.isEmpty())
        return false;
    return hasWorkFor(cur);
}

int TranslationService::doneCount() const
{
    return countRows({Block::Translated});
}

int TranslationService::totalCount() const
{
    // Everything this paper's run is about: what is done, what failed, and
    // what is still coming. Paragraphs nobody has asked for are not in it.
    return countRows({Block::Translated, Block::Failed, Block::Queued,
                      Block::Translating});
}

int TranslationService::failedCount() const
{
    return countRows({Block::Failed});
}

int TranslationService::countRows(
    std::initializer_list<Block::TranslationStatus> want) const
{
    if (!m_model)
        return 0;
    int n = 0;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b)
            continue;
        for (const Block::TranslationStatus s : want) {
            if (b->translationStatus == s) {
                ++n;
                break;
            }
        }
    }
    return n;
}

int TranslationService::backgroundPapers() const
{
    const QString cur = currentPaperId();
    QSet<QString> papers;
    for (const Job &j : m_pending) {
        if (j.paperId != cur)
            papers.insert(j.paperId);
    }
    for (const Job &j : m_inflightJobs) {
        if (j.paperId != cur)
            papers.insert(j.paperId);
    }
    return papers.size();
}

bool TranslationService::hasWorkFor(const QString &paperId) const
{
    for (const Job &j : m_pending) {
        if (j.paperId == paperId)
            return true;
    }
    for (const Job &j : m_inflightJobs) {
        if (j.paperId == paperId)
            return true;
    }
    return false;
}

int TranslationService::rowOfBlockId(int blockId) const
{
    if (!m_model || blockId < 0)
        return -1;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (b && b->id == blockId)
            return row;
    }
    return -1;
}

void TranslationService::rebindRows()
{
    const QString cur = currentPaperId();
    // One pass over the model, so re-binding a long queue stays linear.
    QHash<int, int> rowOf;
    if (m_model) {
        for (int row = 0; row < m_model->blockCount(); ++row) {
            const Block *b = m_model->blockAt(row);
            if (b)
                rowOf.insert(b->id, row);
        }
    }

    // A job is stale when the paragraph it was built from is no longer there
    // — a split, a merge, a delete. Splitting renumbers everything after the
    // cut, so one stale job means the run no longer describes this paper.
    //
    // An EMPTY model is not evidence of that: PaperController clears it and
    // emits blocksChanged before it has even worked out which paper is being
    // opened, so a plain tab switch passes through here with nothing loaded.
    // Reading that as "the paragraphs are gone" is what used to kill the run
    // on every switch.
    const bool populated = m_model && m_model->blockCount() > 0;
    const auto stale = [&](const Job &j) {
        if (j.paperId != cur || !populated)
            return false;
        const int row = rowOf.value(j.blockId, -1);
        if (row < 0)
            return true;
        const Block *b = m_model->blockAt(row);
        return !b || b->text != j.text;
    };
    bool anyStale = false;
    for (const Job &j : m_pending)
        anyStale = anyStale || stale(j);
    for (const Job &j : m_inflightJobs)
        anyStale = anyStale || stale(j);
    if (anyStale)
        cancelPaper(cur);

    const auto bind = [&](Job &j) {
        j.row = (j.paperId == cur) ? rowOf.value(j.blockId, -1) : -1;
    };
    for (Job &j : m_pending)
        bind(j);
    for (auto it = m_inflightJobs.begin(); it != m_inflightJobs.end(); ++it)
        bind(it.value());

    // Paragraphs whose translation is still coming should say so, and show
    // whatever has streamed in so far.
    if (!m_model)
        return;
    for (const Job &j : m_inflightJobs) {
        if (j.row < 0)
            continue;
        m_model->setTranslationStatus(j.row, Block::Translating);
        if (!j.out.isEmpty())
            m_model->setTranslation(j.row, j.out);
    }
    for (const Job &j : m_pending) {
        if (j.row >= 0)
            m_model->setTranslationStatus(j.row, Block::Queued);
    }
}

TranslationCache *TranslationService::cacheFor(const QString &paperId)
{
    if (paperId.isEmpty())
        return nullptr;
    if (paperId == m_cache.paperId())
        return &m_cache;
    auto it = m_bgCaches.find(paperId);
    if (it == m_bgCaches.end()) {
        auto *c = new TranslationCache(this);
        c->setPaperId(paperId);
        it = m_bgCaches.insert(paperId, c);
    }
    return it.value();
}

void TranslationService::retireBackgroundCache(const QString &paperId)
{
    auto it = m_bgCaches.find(paperId);
    if (it == m_bgCaches.end())
        return;
    // Switching to an empty paper id flushes the debounced write before the
    // instance goes away; the destructor alone would drop it.
    it.value()->setPaperId(QString());
    it.value()->deleteLater();
    m_bgCaches.erase(it);
}

void TranslationService::refreshFromCache()
{
    if (busy())
        return;
    rehydrateFromCache();
}

void TranslationService::rehydrateFromCache()
{
    Stall::Mark mark("putting this paper's translations back");
    if (!m_settings || !m_model) return;
    if (m_cache.paperId().isEmpty()) return;

    const QString model      = m_settings->translationModelInUse();
    const QString promptHash = TranslationCache::sha(systemPrompt());
    const QString lang       = m_settings->targetLang();

    int hits = 0;
    // Collected and applied in one go: row by row this was three
    // notifications per paragraph, and a fully translated paper is hundreds
    // of paragraphs every time it is opened.
    QVector<BlockListModel::CachedTranslation> found;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (b->translationStatus == Block::Translated) {
            ++hits;     // already on screen — count it, never overwrite it
            continue;
        }
        const QString cached =
            m_cache.lookup(b->id, b->text, model, promptHash, lang);
        if (cached.isEmpty()) continue;
        // The origin names the member it was adopted from, and is empty for
        // our own work — the same rule setTranslationOrigin followed.
        found.append({row, cached,
                      m_cache.originOf(b->id, b->text, model, promptHash, lang)});
        ++hits;
    }
    m_model->applyCachedTranslations(found);
    if (hits > 0)
        emit progressChanged();
}

void TranslationService::cancel()
{
    // The button cancels the paper the reader is looking at. Another paper's
    // run is that paper's business; its tab closing is what stops it.
    cancelPaper(currentPaperId());
}

void TranslationService::cancelPaper(const QString &paperId)
{
    cancelPaperJobs(paperId);
    // Whatever stopped the run — the tab closing, the Cancel button, a
    // paragraph edited out from under it — the task is over with it. None
    // of those is a failure: the user stopped the work, so the row ends
    // Canceled and carries no error.
    cancelRun(paperId);
}

void TranslationService::cancelPaperJobs(const QString &paperId)
{
    if (paperId.isEmpty() || !hasWorkFor(paperId))
        return;

    // Drop this paper's queued jobs, putting any row they hold back to
    // untranslated so the user can start over.
    QQueue<Job> keep;
    while (!m_pending.isEmpty()) {
        const Job j = m_pending.dequeue();
        if (j.paperId != paperId) {
            keep.enqueue(j);
            continue;
        }
        if (m_model && j.row >= 0) {
            const Block *b = m_model->blockAt(j.row);
            if (b && b->translationStatus == Block::Queued)
                m_model->setTranslationStatus(j.row, Block::NotTranslated);
        }
    }
    m_pending = keep;

    // Stop what is already in flight. Letting those finish "naturally" is
    // what made Cancel look dead: with two requests running, paragraphs kept
    // streaming in and the button stayed on Cancel until they were done.
    //
    // Disconnect before aborting — an aborted reply raises errorOccurred, and
    // that handler would mark the row Failed and count it as a failure, which
    // is not what the user asked for.
    const QList<LlmReply *> inflight = m_inflightJobs.keys();
    for (LlmReply *reply : inflight) {
        const Job j = m_inflightJobs.value(reply);
        if (j.paperId != paperId)
            continue;
        m_inflightJobs.remove(reply);
        --m_inflight;
        if (m_model && j.row >= 0) {
            // Drop a half-streamed paragraph: it is a truncated sentence, it
            // was never cached, and leaving it on screen under a "translated"
            // badge would be a lie. The row goes back to untranslated so
            // Translate picks it up again.
            const Block *b = m_model->blockAt(j.row);
            if (b && b->translationStatus == Block::Translating) {
                m_model->setTranslation(j.row, QString());
                m_model->setTranslationStatus(j.row, Block::NotTranslated);
            }
            // A pinned card mirroring that row has to hear about it too;
            // syncBlockRow would read the reset row as "still translating".
            for (const int id : m_snippets.idsForBlockRow(j.row))
                m_snippets.setStatus(id, QStringLiteral("failed"),
                                     tr("Cancelled."));
        }
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }

    if (paperId != currentPaperId())
        retireBackgroundCache(paperId);

    emit progressChanged();
    emit busyChanged();
    // A cancelled paper frees a slot; whatever else is waiting can start.
    scheduleNext();
}

void TranslationService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A run the app closed on can be started again from nothing: the paper
    // and what it was asked to do is all of it. The paper has to be the one
    // on screen, since that is the only block list a run can work through.
    m_tasks->registerResumer(Tasks::Kind::Translate,
                             [this](const QJsonObject &resume) {
        if (!m_paper)
            return false;
        if (m_paper->paperId()
            != resume.value(QStringLiteral("paperId")).toString())
            return false;
        const QString mode = resume.value(QStringLiteral("mode")).toString();
        const RunMode runMode = (mode == QLatin1String("failed"))
                                    ? RunMode::Failed
                                    : RunMode::All;
        // A resumer that cannot start says no and leaves the screen alone.
        // On a launch where the paragraphs are not loaded yet, the model is
        // no longer configured, or there is nothing left to translate,
        // starting anyway would put a failure in the pane for a run the
        // user never saw begin.
        if (!m_model || m_model->blockCount() == 0)
            return false;
        if (!m_settings || !m_settings->isConfigured())
            return false;
        if (plannedSteps(runMode) == 0)
            return false;
        // A run that was interrupted picks up where it stopped: what is
        // already translated stays translated, whichever way it started.
        startRun(runMode);
        return true;
    });
}

void TranslationService::startRun(RunMode mode)
{
    if (!m_model)
        return;
    const QString paperId = currentPaperId();
    // With no manager -- and with no paper, where there is nothing to
    // submit but the body still has its say about why -- this is the old
    // code path, unchanged.
    if (!m_tasks || paperId.isEmpty()) {
        runRun(mode, paperId);
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::Translate;
    switch (mode) {
    case RunMode::All:         req.title = tr("Translate");             break;
    case RunMode::Retranslate: req.title = tr("Retranslate");           break;
    case RunMode::Failed:      req.title = tr("Retry failed paragraphs"); break;
    }
    req.paperId = paperId;
    req.paperTitle = m_paper ? m_paper->fileName() : QString();
    // The paragraphs are this task's steps. One run, one task: the cap on
    // how many paragraphs are in the air at once is the service's own and
    // has nothing to do with how many tasks may run.
    req.steps = plannedSteps(mode);
    // The default exclusion key (kind|paperId) is exactly what is wanted
    // here: two runs over one paper would ask for the same paragraphs
    // twice and race each other into the same cache.
    req.resume = QJsonObject{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("path"),
         m_paper ? m_paper->pdfSource().toLocalFile() : QString()},
        {QStringLiteral("mode"), mode == RunMode::Failed
                                     ? QStringLiteral("failed")
                                     : QStringLiteral("all")},
    };

    const QString id = m_tasks->submit(req,
        [this, mode, paperId] {
            // submit() only hands the id back when it returns, and it may
            // call this from inside itself -- one turn of the event loop
            // and the run has its id on record to report against.
            QTimer::singleShot(0, this, [this, mode, paperId] {
                // Cancelled in the turn between being admitted and
                // starting: the stop callback has already taken the run
                // off the books and the manager owns the outcome, so
                // there is nothing left here to do or to report.
                if (!m_runs.contains(paperId))
                    return;
                if (currentPaperId() != paperId) {
                    // Admitted after the reader moved on. A run works
                    // through the paragraphs on screen, and those are
                    // another paper's now — the reader moved on, nothing
                    // went wrong.
                    cancelRun(paperId);
                    return;
                }
                runRun(mode, paperId);
            });
        },
        [this, paperId] {
            // The manager is stopping us; it owns the outcome from here,
            // so drop the record rather than finishing the task ourselves.
            m_runs.remove(paperId);
            cancelPaperJobs(paperId);
        });
    if (id.isEmpty())
        return;             // this paper is already being translated

    Run &run = m_runs[paperId];
    run.taskId = id;
    run.total  = req.steps;
    run.done   = 0;
    run.failed = 0;
}

void TranslationService::runRun(RunMode mode, const QString &paperId)
{
    switch (mode) {
    case RunMode::Retranslate:
        clearTranslations();
        Q_FALLTHROUGH();
    case RunMode::All:
        runTranslateAll(paperId);
        break;
    case RunMode::Failed:
        runRetryFailed(paperId);
        break;
    }
}

int TranslationService::plannedSteps(RunMode mode) const
{
    if (!m_model)
        return 0;
    int n = 0;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b)
            continue;
        if (mode == RunMode::Failed) {
            if (b->translationStatus == Block::Failed)
                ++n;
            continue;
        }
        // Pass-through paragraphs are resolved on the spot and never go to
        // the model, so they are not steps.
        if (shouldSkip(b->text))
            continue;
        // Retranslate throws everything away first, so everything counts.
        if (mode == RunMode::All
            && b->translationStatus == Block::Translated)
            continue;
        ++n;
    }
    return n;
}

void TranslationService::noteRunProgress(const QString &paperId, bool failed)
{
    auto it = m_runs.find(paperId);
    if (it == m_runs.end())
        return;
    if (failed)
        ++it->failed;
    // A run can outgrow its estimate — a paragraph translated by hand from
    // the right-click menu lands in the same queue — so the bar is never
    // pushed past its end.
    ++it->done;
    if (it->total > 0 && it->done > it->total)
        it->done = it->total;
    if (m_tasks)
        m_tasks->setProgress(it->taskId, it->done, it->total);
}

void TranslationService::finishRun(const QString &paperId, bool ok,
                                   const QString &error)
{
    auto it = m_runs.find(paperId);
    if (it == m_runs.end())
        return;
    const Run run = *it;
    m_runs.erase(it);
    if (!m_tasks)
        return;
    // A run that got through every paragraph but lost some of them is not
    // a success: the pane still shows a Retry button.
    if (ok && run.failed > 0) {
        m_tasks->finish(run.taskId, false,
                        tr("%n paragraph(s) could not be translated",
                           "", run.failed));
        return;
    }
    m_tasks->finish(run.taskId, ok, error);
}

void TranslationService::cancelRun(const QString &paperId)
{
    auto it = m_runs.find(paperId);
    if (it == m_runs.end())
        return;
    const Run run = *it;
    m_runs.erase(it);
    if (m_tasks)
        m_tasks->markCanceled(run.taskId);
}

void TranslationService::translateAll()
{
    startRun(RunMode::All);
}

void TranslationService::runTranslateAll(const QString &paperId)
{
    // The task was admitted before any of this was looked at, so every way
    // out of here has to settle it: a body that returns quietly leaves its
    // row at 0/N in the viewer for the rest of the session.
    if (!m_settings || !m_model) {
        finishRun(paperId, false,
                  tr("The paper's paragraphs are no longer loaded."));
        return;
    }

    if (!m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        finishRun(paperId, false, m_lastError);
        return;
    }

    refreshClient();

    if (paperId.isEmpty() || paperId != currentPaperId()) {
        // The paragraphs a run works through are the ones on screen, and
        // they are not this run's any more. Nothing went wrong; the reader
        // moved on. (With no manager there is no run on record here and
        // this is the old silent return.)
        cancelRun(paperId);
        return;
    }
    // Starting over on this paper: drop what it had queued, leave other
    // papers' runs alone. The task around this run stays — this is its own
    // body clearing the way.
    cancelPaperJobs(paperId);

    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (shouldSkip(b->text)) {
            m_model->setTranslationStatus(row, Block::Skipped);
            m_model->setTranslation(row, b->text);
            continue;
        }
        if (b->translationStatus == Block::Translated)
            continue;

        Job job = jobForRow(row);
        if (job.paperId.isEmpty())
            continue;
        m_pending.enqueue(job);
        m_model->setTranslationStatus(row, Block::Queued);
    }

    setLastError({});
    emit progressChanged();
    if (!hasWorkFor(paperId)) {
        // Nothing needed translating — the run is over before it began.
        finishRun(paperId, true);
        return;
    }

    emit busyChanged();
    scheduleNext();
}

void TranslationService::retranslateAll()
{
    startRun(RunMode::Retranslate);
}

void TranslationService::clearTranslations()
{
    if (!m_model) return;
    // Clear first, or the run would skip every one of them as already
    // translated. Skipped rows are cleared too — the run marks them
    // Skipped again on the way past, and a stale pass-through would otherwise
    // survive a change of language.
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b || b->translationStatus == Block::NotTranslated)
            continue;
        m_model->setTranslation(row, QString());
        m_model->setTranslationStatus(row, Block::NotTranslated);
    }
}

int TranslationService::translatedParagraphs() const
{
    if (!m_model) return 0;
    int n = 0;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (b && b->translationStatus == Block::Translated)
            ++n;
    }
    return n;
}

int TranslationService::untranslatedParagraphs() const
{
    if (!m_model) return 0;
    int n = 0;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b)
            continue;
        // Skipped is a decision, not a gap: those paragraphs are pass-through
        // math or numbers and asking for them again changes nothing.
        if (b->translationStatus == Block::NotTranslated
            || b->translationStatus == Block::Failed)
            ++n;
    }
    return n;
}

TranslationService::Job TranslationService::jobForRow(int row) const
{
    Job job;
    if (!m_model || !m_settings)
        return job;
    const Block *b = m_model->blockAt(row);
    const QString paperId = currentPaperId();
    if (!b || paperId.isEmpty())
        return job;
    job.paperId = paperId;
    job.blockId = b->id;
    job.text = b->text;
    job.model = m_settings->translationModelInUse();
    job.promptHash = TranslationCache::sha(systemPrompt());
    job.lang = m_settings->targetLang();
    job.row = row;
    return job;
}

void TranslationService::retryFailed()
{
    startRun(RunMode::Failed);
}

void TranslationService::runRetryFailed(const QString &paperId)
{
    // Same rule as the full run: the task is admitted by the time this
    // runs, so no way out of here may leave it unfinished.
    if (!m_model) {
        finishRun(paperId, false,
                  tr("The paper's paragraphs are no longer loaded."));
        return;
    }
    if (!m_settings || !m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        finishRun(paperId, false, m_lastError);
        return;
    }
    // The paragraphs being retried most likely failed on a configuration
    // that has since been fixed — resolve the client the same way every
    // other entry point does, rather than reusing whatever it was.
    refreshClient();
    if (paperId.isEmpty() || paperId != currentPaperId()) {
        cancelRun(paperId);
        return;
    }
    bool added = false;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (b->translationStatus == Block::Failed) {
            Job job = jobForRow(row);
            if (job.paperId.isEmpty())
                continue;
            m_pending.enqueue(job);
            m_model->setTranslationStatus(row, Block::Queued);
            added = true;
        }
    }
    if (!added) {
        finishRun(paperId, true);
        return;
    }
    emit progressChanged();
    emit busyChanged();
    scheduleNext();
}

void TranslationService::translateBlock(int row)
{
    if (!m_settings || !m_model) return;
    if (row < 0 || row >= m_model->blockCount()) return;
    const Block *b = m_model->blockAt(row);
    if (!b) return;

    if (!m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }
    refreshClient();

    if (shouldSkip(b->text)) {
        m_model->setTranslationStatus(row, Block::Skipped);
        m_model->setTranslation(row, b->text);
        return;
    }
    if (b->translationStatus == Block::Translating
        || b->translationStatus == Block::Queued) {
        return;  // already in flight
    }

    Job job = jobForRow(row);
    if (job.paperId.isEmpty()) return;
    for (const Job &q : std::as_const(m_pending)) {
        if (q.paperId == job.paperId && q.blockId == job.blockId)
            return;   // already waiting its turn
    }

    m_pending.enqueue(job);
    m_model->setTranslationStatus(row, Block::Queued);
    setLastError({});
    emit progressChanged();
    if (m_inflight == 0)
        emit busyChanged();
    scheduleNext();
}

void TranslationService::refreshClient()
{
    if (!m_settings) return;
    // Always the current configuration. Paragraphs already in the air
    // finish on the client that sent them — the cache keeps it alive
    // until its last reply is gone.
    m_client = m_clients.client();
}

void TranslationService::scheduleNext()
{
    Job job;
    while (m_inflight < m_maxInflight && takeNextJob(job))
        startJob(std::move(job));
}

bool TranslationService::takeNextJob(Job &out)
{
    if (m_pending.isEmpty())
        return false;

    // Round-robin across papers rather than strict arrival order. A single
    // queue meant a second paper had to wait out the first one's entire
    // backlog before its first paragraph went anywhere — "各翻译各的" only
    // holds if the slots are shared.
    QHash<QString, int> running;
    for (const Job &j : m_inflightJobs)
        ++running[j.paperId];

    int best = -1;
    int bestRunning = 0;
    for (int i = 0; i < m_pending.size(); ++i) {
        const int n = running.value(m_pending.at(i).paperId, 0);
        if (best < 0 || n < bestRunning) {
            best = i;
            bestRunning = n;
            if (n == 0)
                break;      // nothing in the air for that paper — take it
        }
    }
    out = m_pending.at(best);
    m_pending.removeAt(best);
    return true;
}

void TranslationService::applyConcurrency()
{
    const int want = m_settings ? m_settings->translationConcurrency() : 2;
    if (want == m_maxInflight)
        return;
    m_maxInflight = want;
    // Raising it should take effect on the run already going, not on the
    // next one.
    scheduleNext();
}

bool TranslationService::shouldSkip(const QString &text) const
{
    if (text.trimmed().isEmpty()) return true;
    int letters = 0;
    for (QChar c : text) {
        if (c.isLetter()) ++letters;
    }
    // If <20% letters, treat as math/numeric/formula and pass through.
    return letters * 5 < text.size();
}

QString TranslationService::defaultSystemPrompt() const
{
    return QStringLiteral(
        "You are a precise academic translator. Translate the user's text into {{lang}}.\n"
        "\n"
        "Rules:\n"
        "- Preserve all citations like [12], [13, 14], (Smith et al., 2020) unchanged.\n"
        "- Preserve inline math notation ($x$, $$y$$, \\begin{...}) unchanged.\n"
        "- Preserve code, URLs, file paths, and proper nouns unchanged.\n"
        "- Output ONLY the translation. No quotes around the result, no notes, "
        "no source text, no \"Translation:\" prefix.");
}

QString TranslationService::systemPrompt() const
{
    const QString lang = resolveLanguageName(m_settings ? m_settings->targetLang()
                                                       : QString());
    QString tmpl;
    if (m_settings && !m_settings->translationPrompt().isEmpty())
        tmpl = m_settings->translationPrompt();
    else
        tmpl = defaultSystemPrompt();
    return tmpl.replace(QStringLiteral("{{lang}}"), lang);
}

void TranslationService::startJob(Job job)
{
    if (!m_client || job.paperId.isEmpty()) {
        // The job is already out of the queue, so returning quietly is what
        // leaves a run one paragraph short of ever ending — the viewer's row
        // sits at 0/N and nothing will ever move it. A single paragraph
        // asked for by hand has no run behind it and still just goes away.
        if (!m_runs.contains(job.paperId))
            return;
        // No client means the model configuration changed between the run
        // being submitted and its turn coming up, and the paragraphs still
        // queued behind this one would each be dropped the same way — so
        // the run ends here, with the reason on it.
        const QString msg = tr("No model is configured. Open Settings to add "
                               "a model and API key.");
        if (m_model && job.row >= 0) {
            const Block *b = m_model->blockAt(job.row);
            if (b && b->translationStatus == Block::Queued)
                m_model->setTranslationStatus(job.row, Block::NotTranslated);
        }
        if (job.paperId == currentPaperId())
            setLastError(msg);
        cancelPaperJobs(job.paperId);
        finishRun(job.paperId, false, msg);
        emit progressChanged();
        emit busyChanged();
        return;
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), job.text});
    req.temperature = m_settings ? m_settings->temperature() : 0.2;
    req.stream = true;
    req.maxTokens = m_settings ? m_settings->maxTokens() : 4096;

    LlmReply *reply = m_client->send(req);
    ++m_inflight;
    if (m_model && job.row >= 0) {
        m_model->setTranslationStatus(job.row, Block::Translating);
        // Clear any previous translation text before streaming the new one.
        m_model->setTranslation(job.row, QString());
    }
    m_inflightJobs.insert(reply, job);
    emit progressChanged();   // the row just became Translating
    if (m_tasks) {
        // The run says which paragraph it is on, whether or not that
        // paper's rows are the ones on screen.
        const auto run = m_runs.constFind(job.paperId);
        if (run != m_runs.constEnd())
            m_tasks->setNote(run->taskId,
                             tr("Paragraph %1").arg(job.blockId + 1));
    }

    connect(reply, &LlmReply::chunkReceived, this,
            [this, reply](const QString &chunk) {
        auto it = m_inflightJobs.find(reply);
        if (it == m_inflightJobs.end())
            return;
        // The job's own buffer is the authority: its paper may not be the
        // one on screen, in which case there is no row to accumulate into.
        it->out += chunk;
        if (m_model && it->row >= 0) {
            m_model->appendTranslationChunk(it->row, chunk);
            // Any card pinned to this paragraph streams along with it.
            syncBlockRow(it->row);
        }
    });
    connect(reply, &LlmReply::finished, this, [this, reply]() {
        if (!m_inflightJobs.contains(reply))
            return;
        const Job j = m_inflightJobs.take(reply);
        --m_inflight;
        const bool onScreen = (m_model && j.row >= 0);

        const bool empty = j.out.trimmed().isEmpty();
        noteRunProgress(j.paperId, empty);
        if (empty) {
            // A stream can end having delivered nothing at all. Calling that
            // Translated hides the failure behind a blank line and, worse,
            // caches the blank — so reopening the paper "restores" it and the
            // block never gets retried.
            const QString msg = tr("The model returned an empty translation.");
            if (onScreen)
                m_model->setTranslationStatus(j.row, Block::Failed, msg);
            if (j.paperId == currentPaperId())
                setLastError(msg);
        } else {
            if (onScreen)
                m_model->setTranslationStatus(j.row, Block::Translated);
            // Persist to disk so reopening this paper restores the
            // translation without another API round-trip. A paper the reader
            // has moved on from writes to its own cache instead.
            if (TranslationCache *c = cacheFor(j.paperId))
                c->store(j.blockId, j.text, j.model, j.promptHash, j.lang,
                         j.out);
        }
        if (onScreen)
            syncBlockRow(j.row);
        if (!hasWorkFor(j.paperId) && j.paperId != currentPaperId())
            retireBackgroundCache(j.paperId);

        emit progressChanged();
        reply->deleteLater();
        scheduleNext();
        if (!busy())
            emit busyChanged();
        // The run is over when its last paragraph has landed — not when
        // the queue was drained, since a slot freed here may have just
        // taken the next one.
        if (!hasWorkFor(j.paperId))
            finishRun(j.paperId, true);
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply](const QString &message) {
        if (!m_inflightJobs.contains(reply))
            return;
        const Job j = m_inflightJobs.take(reply);
        --m_inflight;
        noteRunProgress(j.paperId, true);
        if (m_model && j.row >= 0) {
            m_model->setTranslationStatus(j.row, Block::Failed, message);
            syncBlockRow(j.row);
        }
        if (j.paperId == currentPaperId())
            setLastError(message);
        if (!hasWorkFor(j.paperId) && j.paperId != currentPaperId())
            retireBackgroundCache(j.paperId);

        emit progressChanged();
        reply->deleteLater();
        scheduleNext();
        if (!busy())
            emit busyChanged();
        if (!hasWorkFor(j.paperId))
            finishRun(j.paperId, true);
    });
}

// ── Selection translation ──────────────────────────────────────────────

int TranslationService::findBlockRow(const QString &text, int page) const
{
    if (!m_model) return -1;
    const QString needle = squeezed(text);
    if (needle.size() < kMinMatchChars) return -1;

    int fallback = -1;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (!squeezed(b->text).contains(needle)) continue;
        // A block's page is where it starts, so prefer the page the
        // selection began on and fall back to the first match (the
        // selection may have run onto the next page).
        if (page < 0 || b->page == page) return row;
        if (fallback < 0) fallback = row;
    }
    return fallback;
}

void TranslationService::syncBlockRow(int row)
{
    if (!m_model || !m_snippets.hasBlockRow(row)) return;
    const Block *b = m_model->blockAt(row);
    if (!b) return;

    QString status;
    switch (b->translationStatus) {
    case Block::Translated:
    case Block::Skipped:
        status = QStringLiteral("done");
        break;
    case Block::Failed:
        status = QStringLiteral("failed");
        break;
    default:
        status = QStringLiteral("translating");
        break;
    }
    const QVector<int> ids = m_snippets.idsForBlockRow(row);
    for (const int id : ids) {
        m_snippets.setText(id, b->translation);
        m_snippets.setStatus(id, status, b->translationError);
    }
}

void TranslationService::closeSnippet(int id)
{
    for (auto it = m_snippetReplies.begin(); it != m_snippetReplies.end(); ) {
        if (it.value() != id) { ++it; continue; }
        if (it.key()) {
            // Same rule as cancel(): disconnect first, or the abort's
            // errorOccurred lands on a card that no longer exists.
            it.key()->disconnect(this);
            it.key()->abort();
            it.key()->deleteLater();
        }
        it = m_snippetReplies.erase(it);
    }
    m_snippets.remove(id);
}

void TranslationService::closeAllSnippets()
{
    for (auto it = m_snippetReplies.begin(); it != m_snippetReplies.end(); ++it) {
        if (it.key()) {
            it.key()->disconnect(this);
            it.key()->abort();
            it.key()->deleteLater();
        }
    }
    m_snippetReplies.clear();
    m_snippets.clear();
}

int TranslationService::translateSelection(const QString &text, int page)
{
    const QString src = text.trimmed();
    if (src.isEmpty() || !m_settings)
        return -1;

    SnippetModel::Snippet s;
    s.source = src;
    s.status = QStringLiteral("translating");

    // Paragraph path — hand the work to the normal per-block pipeline so
    // the result is cached and the right pane fills in as well. Work the
    // model already did is shown whether or not an LLM is configured.
    const int row = findBlockRow(src, page);
    const Block *b = (row >= 0 && m_model) ? m_model->blockAt(row) : nullptr;
    if (b) {
        s.paragraph = true;
        s.blockRow = row;
        s.text = b->translation;
        switch (b->translationStatus) {
        case Block::Translated:
        case Block::Skipped:
            s.status = QStringLiteral("done");
            return m_snippets.add(s);
        case Block::Translating:
        case Block::Queued:
            return m_snippets.add(s);   // in flight; syncBlockRow feeds it
        default:
            break;
        }
        if (!m_settings->isConfigured()) {
            s.status = QStringLiteral("failed");
            s.error = tr("LLM is not configured. Open Settings to add a "
                         "model and API key.");
            return m_snippets.add(s);
        }

        const int id = m_snippets.add(s);
        translateBlock(row);
        // translateBlock can resolve the row on the spot (formula-only
        // text is passed through as Skipped) or refuse it — so read the
        // row back rather than leaving the card spinning.
        syncBlockRow(row);
        const Block *after = m_model->blockAt(row);
        if (after && after->translationStatus == Block::NotTranslated) {
            m_snippets.setStatus(id, QStringLiteral("failed"),
                                 m_lastError.isEmpty()
                                     ? tr("Could not translate this paragraph.")
                                     : m_lastError);
        }
        return id;
    }

    // Ad-hoc path — the selection spans paragraphs, is too short to
    // place, or the paper hasn't been segmented yet.
    const int id = m_snippets.add(s);
    translateSelectionAdHoc(id, src);
    return id;
}

void TranslationService::translateSelectionAdHoc(int snippetId, const QString &text)
{
    // The cache key carries the model and language, so a lookup without
    // a configured model can only miss — no point trying first.
    if (!m_settings->isConfigured()) {
        m_snippets.setStatus(snippetId, QStringLiteral("failed"),
                             tr("LLM is not configured. Open Settings to add "
                                "a model and API key."));
        return;
    }

    const QString promptHash = TranslationCache::sha(systemPrompt());
    const QString model = m_settings->translationModelInUse();
    const QString lang  = m_settings->targetLang();

    const QString cached =
        m_cache.lookup(kSnippetBlockId, text, model, promptHash, lang);
    if (!cached.isEmpty()) {
        m_snippets.setText(snippetId, cached);
        m_snippets.setStatus(snippetId, QStringLiteral("done"));
        return;
    }

    refreshClient();
    if (!m_client) {
        m_snippets.setStatus(snippetId, QStringLiteral("failed"),
                             tr("LLM is not configured. Open Settings to add "
                                "a model and API key."));
        return;
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), text});
    req.temperature = m_settings->temperature();
    req.stream = true;
    req.maxTokens = m_settings->maxTokens();

    LlmReply *reply = m_client->send(req);
    m_snippetReplies.insert(reply, snippetId);

    connect(reply, &LlmReply::chunkReceived, this,
            [this, reply](const QString &chunk) {
        const int id = m_snippetReplies.value(reply, -1);
        if (id >= 0)
            m_snippets.appendText(id, chunk);
    });
    connect(reply, &LlmReply::finished, this, [this, reply, text,
                                               model, promptHash, lang]() {
        const int id = m_snippetReplies.take(reply);
        reply->deleteLater();
        const SnippetModel::Snippet *s = id >= 0 ? m_snippets.byId(id) : nullptr;
        if (!s) return;   // card was closed while the reply was in flight
        if (s->text.trimmed().isEmpty()) {
            m_snippets.setStatus(id, QStringLiteral("failed"),
                                 tr("The model returned an empty translation."));
            return;
        }
        // Worth caching: people re-select the same sentence.
        if (!m_cache.paperId().isEmpty())
            m_cache.store(kSnippetBlockId, text, model, promptHash, lang, s->text);
        m_snippets.setStatus(id, QStringLiteral("done"));
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply](const QString &message) {
        const int id = m_snippetReplies.take(reply);
        reply->deleteLater();
        if (id >= 0)
            m_snippets.setStatus(id, QStringLiteral("failed"), message);
    });
}

void TranslationService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
