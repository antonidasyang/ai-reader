#include "CompareService.h"
#include "Stall.h"

#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LlmClient.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "StructuredLlm.h"
#include "TaskManager.h"
#include "TaskTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QTimer>
#include <QVariantMap>

CompareService::CompareService(Settings *settings, AnalysisStore *store,
                               ProjectController *projects,
                               ProjectProfileController *profile,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_projects(projects)
    , m_profile(profile)
    , m_clients(settings, this)
{
    connect(m_projects, &ProjectController::currentChanged, this, [this]() {
        // A comparison in flight belongs to the project it was started in;
        // its answer would otherwise be filed under whichever project the
        // reader switched to.
        cancel();
        m_basket.clear();
        m_result = QJsonObject();
        load();
        emit basketChanged();
        emit resultChanged();
        emit stateChanged();
    });
    connect(m_store, &AnalysisStore::changed, this, [this]() {
        // A basket filled on another machine lands here with the sync.
        const int before = m_basket.size();
        load();
        if (m_basket.size() != before)
            emit basketChanged();
        emit stateChanged();
    });
    // Whether it can run depends on there being a model to run on, and the
    // button that asks used to find out only on the next store change.
    connect(m_settings, &Settings::configurationChanged, this,
            &CompareService::stateChanged);
    load();
}

void CompareService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A task stopped while it was still queued never calls its stop callback
    // -- there was nothing to stop -- so this is the only word the service
    // gets that the id it is holding is dead.
    connect(m_tasks, &TaskManager::taskFinished, this,
            [this](const QString &id, bool, const QString &) {
                if (id.isEmpty() || id != m_taskId)
                    return;
                m_taskId.clear();
                abortCall();
                emit stateChanged();
            });

    // The basket is stored with the project, so a comparison the last
    // session was still waiting for only means anything if the reader is
    // back in that project with the same papers picked.
    m_tasks->registerResumer(
        Tasks::Kind::Compare, [this](const QJsonObject &resume) {
            if (resume.value(QStringLiteral("projectId")).toString()
                != m_store->projectId())
                return false;
            if (resume.value(QStringLiteral("scope")).toString() != scopeKey())
                return false;
            if (busy() || !canRun())
                return false;
            compare();
            return busy();
        });
}

// ── the basket ───────────────────────────────────────────────────────

QVariantList CompareService::basket() const
{
    QVariantList out;
    for (const Entry &e : m_basket) {
        out.append(QVariantMap{{QStringLiteral("paperId"), e.paperId},
                               {QStringLiteral("title"), e.title},
                               {QStringLiteral("notes"), e.notes}});
    }
    return out;
}

void CompareService::add(const QString &paperId, const QString &title,
                         const QString &note)
{
    if (paperId.isEmpty())
        return;
    for (Entry &e : m_basket) {
        if (e.paperId != paperId)
            continue;
        if (!note.trimmed().isEmpty() && !e.notes.contains(note.trimmed()))
            e.notes.append(note.trimmed());
        save();
        emit basketChanged();
        return;
    }
    Entry e;
    e.paperId = paperId;
    e.title = title;
    if (!note.trimmed().isEmpty())
        e.notes.append(note.trimmed());
    m_basket.append(e);
    save();
    emit basketChanged();
    // canRun moved with the basket. The store's own change signal used to
    // be relied on for this, and a store that cannot be written to -- a
    // project the reader may only view -- never sent one, which left the
    // Compare button dead with two papers ticked.
    emit stateChanged();
}

void CompareService::removePaper(const QString &paperId)
{
    for (int i = 0; i < m_basket.size(); ++i) {
        if (m_basket.at(i).paperId == paperId) {
            m_basket.removeAt(i);
            save();
            emit basketChanged();
            emit stateChanged();
            return;
        }
    }
}

bool CompareService::contains(const QString &paperId) const
{
    for (const Entry &e : m_basket) {
        if (e.paperId == paperId)
            return true;
    }
    return false;
}

void CompareService::clearBasket()
{
    if (m_basket.isEmpty())
        return;
    m_basket.clear();
    save();
    emit basketChanged();
    emit stateChanged();
}

QString CompareService::scopeKey() const
{
    QStringList ids;
    for (const Entry &e : m_basket)
        ids.append(e.paperId);
    return Analysis::scopeHash(ids);
}

