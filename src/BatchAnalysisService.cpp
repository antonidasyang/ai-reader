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
    m_source->cancel();
    m_sourceBusy = false;
    setStatus(tr("Cancelled."));
    emit progressChanged();
    finishIfIdle();
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

    if (!m_client)
        m_client = m_settings->createClient(this);

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
    emit progressChanged();
    setStatus(tr("Interpreting %1…").arg(title));

    auto *job = QuickAnalysisJob::start(m_client, in, this);
    connect(job, &QuickAnalysisJob::succeeded, this,
            [this, itemId, paperId, title, job](const QJsonObject &digest) {
                --m_running;
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
            [this, itemId](const QString &error) {
                --m_running;
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
    emit statusChanged();
}
