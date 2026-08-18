#include "PdfSelectionModel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfLinkModel>
#include <QPdfSelection>
#include <QTextBoundaryFinder>
#include <QUrl>
#include <algorithm>
#include <limits>

namespace {

bool isHyphenLike(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x002D    // HYPHEN-MINUS
        || u == 0x2010    // HYPHEN
        || u == 0x2011;   // NON-BREAKING HYPHEN
}

// Same normalization BlockClusterer applies: drop non-printables
// (soft hyphens, zero-width joiners, PDFium's private-use glyphs),
// collapse any whitespace char to a plain space.
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

bool sameColumn(const QRectF &a, const QRectF &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    const qreal overlap = qMin(a.right(), b.right())
                        - qMax(a.left(), b.left());
    const qreal minWidth = qMin(a.width(), b.width());
    return minWidth > 0 && overlap > 0.5 * minWidth;
}

QRectF boundsRect(const QPdfSelection &sel)
{
    QRectF bb;
    const QList<QPolygonF> polys = sel.bounds();
    for (const QPolygonF &poly : polys)
        bb = bb.united(poly.boundingRect());
    return bb;
}

// Join PDF layout lines back into prose. `raw` may span pages (caller
// joins page substrings with '\n').
QString cleanText(const QString &raw)
{
    QString out;
    bool hyphenJoin = false;
    bool paraBreak = false;
    const QStringList lines = raw.split(QChar('\n'));
    for (const QString &lnRaw : lines) {
        QString t = lnRaw;
        if (t.endsWith(QChar('\r')))
            t.chop(1);
        t = t.trimmed();
        if (t.isEmpty()) {
            if (!out.isEmpty())
                paraBreak = true;
            continue;
        }
        bool hyph = false;
        const QChar last = t.at(t.size() - 1);
        if (isHyphenLike(last)) {
            t.chop(1);
            hyph = true;
        } else if (!last.isPrint() && !last.isSpace()) {
            // PDFium end-of-line private-use codepoint = hyphen glyph
            // from an embedded font; treat like a real hyphen.
            t.chop(1);
            hyph = true;
        }
        t = sanitize(t).trimmed();
        if (t.isEmpty())
            continue;
        if (!out.isEmpty()) {
            if (paraBreak)
                out += QStringLiteral("\n\n");
            else if (!hyphenJoin)
                out += QChar(' ');
        }
        out += t;
        hyphenJoin = hyph;
        paraBreak = false;
    }
    return out;
}

} // namespace

PdfSelectionModel::PdfSelectionModel(QPdfDocument *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
    connect(m_doc, &QPdfDocument::statusChanged,
            this, [this] { reset(); });
}

PdfSelectionModel::~PdfSelectionModel()
{
    qDeleteAll(m_links);
}

bool PdfSelectionModel::docReady() const
{
    return m_doc && m_doc->status() == QPdfDocument::Status::Ready;
}

void PdfSelectionModel::reset()
{
    m_pages.clear();
    qDeleteAll(m_links);
    m_links.clear();
    m_grain = CharGrain;
    m_anchorStart = m_anchorEnd = m_focusStart = m_focusEnd = TextPos();
    emit selectionChanged();
}

PdfSelectionModel::PageData &PdfSelectionModel::pageData(int page) const
{
    if (m_pages.size() != m_doc->pageCount())
        m_pages.resize(m_doc->pageCount());
    PageData &pd = m_pages[page];
    if (pd.loaded)
        return pd;
    pd.loaded = true;
    pd.text = m_doc->getAllText(page).text();
    const int n = pd.text.size();
    int lineStart = 0;
    for (int i = 0; i <= n; ++i) {
        if (i < n && pd.text.at(i) != QChar('\n'))
            continue;
        int end = i;
        if (end > lineStart && pd.text.at(end - 1) == QChar('\r'))
            --end;
        appendLines(page, lineStart, end, pd);
        lineStart = i + 1;
    }
    return pd;
}

