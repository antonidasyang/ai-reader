#pragma once

#include "Block.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

// Turns the paragraph model into something the LLM can cite, and then
// checks what it cited.
//
// §4.4 of the requirement says the system must never invent page
// numbers, results or references. Asking the model nicely does not achieve
// that, so the paper is fed in with every paragraph carrying its own id and
// page -- "[b12 p3] The proposed method ..." -- the schema requires every
// claim to name the block ids it rests on, and verify() then re-reads those
// paragraphs and confirms the quoted words are really there. A citation
// that fails is marked unverified, and a claim that was labelled as the
// authors' or as an experimental result but has nothing verified behind it
// is demoted to plain AI analysis. Only checked citations ever render as
// evidence.
namespace EvidenceIndex {

struct RenderResult {
    QString text;
    bool truncated = false;
    int blocksIncluded = 0;
    int blocksTotal = 0;
};

// The whole paper (or as much of it as fits in maxChars) as citable text.
RenderResult render(const QVector<Block> &blocks, int maxChars);

// The paper split into chunks of at most maxCharsPerChunk, broken on
// heading boundaries where possible. Used for the map half of the
// map-reduce path a long paper needs.
QVector<QString> renderChunks(const QVector<Block> &blocks,
                              int maxCharsPerChunk);

// Just the section headings, one per line, with their block ids: cheap
// context for prompts that don't need the body text.
QString renderOutline(const QVector<Block> &blocks);

// Whether `quote` really appears in `blockText`. Whitespace, case,
// hyphenation and punctuation are normalised away first, and a long quote
// that only mostly matches (the model paraphrasing across a line break)
// still counts.
bool quoteMatches(const QString &quote, const QString &blockText);

struct VerifyStats {
    int total = 0;      // evidence entries seen
    int verified = 0;   // …that check out
    int repaired = 0;   // …whose block id was off by a paragraph or two
    int demoted = 0;    // claims knocked down to ai_analysis
};

// Walks a model result and annotates every evidence entry with
// `verified` + `page` + `ord`, repairing near-miss block ids, then demotes
// unsupported author/experimental claims. Returns the annotated copy.
QJsonObject verify(const QJsonObject &result, const QVector<Block> &blocks,
                   VerifyStats *stats = nullptr);

} // namespace EvidenceIndex
