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

AnalysisService::AnalysisService(Settings *settings, PaperController *paper,
                                 AnalysisStore *store,
                                 ProjectProfileController *profile,
                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_store(store)
    , m_profile(profile)
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
    return m_settings->analysisConfigured();
}

QString AnalysisService::modelInUse() const
{
    return m_settings ? m_settings->analysisModelInUse() : QString();
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
    clearQuick();
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

    if (!m_client)
        m_client = m_settings->createAnalysisClient(this);
    else {
        // Settings may have moved since the client was made.
        delete m_client;
        m_client = m_settings->createAnalysisClient(this);
    }

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

void AnalysisService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit stateChanged();
}
