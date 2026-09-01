#include "AnalysisService.h"
#include "Stall.h"

#include "AnalysisJob.h"
#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "BlockListModel.h"
#include "EvidenceIndex.h"
#include "LlmClient.h"
#include "PaperController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "TaskManager.h"
#include "TaskTypes.h"

#include <QDateTime>
#include <QTimer>

AnalysisService::AnalysisService(Settings *settings, PaperController *paper,
                                 AnalysisStore *store,
                                 ProjectProfileController *profile,
                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_store(store)
    , m_profile(profile)
    , m_clients(settings, this)
{
    connect(m_paper, &PaperController::blocksChanged, this,
            &AnalysisService::onPaperChanged);
    // A collaborator's interpretation can land at any time, and switching
    // project changes which one applies.
    connect(m_store, &AnalysisStore::changed, this, [this]() {
        reloadFromStore();
        emit stateChanged();
    });
    connect(m_profile, &ProjectProfileController::changed, this, [this]() {
        // The profile is part of the staleness key: editing it means last
        // week's readings were written against different goals.
        announceQuick();
    });
    if (m_settings) {
        connect(m_settings, &Settings::analysisConfigChanged, this,
                &AnalysisService::stateChanged);
        connect(m_settings, &Settings::configurationChanged, this,
                &AnalysisService::stateChanged);
    }
}

void AnalysisService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A task stopped while it was still queued never calls its stop callback
    // -- there was nothing to stop -- so this is the only word the service
    // gets that the id it is holding is dead. Without it the pane would sit
    // at Running for ever and refuse to start the reading again.
    connect(m_tasks, &TaskManager::taskFinished, this,
            [this](const QString &id, bool, const QString &) {
                if (id.isEmpty())
                    return;
                if (id == m_quickTaskId) {
                    m_quickTaskId.clear();
                    if (m_status == Running && !m_job)
                        setStatus(Idle);
                    return;
                }
                if (id == m_deepTaskId) {
                    m_deepTaskId.clear();
                    m_deepTaskTotal = 0;
                    m_deepTaskStarted = false;
                    m_deepRunModules.clear();
                    return;
                }
                for (auto it = m_moduleTaskIds.begin();
                     it != m_moduleTaskIds.end(); ++it) {
                    if (it.value() != id)
                        continue;
                    m_moduleTaskIds.erase(it);
                    announceDeep();
                    return;
                }
            });

    // Both readings are of whatever paper is open -- the paragraphs come from
    // PaperController, not from the payload -- so a task the last session
    // left behind can only be picked up again when the reader is back on that
    // same paper. Refusing is the right answer otherwise: interpreting one
    // paper into another's row would be worse than not resuming at all.
    m_tasks->registerResumer(
        Tasks::Kind::QuickInterpret, [this](const QJsonObject &resume) {
            const QString id = resume.value(QStringLiteral("paperId")).toString();
            if (id.isEmpty() || id != paperId() || !canRun())
                return false;
            // This paper is already being interpreted. generateQuick() would
            // return without starting anything and leave m_quickTaskId set
            // from the run that is under way, so answering from it would
            // claim a run this one never made -- and the pending task would
            // be dropped for nothing.
            if (!m_quickTaskId.isEmpty() || m_status == Running)
                return false;
            const QString title = resume.value(QStringLiteral("title")).toString();
            if (!title.isEmpty() && m_paperTitle.isEmpty())
                setPaperTitle(title);
            generateQuick(true);
            return !m_quickTaskId.isEmpty();
        });

    m_tasks->registerResumer(
        Tasks::Kind::DeepInterpret, [this](const QJsonObject &resume) {
            const QString id = resume.value(QStringLiteral("paperId")).toString();
            if (id.isEmpty() || id != paperId() || !canRun())
                return false;
            // A close reading of this paper is already open; there is
            // nothing here for a second one to start.
            if (!m_deepTaskId.isEmpty())
                return false;
            QStringList modules;
            for (const QJsonValue &v :
                 resume.value(QStringLiteral("modules")).toArray())
                modules.append(v.toString());
            // Whatever landed before the app closed stays where it is; only
            // the modules that never made it run again.
            return beginDeepRun(deepModulesToRun(false, modules), false);
        });
}