// One '\r\n'-delimited range of page text is NOT guaranteed to be one
// visual line: PDFium only inserts breaks where its heuristics fire,
// so a range can span several rendered lines (or even a column jump).
// bounds() gives one polygon per rendered baseline run — when there is
// more than one, split the range into per-polygon sub-lines by
// assigning each character to the nearest polygon. Getting this wrong
// made hit-testing snap to the line above the click.
void PdfSelectionModel::appendLines(int page, int start, int end,
                                    PageData &pd) const
{
    if (end <= start) {
        LineInfo ln;
        ln.start = start;
        ln.end = end;
        pd.lines.append(ln);
        return;
    }
    const QList<QPolygonF> polys =
        m_doc->getSelectionAtIndex(page, start, end - start).bounds();
    if (polys.size() <= 1) {
        LineInfo ln;
        ln.start = start;
        ln.end = end;
        if (!polys.isEmpty())
            ln.bbox = polys.first().boundingRect();
        pd.lines.append(ln);
        return;
    }

    QVector<QRectF> rects;
    rects.reserve(polys.size());
    for (const QPolygonF &p : polys)
        rects.append(p.boundingRect());

    int g = 0;           // rect the current sub-line belongs to
    int segStart = start;
    for (int i = start; i < end; ++i) {
        const QRectF bb = boundsRect(m_doc->getSelectionAtIndex(page, i, 1));
        if (bb.isNull() || bb.height() <= 0)
            continue;    // space / synthesized char: stays in current run
        // Nearest rect at or after the current one — text order is
        // monotonic through the rects, but a single range can span
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
            LineInfo ln;
            ln.start = segStart;
            ln.end = i;
            ln.bbox = rects[g];
            pd.lines.append(ln);
            segStart = i;
        }
        g = bestJ;
    }
    LineInfo ln;
    ln.start = segStart;
    ln.end = end;
    ln.bbox = rects[g];
    pd.lines.append(ln);
}

QList<QRectF> PdfSelectionModel::debugLineRects(int page) const
{
    QList<QRectF> out;
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return out;
    const PageData &pd = pageData(page);
    for (const LineInfo &ln : pd.lines)
        if (!ln.bbox.isEmpty())
            out.append(ln.bbox);
    return out;
}

// Nearest text line to `pos`. Lines containing the point (with a small
// margin) always win; otherwise vertical distance dominates, so a drag
// through the margin selects the line beside it, like a browser.
const PdfSelectionModel::LineInfo *
PdfSelectionModel::lineAt(int page, QPointF pos, bool *inside) const
{
    const PageData &pd = pageData(page);
    const LineInfo *best = nullptr;
    qreal bestScore = std::numeric_limits<qreal>::max();
    bool bestInside = false;
    for (const LineInfo &ln : pd.lines) {
        if (ln.bbox.isEmpty())
            continue;
        const QRectF r = ln.bbox.adjusted(-2, -1, 2, 1);
        qreal dx = 0, dy = 0;
        if (pos.x() < r.left())       dx = r.left() - pos.x();
        else if (pos.x() > r.right()) dx = pos.x() - r.right();
        if (pos.y() < r.top())         dy = r.top() - pos.y();
        else if (pos.y() > r.bottom()) dy = pos.y() - r.bottom();
        const bool ins = dx == 0 && dy == 0;
        const qreal score = ins
            ? -1e6 + qAbs(pos.y() - ln.bbox.center().y())
            : dy * 4 + dx;
        if (score < bestScore) {
            bestScore = score;
            best = &ln;
            bestInside = ins;
        }
    }
    if (inside)
        *inside = bestInside;
    return best;
}

void PdfSelectionModel::ensureCarets(int page, const LineInfo &ln) const
{
    if (!ln.carets.isEmpty() || ln.end <= ln.start)
        return;
    const int n = ln.end - ln.start;
    ln.carets.resize(n + 1);
    qreal last = ln.bbox.left();
    for (int k = 0; k < n; ++k) {
        const QRectF bb = boundsRect(
            m_doc->getSelectionAtIndex(page, ln.start + k, 1));
        if (!bb.isNull() && bb.width() > 0) {
            ln.carets[k] = bb.left();
            last = bb.right();
        } else {
            // Space / synthesized char without a glyph box: continue
            // from the previous char's right edge.
            ln.carets[k] = last;
        }
    }
    ln.carets[n] = last;
    for (int k = 1; k <= n; ++k)
        if (ln.carets[k] < ln.carets[k - 1])
            ln.carets[k] = ln.carets[k - 1];
}

PdfSelectionModel::TextPos
PdfSelectionModel::posAt(int page, QPointF pos, bool *insideText) const
{
    TextPos p;
    p.page = page;
    p.idx = 0;
    bool inside = false;
    const LineInfo *ln = lineAt(page, pos, &inside);
    if (ln) {
        ensureCarets(page, *ln);
        // Nearest caret boundary to pos.x — carets[] is monotonic.
        const int n = ln->end - ln->start;
        int k = int(std::lower_bound(ln->carets.begin(), ln->carets.end(),
                                     pos.x())
                    - ln->carets.begin());
        if (k > 0 && (k > n
                      || pos.x() - ln->carets[k - 1]
                         < ln->carets[qMin(k, n)] - pos.x()))
            --k;
        p.idx = ln->start + qBound(0, k, n);
    }
    if (insideText)
        *insideText = inside;
    return p;
}

int PdfSelectionModel::lineIndexOf(const PageData &pd, int charIdx) const
{
    for (int i = 0; i < pd.lines.size(); ++i) {
        const LineInfo &ln = pd.lines[i];
        if (charIdx >= ln.start && charIdx <= ln.end)
            return i;
    }
    return -1;
}

