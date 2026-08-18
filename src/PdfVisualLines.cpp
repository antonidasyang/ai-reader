#include "PdfVisualLines.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <limits>

namespace {

QRectF boundsRect(const QPdfSelection &sel)
{
    QRectF bb;
    const QList<QPolygonF> polys = sel.bounds();
    for (const QPolygonF &poly : polys)
        bb = bb.united(poly.boundingRect());
    return bb;
}

void appendLines(QPdfDocument &doc, int page, int start, int end,
                 QVector<PdfVisualLines::Line> &out)
{
    if (end <= start) {
        PdfVisualLines::Line ln;
        ln.start = start;
        ln.end = end;
        out.append(ln);
        return;
    }
    const QList<QPolygonF> polys =
        doc.getSelectionAtIndex(page, start, end - start).bounds();
    if (polys.size() <= 1) {
        PdfVisualLines::Line ln;
        ln.start = start;
        ln.end = end;
        if (!polys.isEmpty())
            ln.bbox = polys.first().boundingRect();
        out.append(ln);
        return;
    }

    QVector<QRectF> rects;
    rects.reserve(polys.size());
    for (const QPolygonF &p : polys)
        rects.append(p.boundingRect());

    int g = 0;           // rect the current sub-line belongs to
    int segStart = start;
    for (int i = start; i < end; ++i) {
        const QRectF bb = boundsRect(doc.getSelectionAtIndex(page, i, 1));
        if (bb.isNull() || bb.height() <= 0)
            continue;    // space / synthesized char: stays in current run
        // Nearest rect at or after the current one — text order is
        // monotonic through the rects, and a single range can span
        // dozens of segments (poster-style pages), so no lookahead cap.
        int bestJ = g;
        qreal best = std::numeric_limits<qreal>::max();
        for (int j = g; j < rects.size(); ++j) {
            qreal d = qAbs(bb.center().y() - rects[j].center().y());
            if (bb.center().x() < rects[j].left() - 5
                || bb.center().x() > rects[j].right() + 5)
                d += 50;
            if (d < best) {
                best = d;
                bestJ = j;
            }
        }
        if (bestJ != g && i > segStart) {
            PdfVisualLines::Line ln;
            ln.start = segStart;
            ln.end = i;
            ln.bbox = rects[g];
            out.append(ln);
            segStart = i;
        }
        g = bestJ;
    }
    PdfVisualLines::Line ln;
    ln.start = segStart;
    ln.end = end;
    ln.bbox = rects[g];
    out.append(ln);
}

} // namespace

QVector<PdfVisualLines::Line>
PdfVisualLines::extract(QPdfDocument &doc, int page, QString *pageText)
{
    QVector<Line> out;
    const QString text = doc.getAllText(page).text();
    if (pageText)
        *pageText = text;
    const int n = text.size();
    int lineStart = 0;
    for (int i = 0; i <= n; ++i) {
        if (i < n && text.at(i) != QChar('\n'))
            continue;
        int end = i;
        if (end > lineStart && text.at(end - 1) == QChar('\r'))
            --end;
        appendLines(doc, page, lineStart, end, out);
        lineStart = i + 1;
    }

    // Same-baseline merge: italic/bold emphasis words and sub/super-
    // scripts arrive as separate runs on one baseline ("The " +
    // "first " + "pass"), which fragmented paragraphs at every styled
    // word. Adjacent runs whose rects overlap vertically by half the
    // smaller height and sit within 14pt horizontally are one rendered
    // line; column gutters (~24pt+) stay split.
    QVector<Line> merged;
    merged.reserve(out.size());
    for (const Line &ln : out) {
        if (!merged.isEmpty()) {
            Line &prev = merged.last();
            if (!prev.bbox.isNull() && !ln.bbox.isNull()) {
                const qreal vOverlap =
                    qMin(prev.bbox.bottom(), ln.bbox.bottom())
                    - qMax(prev.bbox.top(), ln.bbox.top());
                const qreal minH =
                    qMin(prev.bbox.height(), ln.bbox.height());
                const qreal gap = ln.bbox.left() - prev.bbox.right();
                if (minH > 0 && vOverlap > 0.5 * minH && gap < 14) {
                    prev.end = ln.end;
                    prev.bbox = prev.bbox.united(ln.bbox);
                    continue;
                }
            }
        }
        merged.append(ln);
    }
    return merged;
}

bool PdfVisualLines::endsParagraph(const QString &t)
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