QString AnalysisService::paperId() const
{
    return m_paper ? m_paper->paperId() : QString();
}

void AnalysisService::setPaperTitle(const QString &title)
{
    if (title == m_paperTitle)
        return;
    m_paperTitle = title;
    emit paperChanged();
}

bool AnalysisService::canRun() const
{
    if (!m_settings || !m_paper)
        return false;
    if (m_paper->blockCount() == 0)
        return false;
    return m_settings->isConfigured();
}

QString AnalysisService::modelInUse() const
{
    return m_settings ? m_settings->model() : QString();
}

int AnalysisService::contextChars() const
{
    // Roughly 3.5 characters per token, and the paper gets about 40% of the
    // window -- the rest is the system prompt, the schema and the answer.
    const int window = m_settings ? m_settings->contextWindow() : 128000;
    return qBound(20000, int(window * 1.4), 400000);
}

void AnalysisService::announceQuick()
{
    if (m_batching) {
        m_quickDirty = true;
        return;
    }
    emit quickChanged();
}

void AnalysisService::announceDeep()
{
    if (m_batching) {
        m_deepDirty = true;
        return;
    }
    emit deepChanged();
}

void AnalysisService::beginBatch() { m_batching = true; }

void AnalysisService::endBatch()
{
    m_batching = false;
    if (m_quickDirty) {
        m_quickDirty = false;
        emit quickChanged();
    }
    if (m_deepDirty) {
        m_deepDirty = false;
        emit deepChanged();
    }
}

void AnalysisService::onPaperChanged()
{
    const QString id = paperId();
    if (id == m_lastPaperId)
        return;                    // a paragraph edit, not a new paper
    m_lastPaperId = id;
    // Emptying and refilling is one change as far as anyone watching is
    // concerned, and the pane rebuilds its whole tree for each one it hears.
    beginBatch();
    cancel();
    cancelDeep();
    clearQuick();
    clearDeep();
    m_contentHash.clear();
    emit paperChanged();
    reloadFromStore();
    endBatch();
}

QString AnalysisService::currentInputHash() const
{
    if (m_contentHash.isEmpty())
        return {};
    return Analysis::inputHash(m_contentHash, AnalysisPrompts::promptVersion(),
                               m_profile->hash(), modelInUse());
}

bool AnalysisService::quickStale() const
{
    if (m_quick.isEmpty() || m_quickInputHash.isEmpty())
        return false;
    const QString now = currentInputHash();
    if (now.isEmpty())
        return false;             // can't tell yet; don't cry wolf
    return now != m_quickInputHash;
}

void AnalysisService::clearDeep()
{
    m_deep = QJsonObject();
    m_deepInputHash.clear();
    m_deepAuthorEmail.clear();
    m_deepUpdatedAt.clear();
    m_deepIsMine = true;
    m_deepSaved = false;
    m_deepQueue.clear();
    m_deepErrors.clear();
    m_deepBusy.clear();
    m_deepRunModules.clear();
    announceDeep();
}

void AnalysisService::clearQuick()
{
    m_quick = QJsonObject();
    m_quickInputHash.clear();
    m_quickAuthorEmail.clear();
    m_quickModel.clear();
    m_quickUpdatedAt.clear();
    m_quickIsMine = true;
    m_quickSaved = false;
    announceQuick();
}

