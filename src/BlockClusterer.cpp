#include "BlockClusterer.h"

#include <QDebug>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPolygonF>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

// ─── Heading vocabulary / classifier ──────────────────────────────────────

const QSet<QString> &commonHeadings()
{
    static const QSet<QString> set = {
        QStringLiteral("Abstract"),
        QStringLiteral("Introduction"),
        QStringLiteral("Background"),
        QStringLiteral("Related Work"),
        QStringLiteral("Methods"),
        QStringLiteral("Method"),
        QStringLiteral("Methodology"),
        QStringLiteral("Approach"),
        QStringLiteral("Experiments"),
        QStringLiteral("Evaluation"),
        QStringLiteral("Results"),
        QStringLiteral("Discussion"),
        QStringLiteral("Conclusion"),
        QStringLiteral("Conclusions"),
        QStringLiteral("Limitations"),
        QStringLiteral("Future Work"),
        QStringLiteral("References"),
        QStringLiteral("Acknowledgments"),
        QStringLiteral("Acknowledgements"),
        QStringLiteral("Appendix"),
    };
    return set;
}

Block::Kind classify(const QString &text,
                     qreal lineHeight,
                     qreal medianHeight)
{
    static const QRegularExpression captionRx(
        QStringLiteral("^(Figure|Fig\\.|Table|Tab\\.)\\s*\\d"));
    static const QRegularExpression numberedHeading(
        QStringLiteral("^\\d+(\\.\\d+){0,3}\\s+[A-Z]"));
    static const QRegularExpression romanHeading(
        QStringLiteral("^(I|II|III|IV|V|VI|VII|VIII|IX|X|XI|XII)\\.\\s+[A-Z]"));

    if (captionRx.match(text).hasMatch())
        return Block::Caption;

    if (text.length() < 200) {
        if (numberedHeading.match(text).hasMatch())
            return Block::Heading;
        if (romanHeading.match(text).hasMatch())
            return Block::Heading;
        if (commonHeadings().contains(text.trimmed()))
            return Block::Heading;
        // Geometric hint: short text in a noticeably larger font.
        if (medianHeight > 0
            && lineHeight > medianHeight * 1.18
            && text.length() < 120) {
            return Block::Heading;
        }
    }

    return Block::Paragraph;
}

// ─── Line-level text/character normalization ──────────────────────────────

bool isHyphenLike(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x002D    // HYPHEN-MINUS
        || u == 0x2010    // HYPHEN
        || u == 0x2011;   // NON-BREAKING HYPHEN
}

// QChar::isPrint() is false for Cc (control), Cf (format — incl. soft hyphen,
// zero-width joiners, BOM), Cs (surrogate), Co (private use), Cn (unassigned).
// Drop those entirely; normalize any whitespace to a plain space.
QString sanitize(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isSpace()) {
            out.append(QChar(' '));
            continue;
        }
        if (!c.isPrint())
            continue;
        out.append(c);
    }
    return out;
}

struct Line {
    QString text;
    QRectF  bbox;
    bool    hyphenated = false;
};

// PDFium often emits a private-use codepoint at end-of-line where a word is
// hyphenated across lines (the PDF's embedded font defined a hyphen glyph
// there; no system font can render it). Treat ANY non-printable trailing
// char the same as a real hyphen: chop it and mark the line as hyphenated.
Line preprocessLine(QString raw, const QRectF &bbox)
{
    Line ln;
    ln.bbox = bbox;
    raw = raw.trimmed();
    if (raw.isEmpty())
        return ln;

    QChar last = raw.at(raw.size() - 1);
    if (isHyphenLike(last)) {
        raw.chop(1);
        ln.hyphenated = true;
    } else if (!last.isPrint() && !last.isSpace()) {
        raw.chop(1);
        ln.hyphenated = true;
    }
    ln.text = sanitize(raw).trimmed();
    return ln;
}

// ─── Page → lines ─────────────────────────────────────────────────────────

