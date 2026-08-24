#pragma once

#include <QJsonObject>
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

} // namespace AnalysisPrompts
