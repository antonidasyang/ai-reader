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
            beginBatch(itemIds, /*force=*/false,
                       resume.value(QStringLiteral("deep")).toBool());
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
    beginBatch(itemIds, force, /*deep=*/false);
}

void BatchAnalysisService::startDeepItems(const QStringList &itemIds,
                                          bool force)
{
    beginBatch(itemIds, force, /*deep=*/true);
}

int BatchAnalysisService::deepPartsTotal() const
{
    return int(Analysis::deepModules().size());
}

void BatchAnalysisService::beginBatch(const QStringList &itemIds, bool force,
                                      bool deep)
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
        if (!busy())
            m_deep = deep;
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

    m_deep = deep;
    Tasks::Request req;
    req.kind = Tasks::Kind::BatchInterpret;
    req.title = deep ? tr("Close-read the library")
                     : tr("Interpret the library");
    req.projectId = m_store->projectId();
    // Not the default key: this is project work, not one paper's, and two
    // batches over one project would fight over the same rows.
    req.exclusiveKey =
        QStringLiteral("batch_interpret|") + req.projectId;
    req.steps = int(itemIds.size());
    QJsonArray items;
    for (const QString &id : itemIds)
        items.append(id);
    // No `force` in the payload, and the resumer above never forces: an
    // interrupted "Regenerate all" has already paid for the papers it got
    // through, and resuming must skip what is stored rather than buy it
    // twice.
    req.resume = QJsonObject{{QStringLiteral("projectId"), req.projectId},
                             {QStringLiteral("itemIds"), items},
                             {QStringLiteral("deep"), deep}};

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

QStringList BatchAnalysisService::claimKeys(const QString &paperId) const
{
    QStringList keys{QStringLiteral("quick_interpret|") + paperId};
    if (m_deep)
        keys.append(QStringLiteral("deep_interpret|") + paperId);
    return keys;
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
    for (const QString &key : claimKeys(paperId))
        m_tasks->claim(m_taskId, key);
}

