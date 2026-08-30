#include "PdfSelectionModel.h"

#include "PdfVisualLines.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfLinkModel>
#include <QPdfSelection>
#include <QTextBoundaryFinder>
#include <QThread>
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

PdfSelectionBuilder::~PdfSelectionBuilder() = default;

void PdfSelectionBuilder::open(const QString &path, const QString &password,
                               int generation)
{
    m_doc.reset();          // the old paper's parse is dead weight now
    m_path = path;
    m_password = password;
    m_generation = generation;
}

void PdfSelectionBuilder::build(int page, int generation)
{
    PdfSelection::PageBundle bundle;
    bundle.page = page;
    if (generation != m_generation || m_path.isEmpty()) {
        emit built(bundle, generation);
        return;
    }
    if (!m_doc) {
        auto doc = std::make_unique<QPdfDocument>();
        doc->setPassword(m_password);
        if (doc->load(m_path) != QPdfDocument::Error::None) {
            emit built(bundle, generation);
            return;
        }
        m_doc = std::move(doc);
    }
    if (page < 0 || page >= m_doc->pageCount()) {
        emit built(bundle, generation);
        return;
    }

    bundle.lines = PdfVisualLines::extract(*m_doc, page, &bundle.text,
                                           PdfVisualLines::Precise);
    QPdfLinkModel lm;
    lm.setDocument(m_doc.get());
    lm.setPage(page);
    const int rows = lm.rowCount(QModelIndex());
    for (int r = 0; r < rows; ++r) {
        const QPdfLink lk =
            lm.data(lm.index(r, 0), int(QPdfLinkModel::Role::Link))
                .value<QPdfLink>();
        for (const QRectF &rc : lk.rectangles())
            bundle.links.append(PdfSelection::LinkInfo{
                rc, lk.page(), lk.location(), lk.zoom(), lk.url()});
    }
    emit built(bundle, generation);
}

PdfSelectionModel::PdfSelectionModel(QPdfDocument *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
    qRegisterMetaType<PdfSelection::PageBundle>();
    m_builder = new PdfSelectionBuilder;
    m_builder->moveToThread(&m_builderThread);
    connect(&m_builderThread, &QThread::finished,
            m_builder, &QObject::deleteLater);
    connect(m_builder, &PdfSelectionBuilder::built,
            this, &PdfSelectionModel::onBuilt);
    m_builderThread.start();

    m_prefetchTimer.setSingleShot(true);
    m_prefetchTimer.setInterval(300);
    connect(&m_prefetchTimer, &QTimer::timeout,
            this, &PdfSelectionModel::startPrefetch);

    connect(m_doc, &QPdfDocument::statusChanged, this, [this] {
        reset();
        if (docReady()) {
            // Cover the opening screen; the view drives the rest through
            // prefetchAround() as the reader moves.
            prefetchAround(0);
        }
    });
}

PdfSelectionModel::~PdfSelectionModel()
{
    m_builderThread.quit();
    m_builderThread.wait();
}

void PdfSelectionModel::setSource(const QString &localPath,
                                  const QString &password)
{
    if (localPath == m_srcPath && password == m_srcPassword)
        return;
    m_srcPath = localPath;
    m_srcPassword = password;
    m_prefetchTimer.stop();
    m_prefetchPage = -1;
    // Everything queued belonged to the paper being left. The page the
    // builder is holding right now is allowed to finish -- it is one
    // page, and interrupting PDFium mid-page is not a thing -- but its
    // answer arrives stamped with the old generation and is dropped.
    ++m_buildGen;
    m_queue.clear();
    QMetaObject::invokeMethod(m_builder, "open", Qt::QueuedConnection,
                              Q_ARG(QString, m_srcPath),
                              Q_ARG(QString, m_srcPassword),
                              Q_ARG(int, m_buildGen));
}

void PdfSelectionModel::prefetchAround(int page, int radius)
{
    if (!docReady() || m_srcPath.isEmpty())
        return;
    // The reader moved, so whatever was queued around where they were is
    // no longer the most useful thing to build next.
    m_queue.clear();
    m_prefetchPage = page;
    m_prefetchRadius = radius;
    m_prefetchTimer.start();
}

