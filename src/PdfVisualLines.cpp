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

void appendLines(QPdfDocument &doc, int page, const QString &text,
                 int start, int end, PdfVisualLines::Precision precision,
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

    // Split the range at estimated character boundaries: each rect's
    // share of the total width maps to a share of the characters, and
    // the estimate snaps to the nearest space/hyphen — line wraps
    // happen at exactly those characters. Pure arithmetic: no extra
    // PDFium calls (a per-character scan was ~40 s per dense paper,
    // per-polygon hit tests were fast but imprecise).
    qreal totalW = 0;
    for (const QRectF &r : rects)
        totalW += qMax<qreal>(1, r.width());

    auto isBreakAfter = [&text](int p) {
        const QChar c = text.at(p - 1);
        const ushort u = c.unicode();
        return c.isSpace() || u == 0x002D || u == 0x2010 || u == 0x2011
            || u == 0x00AD;
    };

    QVector<int> boundaries;
    boundaries.reserve(rects.size() + 1);
    boundaries.append(start);
    qreal cum = 0;
    for (int j = 0; j + 1 < rects.size(); ++j) {
        cum += qMax<qreal>(1, rects[j].width());
        const int lo = boundaries.last() + 1;
        const int hi = end - 1;
        if (lo > hi) {
            // More rects than remaining characters (stray marks):
            // degenerate boundary, the empty segment is dropped below.
            boundaries.append(boundaries.last());
            continue;
        }
        int target = start + int(qRound((end - start) * cum / totalW));
        target = qBound(lo, target, hi);
        int best = target;
        int bestDist = 9;   // snap window: ±8 chars
        for (int d = 0; d <= 8; ++d) {
            for (int cand : {target - d, target + d}) {
                if (cand <= boundaries.last() || cand >= end)
                    continue;
                if (isBreakAfter(cand) && d < bestDist) {
                    best = cand;
                    bestDist = d;
                }
            }
            if (bestDist <= d)
                break;
        }
        if (precision == PdfVisualLines::Precise) {
            // Ask PDFium where the next polygon's text actually starts
            // and take it when it roughly agrees with the estimate —
            // exact boundaries, with the estimate as a sanity net
            // against tolerance mis-hits on dense pages.
            const QRectF &r = rects[j + 1];
            const QPdfSelection s = doc.getSelection(
                page,
                QPointF(r.left() + 0.5, r.center().y()),
                QPointF(qMin(r.left() + 6.0, r.right() - 0.5),
                        r.center().y()));
            const int hitB = s.startIndex();
            if (hitB > boundaries.last() && hitB < end
                && qAbs(hitB - best) <= 12)
                best = hitB;
        }
        boundaries.append(best);
    }
    boundaries.append(end);

    for (int j = 0; j < rects.size(); ++j) {
        const int s0 = boundaries[j];
        const int s1 = boundaries[j + 1];
        if (s1 <= s0)
            continue;
        PdfVisualLines::Line ln;
        ln.start = s0;
        ln.end = s1;
        ln.bbox = rects[j];
        out.append(ln);
    }
}

} // namespace

QVector<PdfVisualLines::Line>
PdfVisualLines::extract(QPdfDocument &doc, int page, QString *pageText,
                        Precision precision)
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
        appendLines(doc, page, text, lineStart, end, precision, out);
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
