#pragma once

#include "Block.h"
#include "PdfVisualLines.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include <memory>

class QPdfDocument;

// What one page's selection structures look like once built. Lives out
// here (rather than nested in the model) because it travels between the
// builder thread and the GUI thread as a queued-signal argument.
namespace PdfSelection {

struct LinkInfo {
    QRectF rect;
    int page = -1;
    QPointF location;
    qreal zoom = 0;
    QUrl url;
};

struct PageBundle {
    int page = -1;
    QString text;
    QVector<PdfVisualLines::Line> lines;
    QVector<LinkInfo> links;
};

} // namespace PdfSelection

Q_DECLARE_METATYPE(PdfSelection::PageBundle)

// Builds one page's selection structures at a time, on a thread of its
// own, against a QPdfDocument it opens once per paper.
//
// One page at a time and one document per paper are both deliberate.
// QtPdf funnels every PDFium call through a single global mutex, so a
// builder that swept the whole document up front — as this one used to —
// held that mutex against the GUI thread for as long as the sweep ran,
// and switching papers had to queue behind it. Now the model asks for
// the handful of pages the reader can actually see, and asks for the
// next one only once the last has landed, so a paper switch is never
// more than one page away from a free lock.
class PdfSelectionBuilder : public QObject
{
    Q_OBJECT

public:
    explicit PdfSelectionBuilder(QObject *parent = nullptr) : QObject(parent) {}
    ~PdfSelectionBuilder() override;

public slots:
    // Adopt a new paper. The file is not opened here — a reader who
    // flips past a paper should not pay for parsing it — only when the
    // first page of it is actually asked for.
    void open(const QString &path, const QString &password, int generation);
    // Build one page and hand it back stamped with the generation it
    // was asked for, so answers about a paper the reader has left are
    // recognisable as stale.
    void build(int page, int generation);

signals:
    void built(const PdfSelection::PageBundle &bundle, int generation);

private:
    std::unique_ptr<QPdfDocument> m_doc;
    QString m_path;
    QString m_password;
    int m_generation = -1;
};

// Web-style text selection over a QPdfDocument: cross-page ranges,
// word/paragraph snapping (double/triple click), hover hit-testing
// for the I-beam cursor, and link hit-testing. Replaces the per-page
// PdfSelection items inside Qt's stock PdfMultiPageView, which can't
// select across pages and have no snapping at all.
//
// All coordinates are page points (1/72"), origin top-left — the same
// space QPdfSelection / QPdfSearchModel geometry lives in, so QML
// scales the polygons by the page item's pageScale exactly like it
// already does for search highlights.
class PdfSelectionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString text READ text NOTIFY selectionChanged)
    Q_PROPERTY(int startPage READ startPage NOTIFY selectionChanged)

public:
    explicit PdfSelectionModel(QPdfDocument *doc, QObject *parent = nullptr);
    ~PdfSelectionModel() override;

    bool hasSelection() const;
    // Selected text cleaned for reading: hyphenated line breaks merge
    // the split word, other breaks become spaces, blank lines become
    // paragraph breaks. (Same rules BlockClusterer applies.)
    QString text() const;
    int startPage() const;

    // clickCount: 1 = caret anchor, 2 = word, 3+ = paragraph.
    Q_INVOKABLE void beginAt(int page, QPointF pagePos, int clickCount);
    Q_INVOKABLE void extendTo(int page, QPointF pagePos);
    Q_INVOKABLE void selectAllOnPage(int page);
    Q_INVOKABLE void clear();
    // True when the point sits on a text line — drives the I-beam.
    Q_INVOKABLE bool overText(int page, QPointF pagePos) const;
    // Selection outline for one page, in page points.
    Q_INVOKABLE QList<QPolygonF> polygonsOnPage(int page) const;
    Q_INVOKABLE void copyToClipboard() const;
    // {found, page, location, zoom, url}; page >= 0 = internal jump.
    // Hover calls leave buildIfNeeded false (returns not-found until
    // the background build reaches the page); press/activation calls
    // pass true so links always resolve.
    Q_INVOKABLE QVariantMap linkAt(int page, QPointF pagePos,
                                   bool buildIfNeeded = false) const;
    // Test/diagnostic hook: the visual line rectangles hit-testing uses.
    Q_INVOKABLE QList<QRectF> debugLineRects(int page) const;

    // Where the document lives on disk — lets the background builder
    // open its own instance. main.cpp forwards PaperController's
    // source/password.
    void setSource(const QString &localPath, const QString &password);

    // Ask the builder for the pages around `page`, nearest first. The
    // view calls this as the reader moves; nothing else is built, so a
    // paper nobody scrolls through costs nothing. Pages already built —
    // including ones a click built synchronously — are skipped.
    //
    // Coalesced on a short timer: flipping through tabs or scrolling
    // fast would otherwise start a build for every paper and every page
    // passed through, and each of those takes QtPdf's global lock away
    // from the view that is trying to paint. A page the reader actually
    // stops on is built a fraction of a second later; one they pass is
    // never built at all.
    Q_INVOKABLE void prefetchAround(int page, int radius = 2);

    // Paragraph rectangles from the app's block model (clusterer or
    // GROBID). Triple-click selects one of these, so it matches what
    // the reading pane shows as a paragraph. main.cpp pushes them on
    // every PaperController::blocksChanged.
    void setParagraphs(const QVector<Block> &blocks);

