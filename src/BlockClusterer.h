#pragma once

#include "Block.h"
#include <QString>
#include <QVector>

#include <functional>

class QPdfDocument;

namespace BlockClusterer {

// Asked between pages: true means the caller has lost interest (the
// reader moved to another paper) and extract() should give up.
using CancelFn = std::function<bool()>;

// pacePerPageMs > 0 sleeps between pages — background callers use it
// so the PDFium global lock stays mostly free for page rendering.
// A cancelled run returns an empty list: half a segmentation is worse
// than none, and the caller drops the result either way.
QVector<Block> extract(QPdfDocument &doc, int pacePerPageMs = 0,
                       const CancelFn &canceled = {});

// Diagnostic dump. Returns a UTF-8 text report covering, per page:
// poly/raw-line counts, the raw text PDFium gave us, and every line we
// extracted with its bbox, hyphenation flag, and a "¶" mark when the
// splitter treated it as the end of a paragraph. Then a final "blocks"
// section with the splitter's output. Used by
// PaperController::exportExtractedText so the user can inspect exactly
// what the clusterer is working from.
QString dumpDebug(QPdfDocument &doc);

} // namespace BlockClusterer
