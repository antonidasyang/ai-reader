#include "BatchAnalysisService.h"

#include "AnalysisJob.h"
#include "AnalysisListModel.h"
#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "Block.h"
#include "LlmClient.h"
#include "PaperSource.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "TaskManager.h"
#include "TaskTypes.h"

#include <QJsonArray>
#include <QTimer>

BatchAnalysisService::BatchAnalysisService(Settings *settings,
                                           AnalysisStore *store,
                                           ProjectProfileController *profile,
                                           PaperSource *source,
                                           AnalysisListModel *model,
                                           QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_profile(profile)
    , m_source(source)
    , m_model(model)
    , m_clients(settings, this)
{
    connect(m_source, &PaperSource::ready, this,
            &BatchAnalysisService::onSourceReady);
    connect(m_source, &PaperSource::failed, this,
            &BatchAnalysisService::onSourceFailed);
    connect(m_source, &PaperSource::progress, this,
            [this](const QString &itemId, const QString &what) {
                m_model->setRuntime(itemId, QStringLiteral("running"));
                setStatus(what);
            });
    // The counters move in seven different places; hanging the task's
    // progress off the signal they all emit is the only way not to forget
    // one of them.
    connect(this, &BatchAnalysisService::progressChanged, this, [this]() {
        if (m_tasks && !m_taskId.isEmpty())
            m_tasks->setProgress(m_taskId, m_done + m_failed + m_skipped,
                                 m_total);
    });
}

void BatchAnalysisService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A task stopped while it was still queued never calls its stop callback
    // -- there was nothing to stop -- so this is the only word the service
    // gets that the id it is holding is dead. Without it every later click
    // would add papers to a run that is never going to start.
    connect(m_tasks, &TaskManager::taskFinished, this,
            [this](const QString &id, bool, const QString &) {
                if (id.isEmpty() || id != m_taskId)
                    return;
                m_taskId.clear();
                m_taskStarted = false;
                m_pending.clear();
                m_pendingForce = false;
                // The manager drops a finished task's claims itself.
                m_claimed.clear();
                // The only way to get here still holding the id is the
                // viewer stopping a row that had not started, so the pane
                // says what happened instead of sitting on "Queued…".
                setStatus(tr("Cancelled."));
            });

    // A batch is worth resuming: it is the longest thing the app does, and
    // the papers that were already interpreted are skipped on the way back
    // through, so picking it up again costs only what is left.
    m_tasks->registerResumer(
        Tasks::Kind::BatchInterpret, [this](const QJsonObject &resume) {
            if (resume.value(QStringLiteral("projectId")).toString()
                != m_store->projectId())
                return false;      // the reader is somewhere else now
            QStringList itemIds;
            for (const QJsonValue &v :
                 resume.value(QStringLiteral("itemIds")).toArray())
                itemIds.append(v.toString());
            if (itemIds.isEmpty() || !canRun())
                return false;
            // A batch over this project is already open. startItems() would
            // fold these papers into it and leave m_taskId set from that
            // one, so answering from it would claim a run this never made.
            if (!m_taskId.isEmpty() || busy())
                return false;
            // Never forcing, whatever the interrupted run was doing: a
            // "Regenerate all" that got half way through has already been
            // paid for, and resuming must pick up what is left rather than
            // buy the finished papers a second time.
            startItems(itemIds, /*force=*/false);
            return !m_taskId.isEmpty();
        });
}

bool BatchAnalysisService::canRun() const
{
    return m_settings && m_settings->isConfigured()
           && m_store->canWrite();
}

void BatchAnalysisService::startPending()
{
    startItems(m_model->pendingItemIds(), false);
}