QVector<Line> extractPageLines(const QPdfSelection &sel)
{
    QVector<Line> out;
    const QString text = sel.text();
    if (text.isEmpty())
        return out;

    const QStringList rawLines =
        text.split(QChar('\n'), Qt::KeepEmptyParts);
    const QList<QPolygonF> polys = sel.bounds();

    // QPdfSelection::bounds() returns one polygon per *visible* line, but
    // sel.text() typically ends with a trailing '\n' (so split produces
    // one extra empty entry) and blank vertical space can also produce
    // empty rawLines without a matching poly. Strict count-match used to
    // drop the whole page to text-only mode in those cases — which is
    // why pages came out as one mega-block. Align polys with the non-
    // empty raw lines instead: each non-empty rawLine[i] gets polys[k++],
    // empty rawLines stay bbox-less. Falls through to text-only only
    // when the *non-empty* counts still don't match.
    QVector<int> nonEmptyIdx;
    nonEmptyIdx.reserve(rawLines.size());
    for (int i = 0; i < rawLines.size(); ++i)
        if (!rawLines[i].trimmed().isEmpty())
            nonEmptyIdx.append(i);

    out.reserve(rawLines.size());
    if (!polys.isEmpty() && polys.size() == nonEmptyIdx.size()) {
        int p = 0;
        for (int i = 0; i < rawLines.size(); ++i) {
            QRectF bbox;
            if (p < nonEmptyIdx.size() && i == nonEmptyIdx[p]) {
                bbox = polys[p].boundingRect();
                ++p;
            }
            out.append(preprocessLine(rawLines[i], bbox));
        }
    } else {
        for (const QString &raw : rawLines)
            out.append(preprocessLine(raw, QRectF()));
    }
    return out;
}

// ─── Statistics ───────────────────────────────────────────────────────────

double medianOf(QVector<double> v)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// 25th-percentile gap. Used as the intra-line baseline instead of the
// median: once a column has more than a couple of short paragraphs,
// the median climbs into the no-man's-land between intra-line and
// inter-paragraph gaps and the threshold sits *above* the real
// inter-paragraph gaps — so paragraph breaks never fire. The lower
// quartile stays in the intra-line cluster as long as paragraphs
// average more than ~one line, which holds for body text.
double lowerQuartileOf(QVector<double> v)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 4];
}

bool sameColumn(const QRectF &a, const QRectF &b)
{
    if (a.isEmpty() || b.isEmpty()) return true;
    const qreal overlap = qMin(a.right(), b.right())
                        - qMax(a.left(), b.left());
    const qreal minWidth = qMin(a.width(), b.width());
    return minWidth > 0 && overlap > 0.5 * minWidth;
}

} // namespace

