#pragma once

#include <QString>
#include <QStringList>

// Shared vocabulary for the interpretation layer (§2–§15 of
// docs/需求-论文解读与库级分析.md): the sync object types it stores, the
// analysis kinds inside them, the provenance code every generated claim
// carries, and the deterministic object ids that keep exactly one row per
// (project, paper, kind, author) — the same keying AiArtifactService and
// PaperSyncService already use, so a member's second machine writes to
// their own row instead of a duplicate.
namespace Analysis {

// ── sync object types ────────────────────────────────────────────────
extern const QString TypePaperAnalysis;    // one paper, one member
extern const QString TypeLibraryAnalysis;  // whole project, shared
extern const QString TypeProjectProfile;   // the research profile (§6)
extern const QString TypeAnalysisNote;     // a member's own notes/edits

// ── per-paper analysis kinds ─────────────────────────────────────────
extern const QString KindQuick;   // §2 quick interpretation → the digest
extern const QString KindDeep;    // §3 deep read, module by module

// ── library-level analysis kinds ─────────────────────────────────────
extern const QString KindTaxonomy;       // §8
extern const QString KindMap;            // §9
extern const QString KindCompare;        // §10 (keyed by scopeHash)
extern const QString KindConsensus;      // §11
extern const QString KindEvolution;      // §12
extern const QString KindCoverage;       // §13
extern const QString KindOpportunities;  // §14
extern const QString KindActions;        // §15

// ── provenance of a single claim (§4.3) ──────────────────────────────
// Everything the model emits carries one of these. The evidence checker
// demotes the first two to AiAnalysis when nothing in the paper backs
// them up, which is what keeps §4.4 honest.
extern const QString SourceAuthorClaim;
extern const QString SourceExperimental;
extern const QString SourceAiAnalysis;
extern const QString SourceSpeculation;
QString sourceLabel(const QString &code);

// ── reading advice (§2.2) ────────────────────────────────────────────
extern const QString AdviceReadFull;
extern const QString AdviceMethodExperiments;
extern const QString AdviceBackground;
extern const QString AdviceLowRelevance;
extern const QString AdviceInsufficient;
QString adviceLabel(const QString &code);

// ── relevance to the current project (§2.1) ──────────────────────────
// high | medium | low | unclear
QString relevanceLabel(const QString &code);

// ── analysis status (§17) ────────────────────────────────────────────
extern const QString StatusOk;
extern const QString StatusFailed;
extern const QString StatusInsufficient;   // the PDF didn't carry enough text

// ── deep-read modules (§3.1 … §3.9), in display order ────────────────
QStringList deepModules();
QString deepModuleTitle(const QString &id);
QString libraryKindTitle(const QString &kind);

// ── deterministic object ids ─────────────────────────────────────────
QString paperAnalysisId(const QString &projectId, const QString &paperId,
                        const QString &kind, const QString &author);
QString libraryAnalysisId(const QString &projectId, const QString &kind,
                          const QString &scopeHash = QString());
QString projectProfileId(const QString &projectId);
QString noteId(const QString &projectId, const QString &paperId,
               const QString &author);

// sha1 over the sorted paper ids — the key an ad-hoc comparison is filed
// under, so re-comparing the same set updates one row instead of piling
// up near-identical ones (§10.1).
QString scopeHash(QStringList paperIds);

// Hash of everything a stored result depends on. When the paragraphs, the
// prompt, the research profile or the model move, the stored analysis is
// stale — which is exactly the "分析结果可能过期" state of §17.
QString inputHash(const QString &contentHash, const QString &promptVersion,
                  const QString &profileHash, const QString &model);

// Stable digest of a paper's paragraph text, for the contentHash above.
QString contentHashOfText(const QString &text);

} // namespace Analysis