void PdfSelectionModel::wordRange(const TextPos &pos,
                                  TextPos &s, TextPos &e) const
{
    s = e = pos;
    const PageData &pd = pageData(pos.page);
    if (pd.text.isEmpty())
        return;
    int i = qBound(0, pos.idx, pd.text.size() - 1);
    // Double-clicking the gap between words should select a word, not
    // the invisible whitespace run: snap to the nearest non-space char,
    // staying within the clicked line.
    const int li = lineIndexOf(pd, i);
    const int lo = li >= 0 ? pd.lines[li].start : 0;
    const int hi = li >= 0 ? qMax(pd.lines[li].end - 1, lo)
                           : int(pd.text.size()) - 1;
    i = qBound(lo, i, hi);
    if (pd.text.at(i).isSpace()) {
        int j = i;
        while (j > lo && pd.text.at(j).isSpace())
            --j;
        if (pd.text.at(j).isSpace()) {
            j = i;
            while (j < hi && pd.text.at(j).isSpace())
                ++j;
        }
        i = j;
    }
    QTextBoundaryFinder f(QTextBoundaryFinder::Word, pd.text);
    f.setPosition(i);
    if (!f.isAtBoundary())
        f.toPreviousBoundary();
    s.idx = qMax(0, f.position());
    f.setPosition(i);
    const int end = f.toNextBoundary();
    e.idx = end < 0 ? pd.text.size() : end;
}

void PdfSelectionModel::setParagraphs(const QVector<Block> &blocks)
{
    m_paragraphs.clear();
    for (const Block &b : blocks)
        if (!b.bbox.isEmpty())
            m_paragraphs[b.page].append(b.bbox);
}

void PdfSelectionModel::paraRange(const TextPos &pos,
                                  TextPos &s, TextPos &e) const
{
    s = e = pos;
    const PageData &pd = pageData(pos.page);
    const int li = lineIndexOf(pd, pos.idx);
    if (li < 0)
        return;
    const QRectF lineR = pd.lines[li].bbox;

    // Preferred: the app's own paragraph rectangles. Triple-click then
    // selects exactly what the reading pane shows as one paragraph —
    // far more reliable than line-gap heuristics on formula-heavy or
    // unevenly-leaded text.
    if (!lineR.isEmpty()) {
        const QPointF c = lineR.center();
        QRectF best;
        const QVector<QRectF> paras = m_paragraphs.value(pos.page);
        for (const QRectF &r : paras) {
            if (!r.adjusted(-2, -2, 2, 2).contains(c))
                continue;
            if (best.isNull()
                || r.width() * r.height() < best.width() * best.height())
                best = r;
        }
        if (!best.isNull()) {
            const QRectF probe = best.adjusted(-2, -2, 2, 2);
            int a = -1, b = -1;
            for (int i = 0; i < pd.lines.size(); ++i) {
                const LineInfo &ln = pd.lines[i];
                if (ln.bbox.isEmpty() || !probe.contains(ln.bbox.center()))
                    continue;
                if (a < 0)
                    a = i;
                b = i;
            }
            if (a >= 0 && li >= a && li <= b) {
                s.idx = pd.lines[a].start;
                e.idx = pd.lines[b].end;
                return;
            }
        }
    }

    // Fallback (no block covers this line): join adjacent lines that
    // share a column and sit one leading apart. Center-to-center
    // distance is used instead of edge gaps — inline math inflates a
    // line's bbox and made edge-gap checks split paragraphs mid-way.
    auto joinable = [](const LineInfo &above, const LineInfo &below) {
        if (above.end <= above.start || below.end <= below.start)
            return false;
        if (!sameColumn(above.bbox, below.bbox))
            return false;
        const qreal minH = qMin(above.bbox.height(), below.bbox.height());
        const qreal maxH = qMax(above.bbox.height(), below.bbox.height());
        if (maxH - minH > 0.6 * maxH)
            return false;
        const qreal dy = below.bbox.center().y() - above.bbox.center().y();
        return dy > 0.2 * minH && dy < 1.9 * minH;
    };

    int a = li, b = li;
    while (a > 0 && joinable(pd.lines[a - 1], pd.lines[a]))
        --a;
    while (b + 1 < pd.lines.size() && joinable(pd.lines[b], pd.lines[b + 1]))
        ++b;
    s.idx = pd.lines[a].start;
    e.idx = pd.lines[b].end;
}

