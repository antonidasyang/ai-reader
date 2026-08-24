#include "AnalysisService.h"

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

#include <QDateTime>

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
        emit quickChanged();
    });
    if (m_settings) {
        connect(m_settings, &Settings::analysisConfigChanged, this,
                &AnalysisService::stateChanged);
        connect(m_settings, &Settings::configurationChanged, this,
                &AnalysisService::stateChanged);
    }
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

void AnalysisService::onPaperChanged()
{
    const QString id = paperId();
    if (id == m_lastPaperId)
        return;                    // a paragraph edit, not a new paper
    m_lastPaperId = id;
    cancel();
    cancelDeep();
    clearQuick();
    clearDeep();
    m_contentHash.clear();
    emit paperChanged();
    reloadFromStore();
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
    emit deepChanged();
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
    emit quickChanged();
}

void AnalysisService::reloadFromStore()
{
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
                emit deepChanged();
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
    emit quickChanged();
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
                emit quickChanged();
            });
    connect(m_job, &QuickAnalysisJob::failed, this,
            [this, paperAtStart](const QString &error) {
                m_job.clear();
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
    if (m_status == Running)
        setStatus(Idle);
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
    m_client = m_clients.client(!m_job && m_deepInflight == 0);
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
    return m_deepBusy.contains(id);
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
    m_deepForce = force;
    const QJsonObject modules =
        m_deep.value(QStringLiteral("modules")).toObject();
    for (const QString &id : Analysis::deepModules()) {
        if (!force && modules.contains(id))
            continue;             // keep what is already written
        if (!m_deepQueue.contains(id) && !m_deepBusy.contains(id))
            m_deepQueue.append(id);
    }
    m_deepErrors.clear();
    emit deepChanged();
    pumpDeep();
}

void AnalysisService::regenerateModule(const QString &id)
{
    if (!canRun() || id.isEmpty())
        return;
    if (m_deepBusy.contains(id) || m_deepQueue.contains(id))
        return;
    m_deepErrors.remove(id);
    m_deepQueue.append(id);
    emit deepChanged();
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
    emit deepChanged();
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
    emit deepChanged();
}

void AnalysisService::startModule(const QString &id)
{
    if (!m_paper || !m_settings)
        return;
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

    auto *job = DeepModuleJob::start(m_client, in, this);
    connect(job, &DeepModuleJob::succeeded, this,
            [this, paperAtStart, job](const QString &moduleId,
                                      const QJsonObject &result) {
                --m_deepInflight;
                m_deepBusy.remove(moduleId);
                if (paperAtStart != paperId())
                    return;
                QJsonObject modules =
                    m_deep.value(QStringLiteral("modules")).toObject();
                modules.insert(moduleId, result);
                m_deep.insert(QStringLiteral("modules"), modules);
                m_contentHash = job->contentHash();
                persistDeep();
                emit deepChanged();
                pumpDeep();
            });
    connect(job, &DeepModuleJob::failed, this,
            [this, paperAtStart](const QString &moduleId,
                                 const QString &error) {
                --m_deepInflight;
                m_deepBusy.remove(moduleId);
                if (paperAtStart != paperId())
                    return;
                m_deepErrors.insert(moduleId, error);
                emit deepChanged();
                pumpDeep();
            });
    emit deepChanged();
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
