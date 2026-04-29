#include "BlockClusterer.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <QPolygonF>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
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

bool sameColumn(const QRectF &a, const QRectF &b)
{
    if (a.isEmpty() || b.isEmpty()) return true;
    const qreal overlap = qMin(a.right(), b.right())
                        - qMax(a.left(), b.left());
    const qreal minWidth = qMin(a.width(), b.width());
    return minWidth > 0 && overlap > 0.5 * minWidth;
}

// Paragraph-terminating rule: did this line end with sentence-final
// punctuation? Trailing closing brackets/quotes are skipped first
// (so `…end.")` still counts). Gap-threshold heuristics didn't work
// reliably across PDFs; this is intentionally simple — over-merges
// can be fixed manually from the UI.
bool endsParagraph(const QString &t)
{
    int i = t.size() - 1;
    while (i >= 0) {
        const ushort u = t.at(i).unicode();
        const bool isCloser =
            u == ')' || u == ']' || u == '}' ||
            u == '"' || u == '\'' ||
            u == 0x201D /* ” */ || u == 0x2019 /* ’ */ ||
            u == 0xFF09 /* ） */ || u == 0xFF3D /* ］ */;
        if (!isCloser) break;
        --i;
    }
    if (i < 0) return false;
    const ushort u = t.at(i).unicode();
    return u == '.' || u == '?' || u == '!'
        || u == 0x3002 /* 。 */
        || u == 0xFF1F /* ？ */
        || u == 0xFF01 /* ！ */;
}

} // namespace

QVector<Block> BlockClusterer::extract(QPdfDocument &doc)
{
    QVector<Block> blocks;
    int nextId = 0;

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

        // Median line height is still useful for the heading classifier.
        QVector<double> heights;
        if (haveBboxes) {
            heights.reserve(lines.size());
            for (const Line &ln : lines) {
                if (ln.bbox.isEmpty() || ln.text.isEmpty()) continue;
                heights.append(ln.bbox.height());
            }
        }
        const double medianHeight = medianOf(heights);

        QString currentText;
        QRectF  currentBox;
        double  currentMaxHeight = 0;
        bool    prevHyphen = false;
        bool    flushBeforeNext = false;

        auto flush = [&]() {
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

        const Line *prev = nullptr;
        for (int i = 0; i < lines.size(); ++i) {
            const Line &ln = lines[i];

            if (ln.text.isEmpty()) {
                flush();
                prevHyphen = false;
                flushBeforeNext = false;
                prev = nullptr;
                continue;
            }

            bool startNew = currentText.isEmpty() || flushBeforeNext;
            if (!startNew && prev && haveBboxes
                && !prev->bbox.isEmpty() && !ln.bbox.isEmpty()) {
                const bool sameCol = sameColumn(prev->bbox, ln.bbox);
                const bool fontJump = medianHeight > 0
                    && qAbs(ln.bbox.height() - prev->bbox.height())
                       > medianHeight * 0.35;
                if (!sameCol || fontJump)
                    startNew = true;
            }

            if (startNew) {
                flush();
                currentText = ln.text;
                currentBox = ln.bbox;
            } else if (prevHyphen) {
                currentText += ln.text;
                if (!ln.bbox.isEmpty())
                    currentBox = currentBox.united(ln.bbox);
            } else {
                currentText += QChar(' ');
                currentText += ln.text;
                if (!ln.bbox.isEmpty())
                    currentBox = currentBox.united(ln.bbox);
            }
            if (!ln.bbox.isEmpty())
                currentMaxHeight = qMax(currentMaxHeight, ln.bbox.height());
            prevHyphen = ln.hyphenated;
            // The line's *visible* end-of-line punctuation drives the split:
            // a hyphenated continuation can't terminate a paragraph.
            flushBeforeNext = !ln.hyphenated && endsParagraph(ln.text);
            prev = &ln;
        }
        flush();
    }

    return blocks;
}

QString BlockClusterer::dumpDebug(QPdfDocument &doc)
{
    QString out;
    QTextStream ts(&out);

    for (int p = 0; p < doc.pageCount(); ++p) {
        QPdfSelection sel = doc.getAllText(p);
        const QString rawText = sel.text();
        const QStringList rawLines =
            rawText.split(QChar('\n'), Qt::KeepEmptyParts);
        const QList<QPolygonF> polys = sel.bounds();

        ts << "================================================================\n";
        ts << "Page " << p << "  rawLines=" << rawLines.size()
           << " polys=" << polys.size() << "\n";
        ts << "================================================================\n";
        ts << "--- Raw text from QPdfSelection.text() ---\n";
        ts << rawText;
        if (!rawText.endsWith(QChar('\n'))) ts << "\n";
        ts << "--- end raw text ---\n\n";

        const QVector<Line> lines = extractPageLines(sel);
        ts << "--- Lines after preprocess + poly alignment ---\n";
        ts << "  ¶ marks lines that end a paragraph; ¬ marks hyphenated.\n";
        for (int i = 0; i < lines.size(); ++i) {
            const Line &ln = lines[i];
            const QString bboxStr = ln.bbox.isEmpty()
                ? QStringLiteral("    (no-bbox)        ")
                : QString("(%1,%2,%3,%4)")
                    .arg(ln.bbox.x(),     7, 'f', 1)
                    .arg(ln.bbox.y(),     7, 'f', 1)
                    .arg(ln.bbox.width(), 6, 'f', 1)
                    .arg(ln.bbox.height(),5, 'f', 1);
            const bool ends = !ln.hyphenated && endsParagraph(ln.text);
            ts << QString("  [%1] ").arg(i, 3)
               << bboxStr
               << (ln.hyphenated ? " ¬" : "  ")
               << (ends ? " ¶" : "  ")
               << " | " << ln.text << "\n";
        }
        ts << "\n";
    }

    ts << "================================================================\n";
    ts << "Final blocks produced by extract()\n";
    ts << "================================================================\n";
    const QVector<Block> blocks = extract(doc);
    for (const Block &b : blocks) {
        const char *kindStr =
            b.kind == Block::Heading   ? "heading"   :
            b.kind == Block::Caption   ? "caption"   :
            b.kind == Block::ListItem  ? "list"      :
            b.kind == Block::Equation  ? "equation"  : "paragraph";
        ts << "[#" << b.id << "] page=" << b.page
           << " kind=" << kindStr
           << " bbox=(" << b.bbox.x() << "," << b.bbox.y()
           << "," << b.bbox.width() << "," << b.bbox.height() << ")"
           << " chars=" << b.text.length() << "\n";
        ts << b.text << "\n\n";
    }

    return out;
}