void CompareService::load()
{
    m_basket.clear();
    const QString project = m_projects->currentId();
    if (project.isEmpty())
        return;

    QJsonArray arr = m_store->compareBasket();
    if (arr.isEmpty()) {
        // Migration: the basket used to live in this machine's settings file,
        // where it never reached the reader's other machines. Take it over
        // once, then let the synced object own it.
        const QByteArray raw =
            m_qs.value(QStringLiteral("compare/") + project).toByteArray();
        arr = QJsonDocument::fromJson(raw).array();
        if (!arr.isEmpty())
            m_store->putCompareBasket(arr);
        m_qs.remove(QStringLiteral("compare/") + project);
    }

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.paperId = o.value(QStringLiteral("paperId")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        for (const QJsonValue &n : o.value(QStringLiteral("notes")).toArray())
            e.notes.append(n.toString());
        if (!e.paperId.isEmpty())
            m_basket.append(e);
    }
    loadStored();
}

void CompareService::save()
{
    QJsonArray arr;
    for (const Entry &e : m_basket) {
        QJsonArray notes;
        for (const QString &n : e.notes)
            notes.append(n);
        arr.append(QJsonObject{{QStringLiteral("paperId"), e.paperId},
                               {QStringLiteral("title"), e.title},
                               {QStringLiteral("notes"), notes}});
    }
    m_store->putCompareBasket(arr);
}

// ── the comparison ───────────────────────────────────────────────────

QString CompareService::blocker() const
{
    if (!m_settings || !m_settings->isConfigured())
        return tr("No model is configured — add one in Settings first.");
    if (m_basket.size() < 2)
        return QString();   // the dialog counts these itself
    // Every paper in the basket needs an interpretation: the comparison is
    // built out of those, not out of the PDFs. One that gave up -- too
    // little text, or the model failed -- has nothing to put in a cell.
    int missing = 0;
    for (const Entry &e : m_basket) {
        const AnalysisRecord rec =
            m_store->paperAnalysis(e.paperId, Analysis::KindQuick);
        if (!rec.valid || rec.status == Analysis::StatusInsufficient
            || rec.status == Analysis::StatusFailed || rec.payload.isEmpty())
            ++missing;
    }
    if (missing == 1)
        return tr("One of the picked papers has no interpretation yet.");
    if (missing > 1)
        return tr("%1 of the picked papers have no interpretation yet.")
            .arg(missing);
    return QString();
}

bool CompareService::canRun() const
{
    return m_basket.size() >= 2 && blocker().isEmpty();
}

void CompareService::loadStored()
{
    Stall::Mark mark("loading the comparison basket");
    if (m_basket.size() < 2) {
        if (!m_result.isEmpty()) {
            m_result = QJsonObject();
            emit resultChanged();
        }
        return;
    }
    const AnalysisRecord rec =
        m_store->libraryAnalysis(Analysis::KindCompare, scopeKey());
    const QJsonObject payload = rec.valid ? rec.payload : QJsonObject();
    if (payload == m_result)
        return;
    m_result = payload;
    m_resultAuthor = rec.authorEmail;
    m_resultUpdatedAt = rec.updatedAt;
    emit resultChanged();
}

