#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QString>

// Prompts and JSON Schemas for the interpretation layer, kept together
// because they are two halves of one contract: the schema is what the
// model is forced to fill in, the prompt is why each field exists.
//
// Every schema built here shares one rule -- a statement is a `claim`
// object carrying its own provenance code and the block ids it rests on
// (§4.3) -- because that is the only shape EvidenceIndex can check and the
// only shape the UI can render honestly.
//
// promptVersion() is baked into every stored result's inputHash, so
// editing a prompt here marks existing interpretations as out of date
// rather than leaving them silently inconsistent (§17).
namespace AnalysisPrompts {

QString promptVersion();

// The name to ask the model to answer in, from a Settings::targetLang code.
QString languageName(const QString &code);

// A single statement + where it comes from. Reused everywhere.
QJsonObject claimSchema(const QString &description);
QJsonObject claimArraySchema(const QString &description);

// ── §2 quick interpretation: the digest every other feature reads ────
QJsonObject quickDigestSchema();
QString quickSystem(const QString &lang, const QString &profileBlock);
QString quickUser(const QString &title, const QString &paperText,
                  bool truncated);

// ── §3 deep read: one module at a time ───────────────────────────────
// Nine modules, nine calls. One call cannot write §3.1 through §3.9 well and
// would run out of output tokens trying; separate calls also give §5's
// "regenerate just this part" for free, and let a partial run still be worth
// something.
QJsonObject deepModuleSchema(const QString &moduleId);
QString deepSystem(const QString &lang, const QString &profileBlock,
                   const QString &moduleId);
QString deepUser(const QString &title, const QString &paperText, bool truncated,
                 const QJsonObject &digest);

// ── §10 comparing papers the reader picked ───────────────────────────
// Reads the digests, not the papers: that is what makes twelve papers one
// call. The schema forces the comparability warnings to be part of the
// answer rather than an afterthought (§10.3).
QJsonObject compareSchema();
QString compareSystem(const QString &lang, const QString &profileBlock);
QString compareUser(const QJsonArray &digests, const QStringList &notes);

// A digest cut down to what a cross-paper analysis needs. Everything
// library-level runs on these, never on paper text.
QJsonObject digestBrief(const QString &paperId, const QString &title,
                        const QJsonObject &digest);

// ── §8–§15: everything that reads the whole project ──────────────────
// One schema and one prompt per kind, all of them fed the same thing: the
// digests, never the papers. That is the only reason a fifty-paper project
// fits in one call.
QJsonObject librarySchema(const QString &kind);
QString librarySystem(const QString &kind, const QString &lang,
                      const QString &profileBlock);
QString libraryUser(const QString &kind, const QJsonArray &briefs,
                    const QJsonObject &extra = QJsonObject());

} // namespace AnalysisPrompts