void AnalysisService::reloadFromStore()
{
    Stall::Mark mark("loading the interpretation from the project");
    const QString id = paperId();
    if (id.isEmpty()) {
        if (!m_quick.isEmpty())
            clearQuick();
        return;
    }
    // Never yank away a reading that is on screen and newer than the store
    // (we just generated it and the project may be read-only).
    if (m_status == Running)
        return;

    reloadNotes();

    // The close reading, unless one is being written right now.
    if (!deepRunning()) {
        const AnalysisRecord deep =
            m_store->paperAnalysis(id, Analysis::KindDeep);
        if (deep.valid && !deep.payload.isEmpty()) {
            if (deep.payload != m_deep || deep.updatedAt != m_deepUpdatedAt) {
                m_deep = deep.payload;
                m_deepInputHash = deep.inputHash;
                m_deepAuthorEmail = deep.authorEmail;
                m_deepUpdatedAt = deep.updatedAt;
                m_deepIsMine = deep.mine;
                m_deepSaved = true;
                announceDeep();
            }
        } else if (m_deepSaved) {
            clearDeep();
        }
    }

    const AnalysisRecord rec = m_store->paperAnalysis(id, Analysis::KindQuick);
    if (!rec.valid || rec.payload.isEmpty()) {
        if (m_quickSaved)
            clearQuick();          // it was removed elsewhere
        return;
    }
    if (rec.payload == m_quick && rec.updatedAt == m_quickUpdatedAt)
        return;
    m_quick = rec.payload;
    m_quickInputHash = rec.inputHash;
    m_quickAuthorEmail = rec.authorEmail;
    m_quickModel = rec.model;
    m_quickUpdatedAt = rec.updatedAt;
    m_quickIsMine = rec.mine;
    m_quickSaved = true;
    if (m_status != Running)
        setStatus(Done);
    announceQuick();
}

void AnalysisService::generateQuick(bool force)
{
    if (!m_paper || !m_settings)
        return;
    if (m_status == Running)
        return;
    if (!canRun()) {
        setStatus(Failed,
                  m_paper->blockCount() == 0
                      ? tr("Open a paper and segment it first.")
                      : tr("No model is configured for interpretations. Open "
                           "Settings to pick one."));
        return;
    }
    if (!force && hasQuick() && !quickStale())
        return;

    if (!m_tasks) {
        startQuickRun();
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::QuickInterpret;
    req.title = tr("Interpret");
    req.paperId = paperId();
    req.paperTitle =
        m_paperTitle.isEmpty() ? m_paper->fileName() : m_paperTitle;
    req.projectId = m_store->projectId();
    req.steps = 1;
    // The exclusion key is left at its default, "<kind>|<paperId>": one
    // digest per paper at a time is exactly the rule we want.
    req.resume = QJsonObject{{QStringLiteral("paperId"), req.paperId},
                             {QStringLiteral("title"), req.paperTitle}};

    const QString paper = req.paperId;
    const QString id = m_tasks->submit(
        req,
        // submit() may call this before it returns, so the work hops through
        // the event loop: the id has to be on record before anything can
        // report progress or finish against it. The paper is checked again on
        // the way in -- a queued task can wait out the reader moving on.
        [this, paper] {
            QTimer::singleShot(0, this, [this, paper] {
                // Cancelled while it waited, or the reader moved on: either
                // way there is nothing left to run.
                if (m_quickTaskId.isEmpty())
                    return;
                if (paper != paperId()) {
                    // The paper was closed under a task that waited. That is
                    // not a failure and there is no reason to show; the row
                    // ends Canceled.
                    cancelQuickTask();
                    return;
                }
                startQuickRun();
            });
        },
        [this] { cancel(); });
    if (id.isEmpty())
        return;                    // the same digest is already on its way
    m_quickTaskId = id;
    // Queued counts as running to the pane: the reader asked for it, and the
    // button should not invite them to ask again.
    setStatus(Running);
}

void AnalysisService::startQuickRun()
{
    if (!m_paper || !m_settings) {
        // A real failure, so it is reported as one -- but with something the
        // row can show, rather than a Failed task with nothing under it.
        finishQuickTask(false, tr("There is no paper to interpret."));
        return;
    }

    const QVector<Block> blocks = m_paper->blocks()->allBlocks();
    QuickAnalysisJob::Input in;
    in.paperId = paperId();
    in.title = m_paperTitle.isEmpty() ? m_paper->fileName() : m_paperTitle;
    in.blocks = blocks;
    in.lang = m_settings->targetLang();
    in.profileBlock = m_profile->promptBlock();
    in.contextChars = contextChars();
    in.maxTokens = m_settings->analysisMaxTokens();

    refreshClient();

    setStatus(Running);
    const QString paperAtStart = in.paperId;
    m_job = QuickAnalysisJob::start(m_client, in, this);
    connect(m_job, &QuickAnalysisJob::succeeded, this,
            [this, paperAtStart](const QJsonObject &digest) {
                QuickAnalysisJob *job = m_job;
                m_job.clear();
                // The task is over either way; whether we keep the answer is
                // a separate question.
                finishQuickTask(true);
                if (paperAtStart != paperId())
                    return;        // the reader moved on
                m_contentHash = job ? job->contentHash() : QString();
                m_quick = digest;
                m_quickInputHash = currentInputHash();
                m_quickModel = modelInUse();
                m_quickAuthorEmail = m_store->userEmail();
                m_quickIsMine = true;
                const bool insufficient =
                    digest.value(QStringLiteral("insufficient")).toBool();
                m_quickSaved = m_store->putPaperAnalysis(
                    paperAtStart, Analysis::KindQuick, digest, m_quickModel,
                    m_quickInputHash,
                    insufficient ? Analysis::StatusInsufficient
                                 : Analysis::StatusOk,
                    QString(),
                    m_paperTitle.isEmpty() && m_paper ? m_paper->fileName()
                                                      : m_paperTitle);
                m_quickUpdatedAt =
                    m_store->paperAnalysis(paperAtStart, Analysis::KindQuick)
                        .updatedAt;
                setStatus(Done);
                announceQuick();
            });
    connect(m_job, &QuickAnalysisJob::failed, this,
            [this, paperAtStart](const QString &error) {
                m_job.clear();
                finishQuickTask(false, error);
                if (paperAtStart != paperId())
                    return;
                setStatus(Failed, error);
            });
}

void AnalysisService::cancel()
{
    if (m_job) {
        m_job->abort();
        m_job.clear();
    }
    // The reader stopped it, or the paper was closed: nothing went wrong, so
    // the row must not read Failed with no reason under it.
    cancelQuickTask();
    if (m_status == Running)
        setStatus(Idle);
}

QString AnalysisService::takeQuickTaskId()
{
    if (!m_tasks || m_quickTaskId.isEmpty())
        return {};
    // Cleared first: the manager must never be told twice about one task,
    // and cancel() and a job's own answer can race for the same one.
    const QString id = m_quickTaskId;
    m_quickTaskId.clear();
    return id;
}

void AnalysisService::finishQuickTask(bool ok, const QString &error)
{
    const QString id = takeQuickTaskId();
    if (!id.isEmpty())
        m_tasks->finish(id, ok, error);
}

void AnalysisService::cancelQuickTask()
{
    const QString id = takeQuickTaskId();
    if (!id.isEmpty())
        m_tasks->markCanceled(id);
}

void AnalysisService::discardQuick()
{
    const QString id = paperId();
    if (id.isEmpty())
        return;
    m_store->removePaperAnalysis(id, Analysis::KindQuick);
    clearQuick();
    setStatus(Idle);
}

void AnalysisService::refreshClient()
{
    // Rebuilt when the configuration moved -- but not while a job is in
    // flight: every LlmReply is a child of the client, so replacing it
    // would leave that job waiting for a signal that can never come.
    m_client = m_clients.client();
}

void AnalysisService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit stateChanged();
}