signals:
    void selectionChanged();

private:
    struct TextPos {
        int page = -1;
        int idx = 0;
        bool valid() const { return page >= 0; }
        bool operator<(const TextPos &o) const
        { return page != o.page ? page < o.page : idx < o.idx; }
        bool operator==(const TextPos &o) const
        { return page == o.page && idx == o.idx; }
    };
    struct LineInfo {
        int start = 0;                 // first char (page-text index)
        int end = 0;                   // one past last char (excl. \r\n)
        QRectF bbox;
    };
    struct PageData {
        bool loaded = false;
        bool linksLoaded = false;
        QString text;
        QVector<LineInfo> lines;
    };
    using LinkInfo = PdfSelection::LinkInfo;
    using PageBundle = PdfSelection::PageBundle;
    enum Granularity { CharGrain, WordGrain, ParaGrain };

    bool docReady() const;
    // Builds on demand (click paths). NEVER call from hover/scroll
    // paths — building runs ~100+ PDFium calls that contend with page
    // rendering for QtPdf's global lock; use pageDataIfReady there.
    PageData &pageData(int page) const;
    const PageData *pageDataIfReady(int page) const;
    // What the coalescing timer above finally decided to build.
    void startPrefetch();
    // Queue `page` for the builder if nothing has built it yet.
    void enqueue(int page);
    // Hand the builder the next queued page, if it is idle.
    void pump();
    void onBuilt(const PdfSelection::PageBundle &bundle, int generation);
    const LineInfo *lineIn(const PageData &pd, QPointF pos,
                           bool *inside) const;
    const LineInfo *lineAt(int page, QPointF pos, bool *inside) const;
    TextPos posAt(int page, QPointF pos, bool *insideText = nullptr) const;
    void wordRange(const TextPos &pos, TextPos &s, TextPos &e) const;
    void paraRange(const TextPos &pos, QPointF pagePos,
                   TextPos &s, TextPos &e) const;
    int lineIndexOf(const PageData &pd, int charIdx) const;
    // First valid glyph box at or shortly after char i (skips spaces
    // and off-baseline strays); *at receives the char it belongs to.
    QRectF boxNear(int page, const LineInfo &ln, int i, int cap,
                   int *at) const;
    // Caret index for a click at x — binary search over glyph boxes
    // (~8 PDFium calls) instead of materializing every caret in the
    // line (~100 calls, each contending with page rendering for
    // QtPdf's global PDFium lock — the source of click lag).
    int caretIndexAt(int page, const LineInfo &ln, qreal x) const;
    TextPos selStart() const;
    TextPos selEnd() const;
    bool pageRange(int page, int *from, int *to) const;
    void reset();

    QPdfDocument *m_doc = nullptr;
    mutable QVector<PageData> m_pages;
    mutable QHash<int, QVector<LinkInfo>> m_pageLinks;
    QHash<int, QVector<QRectF>> m_paragraphs;   // page → block bboxes

    QString m_srcPath;
    QString m_srcPassword;
    // Bumped on every paper change; results stamped with an older one
    // are dropped. Cancelling is exactly this plus an empty queue --
    // the page already in the builder's hands is allowed to finish, so
    // a switch waits at most one page rather than a whole document.
    int m_buildGen = 0;
    QThread m_builderThread;
    PdfSelectionBuilder *m_builder = nullptr;
    QList<int> m_queue;
    bool m_building = false;
    QTimer m_prefetchTimer;
    int m_prefetchPage = -1;
    int m_prefetchRadius = 2;

    Granularity m_grain = CharGrain;
    TextPos m_anchorStart, m_anchorEnd;  // unit range at press
    TextPos m_focusStart, m_focusEnd;    // unit range at drag position
};
