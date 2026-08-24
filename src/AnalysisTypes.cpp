#include "AnalysisTypes.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QUuid>

namespace {

// Third namespace in the family: ...0001 is AiArtifactService (shared AI
// output), ...0002 is PaperSyncService (segmentations / translations),
// ...0003 is everything in the interpretation layer. Fixed, so the same
// (project, paper, kind, author) always maps to the same object id on
// every machine.
const QUuid kNs =
    QUuid::fromString(QStringLiteral("{4a1f2e90-7b3c-4d6a-9f21-a1b2c3d40003}"));

QString v5(const QString &name)
{
    return QUuid::createUuidV5(kNs, name.toUtf8()).toString(QUuid::WithoutBraces);
}

QString sha1(const QString &s)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex());
}

} // namespace

namespace Analysis {

const QString TypePaperAnalysis   = QStringLiteral("paper_analysis");
const QString TypeLibraryAnalysis = QStringLiteral("library_analysis");
const QString TypeProjectProfile  = QStringLiteral("project_profile");
const QString TypeAnalysisNote    = QStringLiteral("analysis_note");
const QString TypeCompareBasket   = QStringLiteral("compare_basket");

const QString KindQuick = QStringLiteral("quick");
const QString KindDeep  = QStringLiteral("deep");

const QString KindTaxonomy      = QStringLiteral("taxonomy");
const QString KindMap           = QStringLiteral("map");
const QString KindCompare       = QStringLiteral("compare");
const QString KindConsensus     = QStringLiteral("consensus");
const QString KindEvolution     = QStringLiteral("evolution");
const QString KindCoverage      = QStringLiteral("coverage");
const QString KindOpportunities = QStringLiteral("opportunities");
const QString KindActions       = QStringLiteral("actions");

const QString SourceAuthorClaim  = QStringLiteral("author_claim");
const QString SourceExperimental = QStringLiteral("experimental");
const QString SourceAiAnalysis   = QStringLiteral("ai_analysis");
const QString SourceSpeculation  = QStringLiteral("speculation");

const QString AdviceReadFull           = QStringLiteral("read_full");
const QString AdviceMethodExperiments  = QStringLiteral("read_method_experiments");
const QString AdviceBackground         = QStringLiteral("background");
const QString AdviceLowRelevance       = QStringLiteral("low_relevance");
const QString AdviceInsufficient       = QStringLiteral("insufficient");

const QString StatusOk           = QStringLiteral("ok");
const QString StatusFailed       = QStringLiteral("failed");
const QString StatusInsufficient = QStringLiteral("insufficient");

QString sourceLabel(const QString &code)
{
    if (code == SourceAuthorClaim)
        return QCoreApplication::translate("Analysis", "author's claim");
    if (code == SourceExperimental)
        return QCoreApplication::translate("Analysis", "supported by experiment");
    if (code == SourceSpeculation)
        return QCoreApplication::translate("Analysis", "speculation");
    return QCoreApplication::translate("Analysis", "AI analysis");
}

QString adviceLabel(const QString &code)
{
    if (code == AdviceReadFull)
        return QCoreApplication::translate("Analysis", "Read the whole paper");
    if (code == AdviceMethodExperiments)
        return QCoreApplication::translate("Analysis", "Read method + experiments");
    if (code == AdviceBackground)
        return QCoreApplication::translate("Analysis", "Use as background / related work");
    if (code == AdviceLowRelevance)
        return QCoreApplication::translate("Analysis", "Low relevance to this project");
    return QCoreApplication::translate("Analysis", "Not enough information — check by hand");
}

QString relevanceLabel(const QString &code)
{
    if (code == QLatin1String("high"))
        return QCoreApplication::translate("Analysis", "highly relevant");
    if (code == QLatin1String("medium"))
        return QCoreApplication::translate("Analysis", "somewhat relevant");
    if (code == QLatin1String("low"))
        return QCoreApplication::translate("Analysis", "barely relevant");
    return QCoreApplication::translate("Analysis", "relevance unclear");
}

QStringList deepModules()
{
    return {QStringLiteral("basics"),        // §3.1
            QStringLiteral("background"),    // §3.2
            QStringLiteral("method"),        // §3.3
            QStringLiteral("experiments"),   // §3.4
            QStringLiteral("contributions"), // §3.5
            QStringLiteral("critique"),      // §3.6
            QStringLiteral("limitations"),   // §3.7
            QStringLiteral("repro"),         // §3.8
            QStringLiteral("followups")};    // §3.9
}

QString deepModuleTitle(const QString &id)
{
    if (id == QLatin1String("basics"))
        return QCoreApplication::translate("Analysis", "What this paper is");
    if (id == QLatin1String("background"))
        return QCoreApplication::translate("Analysis", "Background & terminology");
    if (id == QLatin1String("method"))
        return QCoreApplication::translate("Analysis", "Method, step by step");
    if (id == QLatin1String("experiments"))
        return QCoreApplication::translate("Analysis", "Experiments");
    if (id == QLatin1String("contributions"))
        return QCoreApplication::translate("Analysis", "Contributions");
    if (id == QLatin1String("critique"))
        return QCoreApplication::translate("Analysis", "Critical reading");
    if (id == QLatin1String("limitations"))
        return QCoreApplication::translate("Analysis", "Limitations");
    if (id == QLatin1String("repro"))
        return QCoreApplication::translate("Analysis", "Reproducibility");
    if (id == QLatin1String("followups"))
        return QCoreApplication::translate("Analysis", "What to do next");
    return id;
}

QString libraryKindTitle(const QString &kind)
{
    if (kind == KindTaxonomy)
        return QCoreApplication::translate("Analysis", "Categories");
    if (kind == KindMap)
        return QCoreApplication::translate("Analysis", "Research map");
    if (kind == KindCompare)
        return QCoreApplication::translate("Analysis", "Comparison");
    if (kind == KindConsensus)
        return QCoreApplication::translate("Analysis", "Consensus & conflicts");
    if (kind == KindEvolution)
        return QCoreApplication::translate("Analysis", "How the field moved");
    if (kind == KindCoverage)
        return QCoreApplication::translate("Analysis", "Coverage of this library");
    if (kind == KindOpportunities)
        return QCoreApplication::translate("Analysis", "Candidate openings");
    if (kind == KindActions)
        return QCoreApplication::translate("Analysis", "What to read and try next");
    return kind;
}

QString paperAnalysisId(const QString &projectId, const QString &paperId,
                        const QString &kind, const QString &author)
{
    return v5(projectId + QChar('|') + paperId + QChar('|') + kind + QChar('|')
              + author);
}

QString libraryAnalysisId(const QString &projectId, const QString &kind,
                          const QString &scopeHash)
{
    QString name = projectId + QStringLiteral("|library|") + kind;
    if (!scopeHash.isEmpty())
        name += QChar('|') + scopeHash;
    return v5(name);
}

QString projectProfileId(const QString &projectId)
{
    return v5(projectId + QStringLiteral("|profile"));
}

QString noteId(const QString &projectId, const QString &paperId,
               const QString &author)
{
    return v5(projectId + QChar('|') + paperId + QStringLiteral("|note|")
              + author);
}

QString compareBasketId(const QString &projectId, const QString &author)
{
    return v5(projectId + QStringLiteral("|compare-basket|") + author);
}

QString scopeHash(QStringList paperIds)
{
    paperIds.removeAll(QString());
    paperIds.sort();
    paperIds.removeDuplicates();
    return sha1(paperIds.join(QChar(','))).left(16);
}

QString inputHash(const QString &contentHash, const QString &promptVersion,
                  const QString &profileHash, const QString &model)
{
    return sha1(contentHash + QChar('|') + promptVersion + QChar('|')
                + profileHash + QChar('|') + model)
        .left(16);
}

QString contentHashOfText(const QString &text)
{
    return sha1(text).left(16);
}

} // namespace Analysis