QVariantList AnalysisService::quickHistory() const
{
    QVariantList out;
    for (const QJsonValue &v :
         m_store->paperHistoryIndex(paperId(), Analysis::KindQuick))
        out.append(v.toObject().toVariantMap());
    return out;
}

bool AnalysisService::restoreQuick(int index)
{
    if (!m_store->restorePaperVersion(paperId(), Analysis::KindQuick, index))
        return false;
    reloadFromStore();
    return true;
}

QVariantList AnalysisService::deepHistory() const
{
    QVariantList out;
    for (const QJsonValue &v :
         m_store->paperHistoryIndex(paperId(), Analysis::KindDeep))
        out.append(v.toObject().toVariantMap());
    return out;
}

bool AnalysisService::restoreDeep(int index)
{
    if (!m_store->restorePaperVersion(paperId(), Analysis::KindDeep, index))
        return false;
    reloadFromStore();
    return true;
}

// ── the close reading (§3) ───────────────────────────────────────────

QStringList AnalysisService::moduleIds() const { return Analysis::deepModules(); }

QString AnalysisService::moduleTitle(const QString &id) const
{
    return Analysis::deepModuleTitle(id);
}

QVariantMap AnalysisService::module(const QString &id) const
{
    return m_deep.value(QStringLiteral("modules"))
        .toObject()
        .value(id)
        .toObject()
        .toVariantMap();
}