QVector<Block> BlockClusterer::extract(QPdfDocument &doc)
{
    QVector<Block> blocks;
    int nextId = 0;

    // Env-gated diagnostic. Set AIREADER_DEBUG_BLOCKS=1 and rerun: per
    // page we'll print poly/line counts and the gap distribution so we
    // can see whether the bbox path even runs and what threshold it
    // ends up using. No-op in normal use.
    const bool debugDump = qEnvironmentVariableIntValue("AIREADER_DEBUG_BLOCKS") != 0;

    for (int p = 0; p < doc.pageCount(); ++p) {
        QPdfSelection sel = doc.getAllText(p);
        const QVector<Line> lines = extractPageLines(sel);
        if (lines.isEmpty())
            continue;

        // If no bbox info is available for this page, fall back to the old
        // text-only behavior: paragraphs are separated by blank lines.
        const bool haveBboxes = std::any_of(
            lines.begin(), lines.end(),
            [](const Line &l) { return !l.bbox.isEmpty(); });

        QString currentText;
        QRectF  currentBox;
        double  currentMaxHeight = 0;
        bool    prevHyphen = false;

        auto flush = [&](double medianHeight) {
            if (currentText.isEmpty()) return;
            Block b;
            b.id = nextId;
            b.ord = nextId;
            b.page = p;
            b.text = currentText.trimmed();
            b.bbox = currentBox;
            b.kind = classify(b.text, currentMaxHeight, medianHeight);
            blocks.append(b);
            ++nextId;
            currentText.clear();
            currentBox = QRectF();
            currentMaxHeight = 0;
        };

        if (!haveBboxes) {
            for (const Line &ln : lines) {
                if (ln.text.isEmpty()) {
                    flush(0);
                    prevHyphen = false;
                    continue;
                }
                if (currentText.isEmpty()) {
                    currentText = ln.text;
                } else if (prevHyphen) {
                    currentText += ln.text;
                } else {
                    currentText += QChar(' ');
                    currentText += ln.text;
                }
                prevHyphen = ln.hyphenated;
            }
            flush(0);
            continue;
        }

        // Geometric path: compute typical line height, width, and intra-
        // paragraph gap, then split whenever consecutive lines suggest a
        // new paragraph (large vertical gap, column change, font jump).
        QVector<double> heights, widths, gaps;
        heights.reserve(lines.size());
        widths.reserve(lines.size());
        gaps.reserve(lines.size());
        for (const Line &ln : lines) {
            if (ln.bbox.isEmpty() || ln.text.isEmpty()) continue;
            heights.append(ln.bbox.height());
            widths.append(ln.bbox.width());
        }
        for (int i = 1; i < lines.size(); ++i) {
            const Line &a = lines[i - 1];
            const Line &b = lines[i];
            if (a.bbox.isEmpty() || b.bbox.isEmpty()) continue;
            if (!sameColumn(a.bbox, b.bbox)) continue;
            const qreal gap = b.bbox.top() - a.bbox.bottom();
            if (gap >= 0) gaps.append(gap);
        }
        const double medianHeight = medianOf(heights);
        const double tightGap     = lowerQuartileOf(gaps);

        if (debugDump) {
            QVector<double> sortedGaps = gaps;
            std::sort(sortedGaps.begin(), sortedGaps.end());
            qDebug().nospace()
                << "[blocks] page=" << p
                << " rawLines=" << QString::number(lines.size())
                << " bboxes=" << (haveBboxes ? "yes" : "no")
                << " medianHeight=" << medianHeight
                << " tightGap=" << tightGap
                << " medianGap=" << medianOf(gaps)
                << " threshold=" << qMax(tightGap * 1.4, medianHeight * 0.4)
                << " sortedGaps=" << sortedGaps;
        }

        const Line *prev = nullptr;
        for (int i = 0; i < lines.size(); ++i) {
            const Line &ln = lines[i];
            if (ln.text.isEmpty()) {
                flush(medianHeight);
                prevHyphen = false;
                prev = nullptr;
                continue;
            }

            bool startNew = currentText.isEmpty();
            if (!startNew && prev) {
                const bool sameCol = sameColumn(prev->bbox, ln.bbox);
                const qreal vgap   = ln.bbox.top() - prev->bbox.bottom();
                // Tight intra-line baseline × 1.4 catches paragraph
                // breaks even on pages where many gaps are inter-
                // paragraph (median would have drifted up). Floor at
                // 0.4 × line-height so we still split on visible
                // whitespace when intra-line gaps are near zero.
                const qreal gapThreshold = qMax(tightGap * 1.4,
                                                medianHeight * 0.4);
                const bool fontJump = medianHeight > 0
                    && qAbs(ln.bbox.height() - prev->bbox.height())
                       > medianHeight * 0.35;
                if (!sameCol || vgap > gapThreshold || fontJump)
                    startNew = true;
            }

            if (startNew) {
                flush(medianHeight);
                currentText = ln.text;
                currentBox = ln.bbox;
            } else if (prevHyphen) {
                currentText += ln.text;
                currentBox = currentBox.united(ln.bbox);
            } else {
                currentText += QChar(' ');
                currentText += ln.text;
                currentBox = currentBox.united(ln.bbox);
            }
            currentMaxHeight = qMax(currentMaxHeight, ln.bbox.height());
            prevHyphen = ln.hyphenated;
            prev = &ln;
        }
        flush(medianHeight);
    }

    return blocks;
}