void PdfSelectionModel::startPrefetch()
{
    if (!docReady() || m_srcPath.isEmpty() || m_prefetchPage < 0)
        return;
    enqueue(m_prefetchPage);
    for (int d = 1; d <= m_prefetchRadius; ++d) {
        enqueue(m_prefetchPage + d);
        enqueue(m_prefetchPage - d);
    }
    pump();
}

void PdfSelectionModel::enqueue(int page)
{
    if (page < 0 || page >= m_doc->pageCount())
        return;
    if (m_pages.size() != m_doc->pageCount())
        m_pages.resize(m_doc->pageCount());
    const PageData &pd = m_pages[page];
    if (pd.loaded && pd.linksLoaded)
        return;                       // a click already built this one
    if (!m_queue.contains(page))
        m_queue.append(page);
}

void PdfSelectionModel::pump()
{
    if (m_building || m_queue.isEmpty())
        return;
    const int page = m_queue.takeFirst();
    m_building = true;
    QMetaObject::invokeMethod(m_builder, "build", Qt::QueuedConnection,
                              Q_ARG(int, page), Q_ARG(int, m_buildGen));
}

void PdfSelectionModel::onBuilt(const PdfSelection::PageBundle &bundle,
                                int generation)
{
    m_building = false;
    // An answer about the paper the reader has already left.
    if (generation != m_buildGen || !docReady()) {
        pump();
        return;
    }
    const int page = bundle.page;
    if (page >= 0 && page < m_doc->pageCount()) {
        if (m_pages.size() != m_doc->pageCount())
            m_pages.resize(m_doc->pageCount());
        PageData &pd = m_pages[page];
        // Never overwrite a page a click already built on the GUI thread.
        if (!pd.loaded) {
            pd.text = bundle.text;
            pd.lines.clear();
            pd.lines.reserve(bundle.lines.size());
            for (const PdfVisualLines::Line &vl : bundle.lines) {
                LineInfo ln;
                ln.start = vl.start;
                ln.end = vl.end;
                ln.bbox = vl.bbox;
                pd.lines.append(ln);
            }
            pd.loaded = true;
        }
        if (!pd.linksLoaded) {
            m_pageLinks[page] = bundle.links;
            pd.linksLoaded = true;
        }
    }
    pump();
}

bool PdfSelectionModel::docReady() const
{
    return m_doc && m_doc->status() == QPdfDocument::Status::Ready;
}