QString AnalysisService::moduleError(const QString &id) const
{
    return m_deepErrors.value(id);
}

bool AnalysisService::moduleBusy(const QString &id) const
{
    // A module whose own task is still waiting its turn counts as busy: the
    // reader asked for it, and the button should not invite them to ask
    // again. (Without a manager there are no such tasks, so nothing moves.)
    return m_deepBusy.contains(id) || m_moduleTaskIds.contains(id);
}

bool AnalysisService::hasDeep() const
{
    return !m_deep.value(QStringLiteral("modules")).toObject().isEmpty();
}

int AnalysisService::deepDone() const
{
    return m_deep.value(QStringLiteral("modules")).toObject().size();
}

int AnalysisService::deepTotal() const { return Analysis::deepModules().size(); }

bool AnalysisService::deepStale() const
{
    if (!hasDeep() || m_deepInputHash.isEmpty())
        return false;
    const QString now = currentInputHash();
    if (now.isEmpty())
        return false;
    return now != m_deepInputHash;
}

void AnalysisService::generateDeep(bool force)
{
    if (!m_paper || !m_settings)
        return;
    if (!canRun()) {
        setStatus(Failed,
                  m_paper->blockCount() == 0
                      ? tr("Open a paper and segment it first.")
                      : tr("No model is configured for interpretations. Open "
                           "Settings to pick one."));
        return;
    }
    beginDeepRun(deepModulesToRun(force, QStringList()), force);
}

QStringList AnalysisService::deepModulesToRun(bool force,
                                              const QStringList &only) const
{
    const QJsonObject have = m_deep.value(QStringLiteral("modules")).toObject();
    QStringList out;
    for (const QString &id : Analysis::deepModules()) {
        if (!only.isEmpty() && !only.contains(id))
            continue;
        if (!force && have.contains(id))
            continue;             // keep what is already written
        if (m_deepQueue.contains(id) || m_deepBusy.contains(id))
            continue;
        if (m_moduleTaskIds.contains(id))
            continue;             // spoken for by a §5 task of its own
        out.append(id);
    }
    return out;
}

