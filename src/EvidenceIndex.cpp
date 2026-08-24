#include "EvidenceIndex.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

namespace {

// Case-folded, punctuation-free, whitespace-free form. Comparing in this
// space makes the check immune to the line breaks, hyphenation, ligatures
// and quote characters that the PDF extractor and the model each mangle
// in their own way.
QString squash(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c.isLetterOrNumber())
            out.append(c.toLower());
    }
    return out;
}

// Words worth matching on: 4+ letters, lowercased. Short function words
// would match anything.
QStringList wordsOf(const QString &s)
{
    QStringList out;
    QString cur;
    for (const QChar c : s) {
        if (c.isLetterOrNumber()) {
            cur.append(c.toLower());
        } else {
            if (cur.size() >= 4)
                out.append(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 4)
        out.append(cur);
    return out;
}

QString lineFor(const Block &b)
{
    const QString tag = QStringLiteral("[b%1 p%2] ")
                            .arg(b.id)
                            .arg(b.page + 1);
    switch (b.kind) {
    case Block::Heading:
        return QStringLiteral("\n## ") + tag + b.text + QChar('\n');
    case Block::Caption:
        return tag + QStringLiteral("(figure/table caption) ") + b.text
               + QChar('\n');
    case Block::Equation:
        return tag + QStringLiteral("(equation) ") + b.text + QChar('\n');
    default:
        return tag + b.text + QChar('\n');
    }
}

struct Ctx {
    const QVector<Block> *blocks = nullptr;
    QHash<int, int> index;      // block id -> position in *blocks
    EvidenceIndex::VerifyStats *stats = nullptr;
};

QJsonValue walkValue(const QJsonValue &v, Ctx &ctx);

// Checks one claim's evidence list, repairing block ids that are off by a
// paragraph or two (models routinely cite the neighbour) and marking what
// could not be found at all.
QJsonArray verifyEvidence(const QJsonArray &in, Ctx &ctx, int *verifiedCount)
{
    QJsonArray out;
    const QVector<Block> &blocks = *ctx.blocks;
    for (const QJsonValue &ev : in) {
        if (!ev.isObject())
            continue;
        QJsonObject e = ev.toObject();
        const QString quote = e.value(QStringLiteral("quote")).toString();
        const QJsonValue idVal = e.value(QStringLiteral("blockId"));
        const int wantId = idVal.isString() ? idVal.toString().toInt()
                                            : idVal.toInt(-1);
        if (ctx.stats)
            ++ctx.stats->total;

        int found = -1;                      // position in blocks
        const int start = ctx.index.value(wantId, -1);
        if (start >= 0 && EvidenceIndex::quoteMatches(quote, blocks[start].text))
            found = start;

        // The neighbourhood first: a citation that lands one paragraph off
        // is a near miss worth fixing, not a fabrication.
        if (found < 0 && start >= 0) {
            for (int d = 1; d <= 3 && found < 0; ++d) {
                for (const int p : {start - d, start + d}) {
                    if (p < 0 || p >= blocks.size())
                        continue;
                    if (EvidenceIndex::quoteMatches(quote, blocks[p].text)) {
                        found = p;
                        break;
                    }
                }
            }
        }
        // Then the whole paper, but only for a quote long enough that a
        // match means something.
        if (found < 0 && squash(quote).size() >= 16) {
            for (int p = 0; p < blocks.size(); ++p) {
                if (EvidenceIndex::quoteMatches(quote, blocks[p].text)) {
                    found = p;
                    break;
                }
            }
        }

        if (found >= 0) {
            const Block &b = blocks[found];
            if (b.id != wantId) {
                e[QStringLiteral("blockId")] = b.id;
                e[QStringLiteral("repairedFrom")] = wantId;
                if (ctx.stats)
                    ++ctx.stats->repaired;
            }
            e[QStringLiteral("verified")] = true;
            e[QStringLiteral("page")] = b.page + 1;
            e[QStringLiteral("ord")] = b.ord;
            if (ctx.stats)
                ++ctx.stats->verified;
            if (verifiedCount)
                ++(*verifiedCount);
        } else {
            e[QStringLiteral("verified")] = false;
            e.remove(QStringLiteral("page"));
            if (start >= 0) {
                // The paragraph exists; the words don't come from it.
                e[QStringLiteral("ord")] = blocks[start].ord;
                e[QStringLiteral("page")] = blocks[start].page + 1;
            }
        }
        out.append(e);
    }
    return out;
}

QJsonObject walkObject(const QJsonObject &in, Ctx &ctx)
{
    QJsonObject o;
    int verified = 0;
    bool sawEvidence = false;
    for (auto it = in.begin(); it != in.end(); ++it) {
        if (it.key() == QLatin1String("evidence") && it.value().isArray()) {
            sawEvidence = true;
            o.insert(it.key(), verifyEvidence(it.value().toArray(), ctx,
                                              &verified));
        } else {
            o.insert(it.key(), walkValue(it.value(), ctx));
        }
    }

    // §4.3/§4.4: a statement presented as the authors' own or as an
    // experimental result has to be traceable to the paper. With nothing
    // verified behind it, it is the model's own reading — say so rather
    // than let it borrow the authors' authority.
    const QString src = o.value(QStringLiteral("source")).toString();
    if ((src == QLatin1String("author_claim")
         || src == QLatin1String("experimental"))
        && verified == 0) {
        o[QStringLiteral("source")] = QStringLiteral("ai_analysis");
        o[QStringLiteral("originalSource")] = src;
        o[QStringLiteral("unsupported")] = true;
        if (!sawEvidence)
            o[QStringLiteral("evidence")] = QJsonArray();
        if (ctx.stats)
            ++ctx.stats->demoted;
    }
    return o;
}

QJsonValue walkValue(const QJsonValue &v, Ctx &ctx)
{
    if (v.isObject())
        return walkObject(v.toObject(), ctx);
    if (v.isArray()) {
        QJsonArray out;
        for (const QJsonValue &e : v.toArray())
            out.append(walkValue(e, ctx));
        return out;
    }
    return v;
}

} // namespace

namespace EvidenceIndex {

RenderResult render(const QVector<Block> &blocks, int maxChars)
{
    RenderResult r;
    r.blocksTotal = blocks.size();
    for (const Block &b : blocks) {
        if (b.text.trimmed().isEmpty())
            continue;
        const QString line = lineFor(b);
        if (maxChars > 0 && r.text.size() + line.size() > maxChars) {
            r.truncated = true;
            break;
        }
        r.text += line;
        ++r.blocksIncluded;
    }
    return r;
}

QVector<QString> renderChunks(const QVector<Block> &blocks,
                              int maxCharsPerChunk)
{
    QVector<QString> out;
    if (maxCharsPerChunk < 500)
        maxCharsPerChunk = 500;
    QString cur;
    for (const Block &b : blocks) {
        if (b.text.trimmed().isEmpty())
            continue;
        const QString line = lineFor(b);
        // Break before a heading once the chunk is mostly full, so a
        // section rarely straddles two chunks.
        const bool breakHere =
            !cur.isEmpty()
            && (cur.size() + line.size() > maxCharsPerChunk
                || (b.kind == Block::Heading
                    && cur.size() > maxCharsPerChunk * 3 / 5));
        if (breakHere) {
            out.append(cur);
            cur.clear();
        }
        cur += line;
    }
    if (!cur.isEmpty())
        out.append(cur);
    return out;
}

QString renderOutline(const QVector<Block> &blocks)
{
    QString out;
    for (const Block &b : blocks) {
        if (b.kind != Block::Heading)
            continue;
        out += QStringLiteral("[b%1 p%2] %3\n")
                   .arg(b.id)
                   .arg(b.page + 1)
                   .arg(b.text.trimmed());
    }
    return out;
}

bool quoteMatches(const QString &quote, const QString &blockText)
{
    const QString q = squash(quote);
    if (q.size() < 6)
        return false;                    // too short to mean anything
    const QString t = squash(blockText);
    if (t.isEmpty())
        return false;
    if (t.contains(q))
        return true;
    // A long quote that only half matches is still a real citation: the
    // model elides the middle of a sentence, or the extractor lost a
    // hyphen the model kept.
    if (q.size() >= 24) {
        if (t.contains(q.left(q.size() / 2)) || t.contains(q.right(q.size() / 2)))
            return true;
    }
    const QStringList w = wordsOf(quote);
    if (w.size() >= 3) {
        int hit = 0;
        for (const QString &word : w) {
            if (t.contains(word))
                ++hit;
        }
        return double(hit) / double(w.size()) >= 0.7;
    }
    return false;
}

QJsonObject verify(const QJsonObject &result, const QVector<Block> &blocks,
                   VerifyStats *stats)
{
    Ctx ctx;
    ctx.blocks = &blocks;
    ctx.stats = stats;
    for (int i = 0; i < blocks.size(); ++i)
        ctx.index.insert(blocks[i].id, i);
    return walkObject(result, ctx);
}

} // namespace EvidenceIndex