void PdfSelectionModel::reset()
{
    m_pages.clear();
    m_pageLinks.clear();
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
    // Shared rendered-line extraction — the same splitting the
    // clusterer segments paragraphs with, so hit-testing and the
    // reading pane always agree on what "a line" is.
    const QVector<PdfVisualLines::Line> vls =
        PdfVisualLines::extract(*m_doc, page, &pd.text,
                                PdfVisualLines::Precise);
    pd.lines.reserve(vls.size());
    for (const PdfVisualLines::Line &vl : vls) {
        LineInfo ln;
        ln.start = vl.start;
        ln.end = vl.end;
        ln.bbox = vl.bbox;
        pd.lines.append(ln);
    }
    return pd;
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

const PdfSelectionModel::PageData *
PdfSelectionModel::pageDataIfReady(int page) const
{
    if (m_pages.size() != m_doc->pageCount())
        m_pages.resize(m_doc->pageCount());
    const PageData &pd = m_pages[page];
    return pd.loaded ? &pd : nullptr;
}

// Nearest text line to `pos`. Lines containing the point (with a small
// margin) always win; otherwise vertical distance dominates, so a drag
// through the margin selects the line beside it, like a browser.
const PdfSelectionModel::LineInfo *
PdfSelectionModel::lineIn(const PageData &pd, QPointF pos,
                          bool *inside) const
{
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

const PdfSelectionModel::LineInfo *
PdfSelectionModel::lineAt(int page, QPointF pos, bool *inside) const
{
    return lineIn(pageData(page), pos, inside);
}

QRectF PdfSelectionModel::boxNear(int page, const LineInfo &ln, int i,
                                  int cap, int *at) const
{
    const qreal h = qMax<qreal>(1, ln.bbox.height());
    const int stop = qMin(cap, i + 6);
    for (int j = i; j < stop; ++j) {
        const QRectF bb = boundsRect(m_doc->getSelectionAtIndex(page, j, 1));
        if (bb.isNull() || bb.width() <= 0)
            continue;   // space / synthesized char
        // Line ranges are approximate at split boundaries: a char
        // whose real box sits on another baseline belongs to a
        // neighboring line — skip it so it can't attract clicks.
        if (bb.center().y() < ln.bbox.top() - h
            || bb.center().y() > ln.bbox.bottom() + h)
            continue;
        *at = j;
        return bb;
    }
    return QRectF();
}

int PdfSelectionModel::caretIndexAt(int page, const LineInfo &ln,
                                    qreal x) const
{
    int lo = ln.start;
    int hi = ln.end;
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        int at = mid;
        const QRectF b = boxNear(page, ln, mid, hi, &at);
        if (b.isNull()) {
            // Only spaces between mid and hi: the caret is not past
            // them.
            hi = mid;
        } else if (x < b.left()) {
            hi = at;   // boxNear caps at < hi, so this always narrows
        } else if (x > b.right()) {
            lo = at + 1;
        } else {
            return x - b.left() <= b.right() - x ? at : at + 1;
        }
    }
    return lo;
}

PdfSelectionModel::TextPos
PdfSelectionModel::posAt(int page, QPointF pos, bool *insideText) const
{
    TextPos p;
    p.page = page;
    p.idx = 0;
    bool inside = false;
    const LineInfo *ln = lineAt(page, pos, &inside);
    if (ln)
        p.idx = qBound(ln->start, caretIndexAt(page, *ln, pos.x()),
                       ln->end);
    if (insideText)
        *insideText = inside;
    return p;
}


int PdfSelectionModel::lineIndexOf(const PageData &pd, int charIdx) const
{
    // Strict containment first: sub-lines split from one PDFium range
    // share their boundary index (A.end == B.start), and the inclusive
    // scan used to resolve B's first character to A — which sits on a
    // different visual line, often in another column.
    for (int i = 0; i < pd.lines.size(); ++i) {
        const LineInfo &ln = pd.lines[i];
        if (charIdx >= ln.start && charIdx < ln.end)
            return i;
    }
    // Caret exactly at a line end (e.g. selection endpoints).
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

void PdfSelectionModel::paraRange(const TextPos &pos, QPointF pagePos,
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
    //
    // The CLICK point picks the block (so clicking the blank space
    // beside a short heading line still lands in its block), and
    // vertically-nearest wins among overlapping candidates — adjacent
    // blocks' bboxes overlap when tall math lines inflate them, and
    // smallest-area picked the wrong one.
    {
        auto endsPara = [&pd](const LineInfo &ln) {
            return PdfVisualLines::endsParagraph(
                pd.text.mid(ln.start, ln.end - ln.start).trimmed());
        };

        QVector<QRectF> cands;
        const QVector<QRectF> paras = m_paragraphs.value(pos.page);
        for (int attempt = 0; attempt < 2 && cands.isEmpty(); ++attempt) {
            const QPointF c = attempt == 0
                ? pagePos
                : (lineR.isEmpty() ? QPointF() : lineR.center());
            if (c.isNull())
                continue;
            for (const QRectF &r : paras)
                if (r.adjusted(-2, -2, 2, 2).contains(c))
                    cands.append(r);
        }

        // Adjacent blocks' bboxes overlap vertically (tall math or
        // citation-dense lines) and top-edge clicks sit inside both.
        // The text disambiguates: if the previous line terminates a
        // paragraph (or is another column / blank), the clicked line
        // STARTS one — drop candidates that also hold the previous
        // line, and vice versa.
        if (cands.size() > 1 && li > 0) {
            const LineInfo &prevLn = pd.lines[li - 1];
            const bool prevEnds = prevLn.end <= prevLn.start
                || prevLn.bbox.isEmpty()
                || !sameColumn(prevLn.bbox, pd.lines[li].bbox)
                || endsPara(prevLn);
            QVector<QRectF> filtered;
            for (const QRectF &r : cands) {
                const bool holdsPrev = !prevLn.bbox.isEmpty()
                    && r.adjusted(-2, -2, 2, 2).contains(prevLn.bbox.center());
                if (prevEnds ? !holdsPrev : holdsPrev)
                    filtered.append(r);
            }
            if (!filtered.isEmpty())
                cands = filtered;
        }

        QRectF best;
        qreal bestDy = std::numeric_limits<qreal>::max();
        const QPointF refPt = lineR.isEmpty() ? pagePos : lineR.center();
        for (const QRectF &r : cands) {
            const qreal dy = qAbs(refPt.y() - r.center().y());
            if (dy < bestDy) {
                bestDy = dy;
                best = r;
            }
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

    // A line whose visible text ends with sentence-final punctuation
    // terminates the paragraph — same rule the clusterer applies.
    auto endsPara = [&pd](const LineInfo &ln) {
        return PdfVisualLines::endsParagraph(
            pd.text.mid(ln.start, ln.end - ln.start).trimmed());
    };

    int a = li, b = li;
    while (a > 0 && joinable(pd.lines[a - 1], pd.lines[a])
           && !endsPara(pd.lines[a - 1]))
        --a;
    while (b + 1 < pd.lines.size() && joinable(pd.lines[b], pd.lines[b + 1])
           && !endsPara(pd.lines[b]))
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
    if (m_grain == WordGrain) {
        if (inside)
            wordRange(p, s, e);
        else
            m_grain = CharGrain;   // double-click in empty space: caret
    } else if (m_grain == ParaGrain) {
        // The block under the click decides — clicking the blank space
        // beside a short line inside a paragraph still selects it.
        paraRange(p, pagePos, s, e);
        if (s == e)
            m_grain = CharGrain;
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
    else if (m_grain == ParaGrain)
        paraRange(p, pagePos, s, e);
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
    // Hover path: never build page structures here — this runs on
    // every mouse move (and Qt synthesizes hover during scrolling).
    // Until the background build reaches this page, report "no text";
    // the cursor turns into an I-beam a moment later.
    const PageData *pd = pageDataIfReady(page);
    if (!pd)
        return false;
    bool inside = false;
    lineIn(*pd, pagePos, &inside);
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

QVariantMap PdfSelectionModel::linkAt(int page, QPointF pagePos,
                                      bool buildIfNeeded) const
{
    QVariantMap out;
    out.insert(QStringLiteral("found"), false);
    if (!docReady() || page < 0 || page >= m_doc->pageCount())
        return out;
    if (m_pages.size() != m_doc->pageCount())
        m_pages.resize(m_doc->pageCount());
    PageData &pd = m_pages[page];
    if (!pd.linksLoaded) {
        // Hover calls (buildIfNeeded=false) never touch PDFium; the
        // background build fills this in shortly. Click calls build
        // synchronously so link activation always works.
        if (!buildIfNeeded)
            return out;
        QPdfLinkModel lm;
        lm.setDocument(m_doc);
        lm.setPage(page);
        QVector<LinkInfo> links;
        const int rows = lm.rowCount(QModelIndex());
        for (int r = 0; r < rows; ++r) {
            const QPdfLink lk =
                lm.data(lm.index(r, 0), int(QPdfLinkModel::Role::Link))
                    .value<QPdfLink>();
            const QList<QRectF> rects = lk.rectangles();
            for (const QRectF &rc : rects)
                links.append(LinkInfo{rc, lk.page(), lk.location(),
                                      lk.zoom(), lk.url()});
        }
        m_pageLinks[page] = links;
        pd.linksLoaded = true;
    }
    const QVector<LinkInfo> links = m_pageLinks.value(page);
    for (const LinkInfo &lk : links) {
        if (!lk.rect.adjusted(-1, -1, 1, 1).contains(pagePos))
            continue;
        out.insert(QStringLiteral("found"), true);
        out.insert(QStringLiteral("page"), lk.page);
        out.insert(QStringLiteral("location"), lk.location);
        out.insert(QStringLiteral("zoom"), lk.zoom);
        out.insert(QStringLiteral("url"), lk.url);
        return out;
    }
    return out;
}