bool AnalysisService::beginDeepRun(const QStringList &modules, bool force)
{
    if (modules.isEmpty()) {
        // Nothing left to run still clears the errors and tells the pane,
        // the way it always did -- but not while a task of ours is open:
        // startDeepRun() marks the run started, and a task the manager has
        // not admitted yet would then be declared finished by pumpDeep()
        // without a single module having been asked for.
        if (m_deepTaskId.isEmpty())
            startDeepRun(modules, force);
        return false;
    }
    if (!m_tasks) {
        startDeepRun(modules, force);
        return true;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::DeepInterpret;
    req.title = tr("Close read");
    req.paperId = paperId();
    req.paperTitle =
        m_paperTitle.isEmpty() && m_paper ? m_paper->fileName() : m_paperTitle;
    req.projectId = m_store->projectId();
    req.steps = int(modules.size());
    QJsonArray wanted;
    for (const QString &id : modules)
        wanted.append(id);
    // The modules go into the payload so a resumed run picks up where this
    // one stopped instead of paying for the nine all over again.
    req.resume = QJsonObject{{QStringLiteral("paperId"), req.paperId},
                             {QStringLiteral("modules"), wanted}};

    const QString paper = req.paperId;
    const QString id = m_tasks->submit(
        req,
        // As with the digest: submit() may call this before it returns, the
        // id has to exist before the first module reports back, and the
        // reader may have moved to another paper while this waited.
        [this, modules, force, paper] {
            QTimer::singleShot(0, this, [this, modules, force, paper] {
                if (m_deepTaskId.isEmpty())
                    return;       // cancelled while it waited
                if (paper != paperId()) {
                    // The paper was closed while this waited: canceled, not
                    // failed, and nothing to explain.
                    cancelDeepTask();
                    return;
                }
                startDeepRun(modules, force);
            });
        },
        [this] { cancelDeep(); });
    if (id.isEmpty())
        return false;              // this paper is already being read
    m_deepTaskId = id;
    m_deepTaskTotal = int(modules.size());
    m_deepTaskStarted = false;
    return true;
}

void AnalysisService::startDeepRun(const QStringList &modules, bool force)
{
    m_deepTaskStarted = true;
    m_deepForce = force;
    // What this run is answerable for. A module being redone on its own
    // travels through the same queue but is not one of these, so it neither
    // moves this run's bar nor decides whether it succeeded.
    m_deepRunModules.clear();
    for (const QString &id : modules) {
        m_deepRunModules.insert(id);
        if (!m_deepQueue.contains(id) && !m_deepBusy.contains(id))
            m_deepQueue.append(id);
    }
    m_deepErrors.clear();
    announceDeep();
    pumpDeep();
}

void AnalysisService::regenerateModule(const QString &id)
{
    if (!canRun() || id.isEmpty())
        return;
    if (m_deepBusy.contains(id) || m_deepQueue.contains(id))
        return;
    if (m_moduleTaskIds.contains(id))
        return;                    // this module is already being redone
    m_deepErrors.remove(id);

    if (!m_tasks) {
        startModuleRun(id);
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::DeepInterpret;
    // The module's own title: a row reading "Close read" beside the whole
    // run's would say nothing about which of the two the reader stopped.
    req.title = moduleTitle(id);
    req.paperId = paperId();
    req.paperTitle =
        m_paperTitle.isEmpty() && m_paper ? m_paper->fileName() : m_paperTitle;
    req.projectId = m_store->projectId();
    // Not the default "<kind>|<paperId>": redoing one part is not the whole
    // reading, so it must not be refused because the nine are running -- only
    // because this same module already is.
    req.exclusiveKey = QStringLiteral("deep_interpret|") + req.paperId
                       + QChar('|') + id;
    req.steps = 1;
    req.resume = QJsonObject{{QStringLiteral("paperId"), req.paperId},
                             {QStringLiteral("modules"), QJsonArray{id}}};

    const QString paper = req.paperId;
    const QString taskId = m_tasks->submit(
        req,
        // As everywhere else here: submit() may call this before it returns,
        // and the id has to be on record before the module reports back.
        [this, id, paper] {
            QTimer::singleShot(0, this, [this, id, paper] {
                if (!m_moduleTaskIds.contains(id))
                    return;        // cancelled while it waited
                if (paper != paperId()) {
                    cancelModuleTask(id);
                    return;
                }
                startModuleRun(id);
            });
        },
        [this, id] { cancelModule(id); });
    if (taskId.isEmpty())
        return;                    // this module is already on its way
    m_moduleTaskIds.insert(id, taskId);
    announceDeep();
}

void AnalysisService::startModuleRun(const QString &id)
{
    // A whole run may have swept this module up while its own task waited.
    // One call answers both rows -- the job's own handler closes whichever
    // tasks are watching this module -- so it is not asked for twice.
    if (!m_deepQueue.contains(id) && !m_deepBusy.contains(id))
        m_deepQueue.append(id);
    announceDeep();
    pumpDeep();
}

void AnalysisService::cancelModule(const QString &id)
{
    // What the viewer's stop button means for one module: drop it if it is
    // still waiting. One already at the model is left to land -- there is no
    // handle here to abort a single job with -- but its row is over.
    m_deepQueue.removeAll(id);
    cancelModuleTask(id);
    announceDeep();
    pumpDeep();
}

void AnalysisService::cancelDeep()
{
    m_deepQueue.clear();
    // The jobs themselves are children of this object and abort with it;
    // clearing the busy set is what stops their results from landing.
    for (const QString &id : m_deepBusy)
        m_deepErrors.remove(id);
    m_deepBusy.clear();
    m_deepInflight = 0;
    // The reader stopped it, or the paper was closed: canceled, not failed.
    cancelDeepTask();
    // A module being redone on its own has a task of its own, and the queue
    // it was waiting in has just been emptied -- its row would sit at
    // Running for ever if it were not ended here too.
    const QStringList redoing = m_moduleTaskIds.keys();
    for (const QString &id : redoing)
        cancelModuleTask(id);
    announceDeep();
}

QString AnalysisService::firstDeepError(const QSet<QString> &only) const
{
    // In display order, so the reader is told about the first module that
    // failed rather than whichever one the hash happens to yield.
    for (const QString &id : Analysis::deepModules()) {
        if (!only.isEmpty() && !only.contains(id))
            continue;
        if (m_deepErrors.contains(id))
            return m_deepErrors.value(id);
    }
    return {};
}

int AnalysisService::deepRunLeft() const
{
    int left = 0;
    for (const QString &id : m_deepRunModules) {
        if (m_deepQueue.contains(id) || m_deepBusy.contains(id))
            ++left;
    }
    return left;
}

void AnalysisService::reportDeepProgress()
{
    // A module being redone on its own is one step of a task of its own,
    // reported where it starts and where it ends; there is nothing in
    // between for this to say about it.
    if (!m_tasks || m_deepTaskId.isEmpty())
        return;
    const int left = deepRunLeft();
    if (left > m_deepTaskTotal)
        m_deepTaskTotal = left;    // more modules joined this run
    m_tasks->setProgress(m_deepTaskId, m_deepTaskTotal - left, m_deepTaskTotal);
    for (const QString &id : Analysis::deepModules()) {
        if (m_deepRunModules.contains(id) && m_deepBusy.contains(id)) {
            m_tasks->setNote(m_deepTaskId, moduleTitle(id));
            break;
        }
    }
}

QString AnalysisService::takeDeepTaskId()
{
    if (!m_tasks || m_deepTaskId.isEmpty())
        return {};
    const QString id = m_deepTaskId;
    m_deepTaskId.clear();
    m_deepTaskTotal = 0;
    m_deepTaskStarted = false;
    m_deepRunModules.clear();
    return id;
}

void AnalysisService::finishDeepTask(bool ok, const QString &error)
{
    const QString id = takeDeepTaskId();
    if (!id.isEmpty())
        m_tasks->finish(id, ok, error);
}

void AnalysisService::cancelDeepTask()
{
    const QString id = takeDeepTaskId();
    if (!id.isEmpty())
        m_tasks->markCanceled(id);
}

QString AnalysisService::takeModuleTaskId(const QString &moduleId)
{
    if (!m_tasks)
        return {};
    return m_moduleTaskIds.take(moduleId);
}

void AnalysisService::finishModuleTask(const QString &moduleId, bool ok,
                                       const QString &error)
{
    const QString id = takeModuleTaskId(moduleId);
    if (!id.isEmpty())
        m_tasks->finish(id, ok, error);
}

void AnalysisService::cancelModuleTask(const QString &moduleId)
{
    const QString id = takeModuleTaskId(moduleId);
    if (!id.isEmpty())
        m_tasks->markCanceled(id);
}

void AnalysisService::discardDeep()
{
    const QString id = paperId();
    if (id.isEmpty())
        return;
    cancelDeep();
    m_store->removePaperAnalysis(id, Analysis::KindDeep);
    clearDeep();
}

void AnalysisService::pumpDeep()
{
    const int limit = m_settings ? m_settings->analysisConcurrency() : 2;
    while (m_deepInflight < limit && !m_deepQueue.isEmpty())
        startModule(m_deepQueue.takeFirst());
    announceDeep();
    reportDeepProgress();
    // The run is over the moment none of its own modules is left; the ones
    // that failed decide whether it counts as a success. A task still
    // waiting in the queue has not begun and cannot be over.
    if (m_deepTaskStarted && deepRunLeft() <= 0) {
        const QString err = firstDeepError(m_deepRunModules);
        finishDeepTask(err.isEmpty(), err);
    }
}

void AnalysisService::startModule(const QString &id)
{
    if (!m_paper || !m_settings) {
        // pumpDeep() has already taken this module off the queue. Returning
        // here used to drop it without a word, and the run that was missing
        // it was reported as a success; it is recorded as the failure it is
        // instead, so whichever task owns it ends saying so.
        const QString why = tr("The paper is no longer open.");
        m_deepErrors.insert(id, why);
        finishModuleTask(id, false, why);
        return;
    }
    refreshClient();

    DeepModuleJob::Input in;
    in.paperId = paperId();
    in.title = m_paperTitle.isEmpty() ? m_paper->fileName() : m_paperTitle;
    in.moduleId = id;
    in.blocks = m_paper->blocks()->allBlocks();
    in.lang = m_settings->targetLang();
    in.profileBlock = m_profile->promptBlock();
    in.digest = m_quick;
    in.contextChars = contextChars();
    in.maxTokens = m_settings->analysisMaxTokens();

    m_deepBusy.insert(id);
    ++m_deepInflight;
    const QString paperAtStart = in.paperId;
    if (m_tasks && m_moduleTaskIds.contains(id)) {
        m_tasks->setProgress(m_moduleTaskIds.value(id), 0, 1);
        m_tasks->setNote(m_moduleTaskIds.value(id), moduleTitle(id));
    }

    auto *job = DeepModuleJob::start(m_client, in, this);
    connect(job, &DeepModuleJob::succeeded, this,
            [this, paperAtStart, job](const QString &moduleId,
                                      const QJsonObject &result) {
                --m_deepInflight;
                m_deepBusy.remove(moduleId);
                if (paperAtStart != paperId()) {
                    // The reader moved on while this was at the model; the
                    // answer is for a paper that is no longer open.
                    cancelModuleTask(moduleId);
                    return;
                }
                QJsonObject modules =
                    m_deep.value(QStringLiteral("modules")).toObject();
                modules.insert(moduleId, result);
                m_deep.insert(QStringLiteral("modules"), modules);
                m_contentHash = job->contentHash();
                persistDeep();
                finishModuleTask(moduleId, true);
                announceDeep();
                pumpDeep();
            });
    connect(job, &DeepModuleJob::failed, this,
            [this, paperAtStart](const QString &moduleId,
                                 const QString &error) {
                --m_deepInflight;
                m_deepBusy.remove(moduleId);
                if (paperAtStart != paperId()) {
                    cancelModuleTask(moduleId);
                    return;
                }
                m_deepErrors.insert(moduleId, error);
                finishModuleTask(moduleId, false, error);
                announceDeep();
                pumpDeep();
            });
    announceDeep();
}

void AnalysisService::persistDeep()
{
    const QString id = paperId();
    if (id.isEmpty() || !hasDeep())
        return;
    m_deepInputHash = currentInputHash();
    m_deepIsMine = true;
    m_deepAuthorEmail = m_store->userEmail();
    m_deep.insert(QStringLiteral("meta"),
                  QJsonObject{{QStringLiteral("model"), modelInUse()},
                              {QStringLiteral("modulesTotal"), deepTotal()}});
    m_deepSaved = m_store->putPaperAnalysis(
        id, Analysis::KindDeep, m_deep, modelInUse(), m_deepInputHash,
        Analysis::StatusOk, QString(),
        m_paperTitle.isEmpty() && m_paper ? m_paper->fileName() : m_paperTitle);
    m_deepUpdatedAt =
        m_store->paperAnalysis(id, Analysis::KindDeep).updatedAt;
}

// ── the reader's own notes (§5, §16) ─────────────────────────────────

void AnalysisService::reloadNotes()
{
    const QJsonArray fresh =
        m_store->note(paperId()).value(QStringLiteral("notes")).toArray();
    if (fresh == m_notes)
        return;
    m_notes = fresh;
    emit notesChanged();
}

QVariantList AnalysisService::notes() const
{
    QVariantList out;
    for (const QJsonValue &v : m_notes)
        out.append(v.toObject().toVariantMap());
    return out;
}

void AnalysisService::saveNote(const QString &text, const QString &moduleId)
{
    const QString id = paperId();
    if (id.isEmpty() || text.trimmed().isEmpty())
        return;
    QJsonObject note{
        {QStringLiteral("text"), text.trimmed()},
        {QStringLiteral("moduleId"), moduleId},
        {QStringLiteral("createdAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_notes.append(note);
    // Notes are the reader's, not the model's: they live in their own object
    // so regenerating an interpretation never touches them.
    m_store->putNote(id, QJsonObject{{QStringLiteral("notes"), m_notes}});
    emit notesChanged();
}

void AnalysisService::removeNote(int index)
{
    if (index < 0 || index >= m_notes.size())
        return;
    m_notes.removeAt(index);
    m_store->putNote(paperId(),
                     QJsonObject{{QStringLiteral("notes"), m_notes}});
    emit notesChanged();
}