void PdfSelectionModel::beginAt(int page, QPointF pagePos, int clickCount)
{
    if (!docReady() || page < 0 || page >= m_doc->pageCount()) {
        clear();
        return;
    }
    m_grain = clickCount >= 3 ? ParaGrain
            : clickCount == 2 ? WordGrain
                              : CharGrain;
    bool inside = false;
    const TextPos p = posAt(page, pagePos, &inside);
    TextPos s = p, e = p;
    if (m_grain != CharGrain && inside) {
        if (m_grain == WordGrain)
            wordRange(p, s, e);
        else
            paraRange(p, s, e);
    } else if (m_grain != CharGrain) {
        m_grain = CharGrain;   // multi-click in empty space: caret only
    }
    m_anchorStart = s;
    m_anchorEnd = e;
    m_focusStart = s;
    m_focusEnd = e;
    emit selectionChanged();
}

void PdfSelectionModel::extendTo(int page, QPointF pagePos)
{
    if (!docReady() || !m_anchorStart.valid()
        || page < 0 || page >= m_doc->pageCount())
        return;
    bool inside = false;
    const TextPos p = posAt(page, pagePos, &inside);
    TextPos s = p, e = p;
    if (m_grain == WordGrain && inside)
        wordRange(p, s, e);
    else if (m_grain == ParaGrain && inside)
        paraRange(p, s, e);
    if (s == m_focusStart && e == m_focusEnd)
        return;
    m_focusStart = s;
    m_focusEnd = e;
    emit selectionChanged();
}

void PdfSelectionModel::selectAllOnPage(int page)
{
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return;
    const PageData &pd = pageData(page);
    m_grain = CharGrain;
    m_anchorStart = m_focusStart = TextPos{page, 0};
    m_anchorEnd = m_focusEnd = TextPos{page, int(pd.text.size())};
    emit selectionChanged();
}

void PdfSelectionModel::clear()
{
    if (!m_anchorStart.valid() && !m_focusStart.valid())
        return;
    m_anchorStart = m_anchorEnd = m_focusStart = m_focusEnd = TextPos();
    emit selectionChanged();
}

bool PdfSelectionModel::overText(int page, QPointF pagePos) const
{
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return false;
    bool inside = false;
    lineAt(page, pagePos, &inside);
    return inside;
}

PdfSelectionModel::TextPos PdfSelectionModel::selStart() const
{
    return qMin(m_anchorStart, m_focusStart);
}

PdfSelectionModel::TextPos PdfSelectionModel::selEnd() const
{
    return qMax(m_anchorEnd, m_focusEnd);
}

bool PdfSelectionModel::hasSelection() const
{
    return docReady() && m_anchorStart.valid() && selStart() < selEnd();
}

int PdfSelectionModel::startPage() const
{
    return hasSelection() ? selStart().page : -1;
}

bool PdfSelectionModel::pageRange(int page, int *from, int *to) const
{
    if (!hasSelection())
        return false;
    const TextPos s = selStart();
    const TextPos e = selEnd();
    if (page < s.page || page > e.page)
        return false;
    const PageData &pd = pageData(page);
    *from = page == s.page ? s.idx : 0;
    *to = page == e.page ? e.idx : int(pd.text.size());
    return *to > *from;
}

QList<QPolygonF> PdfSelectionModel::polygonsOnPage(int page) const
{
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return {};
    int from = 0, to = 0;
    if (!pageRange(page, &from, &to))
        return {};
    return m_doc->getSelectionAtIndex(page, from, to - from).bounds();
}

QString PdfSelectionModel::text() const
{
    if (!hasSelection())
        return {};
    const TextPos s = selStart();
    const TextPos e = selEnd();
    QString raw;
    for (int p = s.page; p <= e.page; ++p) {
        int from = 0, to = 0;
        if (!pageRange(p, &from, &to))
            continue;
        if (!raw.isEmpty())
            raw += QChar('\n');
        raw += pageData(p).text.mid(from, to - from);
    }
    return cleanText(raw);
}

void PdfSelectionModel::copyToClipboard() const
{
    const QString t = text();
    if (!t.isEmpty())
        QGuiApplication::clipboard()->setText(t);
}

QVariantMap PdfSelectionModel::linkAt(int page, QPointF pagePos) const
{
    QVariantMap out;
    out.insert(QStringLiteral("found"), false);
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return out;
    QPdfLinkModel *lm = m_links.value(page);
    if (!lm) {
        lm = new QPdfLinkModel;
        lm->setDocument(m_doc);
        lm->setPage(page);
        m_links.insert(page, lm);
    }
    const QPdfLink link = lm->linkAt(pagePos);
    if (!link.isValid())
        return out;
    out.insert(QStringLiteral("found"), true);
    out.insert(QStringLiteral("page"), link.page());
    out.insert(QStringLiteral("location"), link.location());
    out.insert(QStringLiteral("zoom"), link.zoom());
    out.insert(QStringLiteral("url"), link.url());
    return out;
}
