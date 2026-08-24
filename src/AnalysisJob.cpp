#include "AnalysisJob.h"

#include "AnalysisPrompts.h"
#include "AnalysisTypes.h"
#include "LlmClient.h"
#include "StructuredLlm.h"

#include <QJsonObject>
#include <QTimer>

QuickAnalysisJob::QuickAnalysisJob(const Input &in, QObject *parent)
    : QObject(parent)
    , m_in(in)
{
}

QuickAnalysisJob *QuickAnalysisJob::start(LlmClient *client, const Input &in,
                                          QObject *parent)
{
    auto *job = new QuickAnalysisJob(in, parent);
    QTimer::singleShot(0, job, [job, client]() {
        if (!job->m_done)
            job->run(client);
    });
    return job;
}

void QuickAnalysisJob::abort()
{
    if (m_done)
        return;
    m_done = true;
    if (m_call)
        m_call->abort();
    deleteLater();
}

void QuickAnalysisJob::run(LlmClient *client)
{
    const EvidenceIndex::RenderResult rendered =
        EvidenceIndex::render(m_in.blocks, m_in.contextChars);
    m_contentHash = Analysis::contentHashOfText(rendered.text);
    m_truncated = rendered.truncated;
    m_blocksIncluded = rendered.blocksIncluded;

    // Below this there is nothing to read: a scanned PDF with no text
    // layer, or a paper that was never segmented. Saying so is the honest
    // answer, and §17 has a state for it.
    if (rendered.text.size() < 400) {
        finishErr(tr("This paper has too little extracted text to interpret. "
                     "Segment it first, or check that the PDF has a text "
                     "layer."));
        return;
    }

    StructuredCall::Request req;
    req.system = AnalysisPrompts::quickSystem(m_in.lang, m_in.profileBlock);
    req.user = AnalysisPrompts::quickUser(m_in.title, rendered.text,
                                          rendered.truncated);
    req.schema = AnalysisPrompts::quickDigestSchema();
    req.toolName = QStringLiteral("emit_interpretation");
    req.toolDescription =
        QStringLiteral("Return the interpretation of this paper.");
    req.maxTokens = m_in.maxTokens;
    req.temperature = 0.1;

    m_call = StructuredCall::start(client, req, this);
    connect(m_call, &StructuredCall::succeeded, this,
            [this](const QJsonObject &raw) {
                EvidenceIndex::VerifyStats stats;
                QJsonObject digest =
                    EvidenceIndex::verify(raw, m_in.blocks, &stats);
                digest.insert(
                    QStringLiteral("meta"),
                    QJsonObject{
                        {QStringLiteral("truncated"), m_truncated},
                        {QStringLiteral("blocksIncluded"), m_blocksIncluded},
                        {QStringLiteral("blocksTotal"), int(m_in.blocks.size())},
                        {QStringLiteral("evidenceTotal"), stats.total},
                        {QStringLiteral("evidenceVerified"), stats.verified},
                        {QStringLiteral("evidenceRepaired"), stats.repaired},
                        {QStringLiteral("claimsDemoted"), stats.demoted}});
                finishOk(digest);
            });
    connect(m_call, &StructuredCall::failed, this,
            [this](const QString &e) { finishErr(e); });
}

void QuickAnalysisJob::finishOk(const QJsonObject &digest)
{
    if (m_done)
        return;
    m_done = true;
    emit succeeded(digest);
    deleteLater();
}

void QuickAnalysisJob::finishErr(const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit failed(error);
    deleteLater();
}
