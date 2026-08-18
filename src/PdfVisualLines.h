#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

class QPdfDocument;

// Rendered-line extraction shared by BlockClusterer (paragraph
// segmentation) and PdfSelectionModel (selection hit-testing).
//
// PDFium's \r\n breaks under-report visual lines: one break-delimited
// range can span several baselines or a column jump (Keshav p.1: 73
// ranges vs 107 rendered lines). Ranges whose bounds() report several
// polygons are split per polygon by assigning each character to its
// nearest rect, so every returned Line is one rendered line with a
// tight bbox. Both consumers depending on the same splitting is what
// keeps triple-click selection and the reading pane's paragraphs in
// agreement.
namespace PdfVisualLines {

struct Line {
    int start = 0;    // char index into the page text
    int end = 0;      // one past the last char (excl. trailing \r\n)
    QRectF bbox;      // page points, top-left origin; null if no glyphs
};

// Fast: character boundaries between same-range polygons are
// estimated from rect-width proportions snapped to spaces/hyphens —
// no extra PDFium calls, right for whole-document paragraph
// segmentation (rects are exact; a boundary off by a couple of
// characters doesn't move a paragraph).
// Precise: each estimate is cross-checked against a per-polygon
// getSelection hit test (~1 ms per polygon) — used by the selection
// model, which builds pages lazily, so the cost lands per page.
enum Precision { Fast, Precise };

// pageText receives QPdfDocument::getAllText(page).text() verbatim —
// Line indices refer to it, and they are valid indices for
// getSelectionAtIndex(). Empty ranges are kept: blank lines are
// paragraph separators for the clusterer.
QVector<Line> extract(QPdfDocument &doc, int page, QString *pageText,
                      Precision precision = Fast);

// Paragraph-terminating rule shared with the clusterer: the line ends
// with sentence-final punctuation (trailing closers skipped).
bool endsParagraph(const QString &lineText);

} // namespace PdfVisualLines