void BatchAnalysisService::startItems(const QStringList &itemIds, bool force)
{
    if (itemIds.isEmpty())
        return;
    if (!canRun()) {
        setStatus(m_store->canWrite()
                      ? tr("No model is configured for interpretations.")
                      : tr("Sign in and pick a project you can write to."));
        return;
    }

    if (!m_tasks) {
        startRun(itemIds, force);
        return;
    }
    if (!m_taskId.isEmpty()) {
        // Papers added while a run is under way join that run rather than
        // opening a second task -- there is one batch, and it grew.
        if (m_taskStarted) {
            startRun(itemIds, force);
            return;
        }
        // But a task that has been submitted and not yet started is not a
        // run under way. Enqueueing these papers now would interpret them
        // outside the concurrency cap, and when they drained finishIfIdle()
        // would close the task -- so the start callback would find nothing
        // to do and the papers the task was opened for would never be read
        // at all, under a row saying "Succeeded". They wait with the rest.
        for (const QString &id : itemIds) {
            if (!m_pending.contains(id))
                m_pending.append(id);
        }
        m_pendingForce = m_pendingForce || force;
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::BatchInterpret;
    req.title = tr("Interpret the library");
    req.projectId = m_store->projectId();
    // Not the default key: this is project work, not one paper's, and two
    // batches over one project would fight over the same rows.
    req.exclusiveKey =
        QStringLiteral("batch_interpret|") + req.projectId;
    req.steps = itemIds.size();
    QJsonArray items;
    for (const QString &id : itemIds)
        items.append(id);
    // No `force` in the payload, and the resumer above never forces: an
    // interrupted "Regenerate all" has already paid for the papers it got
    // through, and resuming must skip what is stored rather than buy it
    // twice.
    req.resume = QJsonObject{{QStringLiteral("projectId"), req.projectId},
                             {QStringLiteral("itemIds"), items}};

    const QString project = req.projectId;
    const QString id = m_tasks->submit(
        req,
        // A paper whose paragraphs are already cached comes back inside
        // request(), so a whole batch can run dry before submit() returns.
        // The hop through the event loop keeps the id ahead of the work --
        // and gives us somewhere to notice that the project has changed
        // under a task that waited.
        [this, project] {
            QTimer::singleShot(0, this, [this, project] {
                if (m_taskId.isEmpty())
                    return;       // cancelled while it waited
                if (project != m_store->projectId()) {
                    // The reader is somewhere else now; nothing went wrong.
                    cancelTask();
                    return;
                }
                // Everything that accumulated while this waited, not only
                // what the click that opened the task asked for.
                const QStringList items = m_pending;
                const bool force = m_pendingForce;
                m_pending.clear();
                m_taskStarted = true;
                if (items.isEmpty()) {
                    finishTask(true);
                    return;
                }
                startRun(items, force);
            });
        },
        [this] { cancel(); });
    if (id.isEmpty())
        return;                 // this project is already being interpreted
    m_taskId = id;              // the manager starts it when the queue lets it
    m_taskStarted = false;
    m_pending = itemIds;
    m_pendingForce = force;
    // Between the click and the manager admitting it there is nothing else
    // to see: say so rather than leave the pane looking untouched.
    setStatus(tr("Queued…"));
}

void BatchAnalysisService::startRun(const QStringList &itemIds, bool force)
{
    m_cancelled = false;
    m_force = force;
    if (!busy()) {
        m_total = m_done = m_failed = m_skipped = 0;
        m_errors.clear();
        m_failedItems.clear();
    }
    for (const QString &id : itemIds) {
        if (!m_queue.contains(id))
            m_queue.enqueue(id);
    }
    m_total += itemIds.size();
    for (const QString &id : itemIds)
        m_model->setRuntime(id, QStringLiteral("queued"));
    emit progressChanged();
    pump();
}

void BatchAnalysisService::retryFailed()
{
    const QStringList again = m_failedItems;
    m_failedItems.clear();
    for (const QString &id : again) {
        m_errors.remove(id);
        m_model->setRuntime(id, QString());
    }
    m_failed = qMax(0, m_failed - again.size());
    startItems(again, m_force);
}

void BatchAnalysisService::cancel()
{
    m_cancelled = true;
    for (const QString &id : m_queue)
        m_model->setRuntime(id, QString());
    m_queue.clear();
    m_pending.clear();
    m_source->cancel();
    m_sourceBusy = false;
    setStatus(tr("Cancelled."));
    emit progressChanged();
    // Interpretations already in flight still land, but the run is over as
    // far as the queue is concerned -- and finishIfIdle() will not speak for
    // it once cancelled, so the task is closed here. The reader stopped it,
    // so the row ends Canceled rather than Failed with nothing under it.
    cancelTask();
    finishIfIdle();
}

QString BatchAnalysisService::takeTaskId()
{
    if (!m_tasks || m_taskId.isEmpty())
        return {};
    // Cleared first: cancel() and the last paper's answer can both arrive.
    // The papers still spoken for are let go before the id goes, or nothing
    // would be left to release them against.
    releaseAllPapers();
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskStarted = false;
    // Whatever was still waiting for this task to be admitted has nothing
    // left to be admitted into.
    m_pending.clear();
    m_pendingForce = false;
    return id;
}

void BatchAnalysisService::finishTask(bool ok, const QString &error)
{
    const QString id = takeTaskId();
    if (!id.isEmpty())
        m_tasks->finish(id, ok, error);
}

void BatchAnalysisService::cancelTask()
{
    const QString id = takeTaskId();
    if (!id.isEmpty())
        m_tasks->markCanceled(id);
}

void BatchAnalysisService::claimPaper(const QString &paperId)
{
    if (!m_tasks || m_taskId.isEmpty() || paperId.isEmpty())
        return;
    if (m_claimed.contains(paperId))
        return;
    m_claimed.insert(paperId);
    // The batch is keyed by the project, an individual quick read by the
    // paper, so without this the reader could click Interpret on the very
    // paper the batch is working on: two paid calls, two writes of one
    // record, and whichever answered last would win.
    m_tasks->claim(m_taskId,
                   QStringLiteral("quick_interpret|") + paperId);
}

void BatchAnalysisService::releasePaper(const QString &paperId)
{
    if (!m_claimed.remove(paperId))
        return;
    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->releaseClaim(m_taskId,
                              QStringLiteral("quick_interpret|") + paperId);
}

void BatchAnalysisService::releaseAllPapers()
{
    const QSet<QString> held = m_claimed;
    for (const QString &paperId : held)
        releasePaper(paperId);
}

void BatchAnalysisService::pump()
{
    if (m_cancelled)
        return;
    const int limit = m_settings ? m_settings->analysisConcurrency() : 2;
    while (!m_sourceBusy && !m_queue.isEmpty() && m_running < limit) {
        // Which paper an item points at is only known once its fields have
        // been read, so the "already interpreted" skip happens where the
        // paperId is in hand -- in onSourceReady.
        const QString itemId = m_queue.dequeue();
        m_sourceBusy = true;
        m_model->setRuntime(itemId, QStringLiteral("running"));
        emit progressChanged();
        m_source->request(itemId);
        return;                 // one fetch at a time; the rest resumes later
    }
    finishIfIdle();
}

void BatchAnalysisService::onSourceReady(const QString &itemId,
                                         const QString &paperId,
                                         const QString &title,
                                         const QVector<Block> &blocks)
{
    m_sourceBusy = false;
    if (m_cancelled)
        return;

    if (!m_force) {
        const AnalysisRecord existing =
            m_store->paperAnalysis(paperId, Analysis::KindQuick);
        if (existing.valid && !existing.payload.isEmpty()) {
            ++m_skipped;
            m_model->setRuntime(itemId, QString());
            emit progressChanged();
            pump();
            return;
        }
    }

    // Rebuilt when the model configuration moved (a batch has several interpretations in flight at once).
    m_client = m_clients.client(m_running == 0);

    QuickAnalysisJob::Input in;
    in.paperId = paperId;
    in.title = title;
    in.blocks = blocks;
    in.lang = m_settings->targetLang();
    in.profileBlock = m_profile->promptBlock();
    in.contextChars =
        qBound(20000, int(m_settings->contextWindow() * 1.4), 400000);
    in.maxTokens = m_settings->analysisMaxTokens();

    ++m_running;
    // Held from here until the answer lands: while the batch is on this
    // paper, nothing else may interpret it.
    claimPaper(paperId);
    emit progressChanged();
    setStatus(tr("Interpreting %1…").arg(title));

    auto *job = QuickAnalysisJob::start(m_client, in, this);
    connect(job, &QuickAnalysisJob::succeeded, this,
            [this, itemId, paperId, title, job](const QJsonObject &digest) {
                --m_running;
                releasePaper(paperId);
                const bool insufficient =
                    digest.value(QStringLiteral("insufficient")).toBool();
                m_store->putPaperAnalysis(
                    paperId, Analysis::KindQuick, digest,
                    m_settings->model(),
                    Analysis::inputHash(job->contentHash(),
                                        AnalysisPrompts::promptVersion(),
                                        m_profile->hash(),
                                        m_settings->model()),
                    insufficient ? Analysis::StatusInsufficient
                                 : Analysis::StatusOk,
                    QString(), title);
                ++m_done;
                m_model->setRuntime(itemId, QString());
                emit progressChanged();
                pump();
            });
    connect(job, &QuickAnalysisJob::failed, this,
            [this, itemId, paperId](const QString &error) {
                --m_running;
                releasePaper(paperId);
                recordFailure(itemId, error);
                pump();
            });
    pump();
}

void BatchAnalysisService::onSourceFailed(const QString &itemId,
                                          const QString &reason)
{
    m_sourceBusy = false;
    if (m_cancelled)
        return;
    recordFailure(itemId, reason);
    pump();
}

void BatchAnalysisService::recordFailure(const QString &itemId,
                                         const QString &reason)
{
    ++m_failed;
    m_errors.insert(itemId, reason);
    if (!m_failedItems.contains(itemId))
        m_failedItems.append(itemId);
    m_model->setRuntime(itemId, QStringLiteral("failed"), reason);
    emit progressChanged();
}

void BatchAnalysisService::finishIfIdle()
{
    if (m_running > 0 || !m_queue.isEmpty() || m_sourceBusy)
        return;
    if (m_total == 0)
        return;
    setStatus(tr("%1 interpreted, %2 failed, %3 already done.")
                  .arg(m_done)
                  .arg(m_failed)
                  .arg(m_skipped));
    emit progressChanged();
    finishTask(m_failed == 0,
               m_failed == 0
                   ? QString()
                   : tr("%1 of %2 could not be interpreted.")
                         .arg(m_failed)
                         .arg(m_total));
    emit finished(m_done, m_failed, m_skipped);
}

QString BatchAnalysisService::errorFor(const QString &itemId) const
{
    return m_errors.value(itemId);
}

QStringList BatchAnalysisService::failedItems() const { return m_failedItems; }

void BatchAnalysisService::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    // The status line already names the paper being fetched or interpreted,
    // which is exactly what the task row wants under its title.
    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->setNote(m_taskId, s);
    emit statusChanged();
}
