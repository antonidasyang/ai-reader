#pragma once

#include "Block.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QPdfDocument;
class QPdfLinkModel;

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
    Q_INVOKABLE QVariantMap linkAt(int page, QPointF pagePos) const;
    // Test/diagnostic hook: the visual line rectangles hit-testing uses.
    Q_INVOKABLE QList<QRectF> debugLineRects(int page) const;

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
        // Lazy caret x-positions: end-start+1 monotonic boundaries.
        mutable QVector<qreal> carets;
    };
    struct PageData {
        bool loaded = false;
        QString text;
        QVector<LineInfo> lines;
    };
    enum Granularity { CharGrain, WordGrain, ParaGrain };

    bool docReady() const;
    PageData &pageData(int page) const;
    void appendLines(int page, int start, int end, PageData &pd) const;
    const LineInfo *lineAt(int page, QPointF pos, bool *inside) const;
    TextPos posAt(int page, QPointF pos, bool *insideText = nullptr) const;
    void wordRange(const TextPos &pos, TextPos &s, TextPos &e) const;
    void paraRange(const TextPos &pos, TextPos &s, TextPos &e) const;
    int lineIndexOf(const PageData &pd, int charIdx) const;
    void ensureCarets(int page, const LineInfo &ln) const;
    TextPos selStart() const;
    TextPos selEnd() const;
    bool pageRange(int page, int *from, int *to) const;
    void reset();

    QPdfDocument *m_doc = nullptr;
    mutable QVector<PageData> m_pages;
    mutable QHash<int, QPdfLinkModel *> m_links;
    QHash<int, QVector<QRectF>> m_paragraphs;   // page → block bboxes

    Granularity m_grain = CharGrain;
    TextPos m_anchorStart, m_anchorEnd;  // unit range at press
    TextPos m_focusStart, m_focusEnd;    // unit range at drag position
};