void CompareService::compare()
{
    if (busy())
        return;
    if (!canRun()) {
        setError(m_basket.size() < 2
                     ? tr("Put at least two papers in the comparison.")
                     : blocker());
        return;
    }
    setError(QString());
    setReceived(0);

    if (!m_tasks) {
        startCall();
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::Compare;
    req.title = tr("Compare papers");
    req.projectId = m_store->projectId();
    const QString scope = scopeKey();
    // Project work, not one paper's: the key names the project and the set
    // of papers, so the same comparison is never asked for twice at once.
    req.exclusiveKey = QStringLiteral("compare|") + req.projectId + QChar('|')
                       + scope;
    req.steps = 1;
    req.resume = QJsonObject{{QStringLiteral("projectId"), req.projectId},
                             {QStringLiteral("scope"), scope}};

    const QString project = req.projectId;
    const QString id = m_tasks->submit(
        req,
        // submit() may call this before it returns; the hop through the
        // event loop keeps the id ahead of anything that reports against it,
        // and is where a task that waited out a project switch is dropped.
        [this, project, scope] {
            QTimer::singleShot(0, this, [this, project, scope] {
                if (m_taskId.isEmpty())
                    return;   // cancelled while it waited
                if (project != m_store->projectId() || scope != scopeKey()) {
                    // The reader is somewhere else now, or changed the set
                    // while it waited; nothing went wrong here.
                    cancelTask();
                    emit stateChanged();
                    return;
                }
                startCall();
            });
        },
        [this] {
            // The pane's stop button. The row is already marked; only the
            // call is ours to end.
            m_taskId.clear();
            abortCall();
            emit stateChanged();
        });
    if (id.isEmpty()) {
        // The manager already holds this exact comparison. It cannot be
        // ours -- busy() would have said so -- so say what happened rather
        // than answering the click with nothing.
        setError(tr("A comparison of these papers is already running."));
        return;
    }
    m_taskId = id;
    emit stateChanged();
}

void CompareService::startCall()
{
    if (m_call)
        return;

    QJsonArray digests;
    QStringList notes;
    for (const Entry &e : m_basket) {
        const AnalysisRecord rec =
            m_store->paperAnalysis(e.paperId, Analysis::KindQuick);
        const QString title =
            e.title.isEmpty() ? rec.title : e.title;
        digests.append(
            AnalysisPrompts::digestBrief(e.paperId, title, rec.payload));
        for (const QString &n : e.notes)
            notes.append(QStringLiteral("%1 — %2").arg(title, n));
    }

    // Rebuilt when the model configuration moved (a comparison is a single call).
    m_client = m_clients.client();

    StructuredCall::Request req;
    req.system = AnalysisPrompts::compareSystem(
        AnalysisPrompts::languageName(m_settings->targetLang()),
        m_profile->promptBlock());
    req.user = AnalysisPrompts::compareUser(digests, notes);
    req.schema = AnalysisPrompts::compareSchema();
    req.toolName = QStringLiteral("emit_comparison");
    req.toolDescription = QStringLiteral("Return the comparison table.");
    req.maxTokens = m_settings->analysisMaxTokens();
    req.temperature = 0.1;

    const QString scope = scopeKey();
    const int count = m_basket.size();
    m_call = StructuredCall::start(m_client, req, this);
    if (m_tasks && !m_taskId.isEmpty()) {
        m_tasks->setProgress(m_taskId, 0, 1);
        m_tasks->setNote(m_taskId,
                         tr("%1 interpretations sent; waiting for the model")
                             .arg(count));
    }
    emit stateChanged();

    connect(m_call, &StructuredCall::progress, this,
            [this](qint64 bytes) { setReceived(bytes); });
    connect(m_call, &StructuredCall::succeeded, this,
            [this, scope](const QJsonObject &result) {
                m_call.clear();
                QJsonObject stored = result;
                QJsonArray papers;
                for (const Entry &e : m_basket) {
                    papers.append(
                        QJsonObject{{QStringLiteral("paperId"), e.paperId},
                                    {QStringLiteral("title"), e.title}});
                }
                stored.insert(QStringLiteral("papers"), papers);
                m_result = stored;
                m_resultAuthor = m_store->userEmail();
                m_store->putLibraryAnalysis(Analysis::KindCompare, scope,
                                            stored,
                                            m_settings->model(),
                                            scope, m_basket.size());
                m_resultUpdatedAt =
                    m_store->libraryAnalysis(Analysis::KindCompare, scope)
                        .updatedAt;
                finishTask(true);
                emit resultChanged();
                emit stateChanged();
            });
    connect(m_call, &StructuredCall::failed, this, [this](const QString &e) {
        m_call.clear();
        setError(e);
        finishTask(false, e);
        emit stateChanged();
    });
}

void CompareService::cancel()
{
    if (!busy())
        return;
    abortCall();
    cancelTask();
    emit stateChanged();
}

void CompareService::abortCall()
{
    if (!m_call)
        return;
    m_call->abort();
    m_call.clear();
}

void CompareService::finishTask(bool ok, const QString &error)
{
    // Taken out first: a cancel and an answer can arrive together.
    const QString id = m_taskId;
    m_taskId.clear();
    if (id.isEmpty() || !m_tasks)
        return;
    m_tasks->finish(id, ok, error);
}

void CompareService::cancelTask()
{
    const QString id = m_taskId;
    m_taskId.clear();
    if (id.isEmpty() || !m_tasks)
        return;
    m_tasks->markCanceled(id);
}

void CompareService::setReceived(qint64 bytes)
{
    if (bytes == m_receivedBytes)
        return;
    m_receivedBytes = bytes;
    emit progressChanged();
    if (m_tasks && !m_taskId.isEmpty() && bytes > 0) {
        // Rounded to the kilobyte, so the row is not repainted on every
        // packet. setNote() drops an unchanged line on its own.
        m_tasks->setNote(m_taskId,
                         tr("the model is writing — %1 received so far")
                             .arg(QLocale().formattedDataSize(bytes, 0)));
    }
}

void CompareService::setError(const QString &e)
{
    if (e == m_lastError)
        return;
    m_lastError = e;
    emit stateChanged();
}