void BatchAnalysisService::releasePaper(const QString &paperId)
{
    if (!m_claimed.remove(paperId))
        return;
    if (!m_tasks || m_taskId.isEmpty())
        return;
    for (const QString &key : claimKeys(paperId))
        m_tasks->releaseClaim(m_taskId, key);
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
    // A close reading is nine calls, already paced against the cap among
    // themselves; taking two papers at once would multiply that by two.
    const int paperLimit = m_deep ? 1 : limit;
    while (!m_sourceBusy && !m_queue.isEmpty() && m_running < paperLimit) {
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

    const AnalysisRecord quick =
        m_store->paperAnalysis(paperId, Analysis::KindQuick);
    const bool hasQuick = quick.valid && !quick.payload.isEmpty();

    if (!m_force) {
        bool alreadyDone = false;
        if (m_deep) {
            const AnalysisRecord deep =
                m_store->paperAnalysis(paperId, Analysis::KindDeep);
            alreadyDone =
                deep.valid
                && deep.payload.value(QStringLiteral("modules")).toObject().size()
                       >= deepPartsTotal();
        } else {
            alreadyDone = hasQuick;
        }
        if (alreadyDone) {
            ++m_skipped;
            m_model->setRuntime(itemId, QString());
            emit progressChanged();
            pump();
            return;
        }
    }

    // A paper the quick read already gave up on has nothing for nine more
    // calls to work with. Skipping it is not a failure -- it is the answer
    // the quick read already paid for.
    if (m_deep && hasQuick && quick.status == Analysis::StatusInsufficient) {
        ++m_skipped;
        m_model->setRuntime(itemId, QString());
        emit progressChanged();
        pump();
        return;
    }

    // Counted and spoken for once per paper, whichever of the two runs
    // below ends up doing the work.
    ++m_running;
    claimPaper(paperId);
    emit progressChanged();

    if (!m_deep) {
        startQuick(itemId, paperId, title, blocks, /*thenDeep=*/false);
        pump();
        return;
    }
    if (!hasQuick) {
        // The nine parts are written against the quick interpretation, so a
        // paper reached without one gets it first rather than being failed.
        startQuick(itemId, paperId, title, blocks, /*thenDeep=*/true);
        return;
    }
    beginDeepRun(itemId, paperId, title, blocks, quick.payload);
}

void BatchAnalysisService::startQuick(const QString &itemId,
                                      const QString &paperId,
                                      const QString &title,
                                      const QVector<Block> &blocks,
                                      bool thenDeep)
{
    // Rebuilt when the model configuration moved (a batch has several interpretations in flight at once).
    m_client = m_clients.client();

    QuickAnalysisJob::Input in;
    in.paperId = paperId;
    in.title = title;
    in.blocks = blocks;
    in.lang = m_settings->targetLang();
    in.profileBlock = m_profile->promptBlock();
    in.contextChars =
        qBound(20000, int(m_settings->contextWindow() * 1.4), 400000);
    in.maxTokens = m_settings->analysisMaxTokens();

    setStatus(thenDeep ? tr("Interpreting %1 first…").arg(title)
                       : tr("Interpreting %1…").arg(title));

    auto *job = QuickAnalysisJob::start(m_client, in, this);
    connect(job, &QuickAnalysisJob::succeeded, this,
            [this, itemId, paperId, title, blocks, thenDeep,
             job](const QJsonObject &digest) {
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
                if (thenDeep && !insufficient && !m_cancelled) {
                    // Still the same paper, still counted, still claimed:
                    // the context it needed has just arrived.
                    beginDeepRun(itemId, paperId, title, blocks, digest);
                    return;
                }
                --m_running;
                releasePaper(paperId);
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
}

void BatchAnalysisService::beginDeepRun(const QString &itemId,
                                        const QString &paperId,
                                        const QString &title,
                                        const QVector<Block> &blocks,
                                        const QJsonObject &digest)
{
    m_deepRun = DeepRun{};
    m_deepRun.active = true;
    m_deepRun.itemId = itemId;
    m_deepRun.paperId = paperId;
    m_deepRun.title = title;
    m_deepRun.blocks = blocks;
    m_deepRun.digest = digest;

    // A reading interrupted after six parts picks up at the seventh. Only a
    // forced run asks for the ones that are already on disk again.
    if (!m_force) {
        const AnalysisRecord existing =
            m_store->paperAnalysis(paperId, Analysis::KindDeep);
        if (existing.valid)
            m_deepRun.modules =
                existing.payload.value(QStringLiteral("modules")).toObject();
    }
    for (const QString &id : Analysis::deepModules()) {
        if (!m_deepRun.modules.contains(id))
            m_deepRun.queue.append(id);
    }
    m_deepRun.done = int(m_deepRun.modules.size());
    pumpDeepModules();
}

void BatchAnalysisService::pumpDeepModules()
{
    if (!m_deepRun.active)
        return;
    if (m_cancelled) {
        // Same bargain as the quick run: nothing new is asked for, but the
        // parts already at the model are let land and filed.
        m_deepRun.queue.clear();
        if (m_deepRun.inflight == 0)
            finishDeepRun();
        return;
    }
    const int limit = m_settings ? m_settings->analysisConcurrency() : 2;
    while (m_deepRun.inflight < limit && !m_deepRun.queue.isEmpty())
        startDeepModule(m_deepRun.queue.takeFirst());
    if (m_deepRun.inflight == 0 && m_deepRun.queue.isEmpty())
        finishDeepRun();
}

void BatchAnalysisService::startDeepModule(const QString &moduleId)
{
    m_client = m_clients.client();

    DeepModuleJob::Input in;
    in.paperId = m_deepRun.paperId;
    in.title = m_deepRun.title;
    in.moduleId = moduleId;
    in.blocks = m_deepRun.blocks;
    in.lang = m_settings->targetLang();
    in.profileBlock = m_profile->promptBlock();
    in.digest = m_deepRun.digest;
    in.contextChars =
        qBound(20000, int(m_settings->contextWindow() * 1.4), 400000);
    in.maxTokens = m_settings->analysisMaxTokens();

    ++m_deepRun.inflight;
    setStatus(tr("%1 — %2 (%3/%4)")
                  .arg(m_deepRun.title, Analysis::deepModuleTitle(moduleId))
                  .arg(m_deepRun.done + 1)
                  .arg(deepPartsTotal()));
    emit progressChanged();

    // The run this job belongs to. By the time it answers the service may
    // be on another paper entirely, and that answer is not for this one.
    const QString paperAtStart = m_deepRun.paperId;
    auto *job = DeepModuleJob::start(m_client, in, this);
    connect(job, &DeepModuleJob::succeeded, this,
            [this, paperAtStart, job](const QString &moduleId,
                                      const QJsonObject &result) {
                if (!m_deepRun.active || m_deepRun.paperId != paperAtStart)
                    return;
                --m_deepRun.inflight;
                m_deepRun.modules.insert(moduleId, result);
                m_deepRun.contentHash = job->contentHash();
                ++m_deepRun.done;
                ++m_deepRun.fetched;
                emit progressChanged();
                pumpDeepModules();
            });
    connect(job, &DeepModuleJob::failed, this,
            [this, paperAtStart](const QString &, const QString &error) {
                if (!m_deepRun.active || m_deepRun.paperId != paperAtStart)
                    return;
                --m_deepRun.inflight;
                if (m_deepRun.firstError.isEmpty())
                    m_deepRun.firstError = error;
                emit progressChanged();
                pumpDeepModules();
            });
}

void BatchAnalysisService::finishDeepRun()
{
    if (!m_deepRun.active)
        return;
    const QString itemId = m_deepRun.itemId;
    const QString paperId = m_deepRun.paperId;
    const QString title = m_deepRun.title;
    const QString error = m_deepRun.firstError;
    const int have = int(m_deepRun.modules.size());

    // Whatever came back is filed, even a partial reading: eight parts paid
    // for and thrown away because the ninth timed out is the one outcome
    // nobody wants. Nothing new fetched means nothing to write -- the parts
    // in hand are the ones already on disk.
    if (m_deepRun.fetched > 0) {
        QJsonObject payload;
        payload.insert(QStringLiteral("modules"), m_deepRun.modules);
        payload.insert(
            QStringLiteral("meta"),
            QJsonObject{{QStringLiteral("model"), m_settings->model()},
                        {QStringLiteral("modulesTotal"), deepPartsTotal()}});
        m_store->putPaperAnalysis(
            paperId, Analysis::KindDeep, payload, m_settings->model(),
            Analysis::inputHash(m_deepRun.contentHash,
                                AnalysisPrompts::promptVersion(),
                                m_profile->hash(), m_settings->model()),
            Analysis::StatusOk, QString(), title);
    }
    m_deepRun = DeepRun{};

    --m_running;
    releasePaper(paperId);
    if (have >= deepPartsTotal()) {
        ++m_done;
        m_model->setRuntime(itemId, QString());
        emit progressChanged();
    } else {
        recordFailure(itemId,
                      error.isEmpty()
                          ? tr("Only %1 of the %2 parts came back.")
                                .arg(have)
                                .arg(deepPartsTotal())
                          : error);
    }
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
    setStatus(m_deep ? tr("%1 close-read, %2 failed, %3 already done.")
                           .arg(m_done)
                           .arg(m_failed)
                           .arg(m_skipped)
                     : tr("%1 interpreted, %2 failed, %3 already done.")
                           .arg(m_done)
                           .arg(m_failed)
                           .arg(m_skipped));
    emit progressChanged();
    finishTask(m_failed == 0,
               m_failed == 0 ? QString()
               : m_deep     ? tr("%1 of %2 could not be close-read.")
                                  .arg(m_failed)
                                  .arg(m_total)
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
