#pragma once

#include "Block.h"
#include <QString>
#include <QVector>

class QPdfDocument;

namespace BlockClusterer {

// pacePerPageMs > 0 sleeps between pages — background callers use it
// so the PDFium global lock stays mostly free for page rendering.
QVector<Block> extract(QPdfDocument &doc, int pacePerPageMs = 0);

// Diagnostic dump. Returns a UTF-8 text report covering, per page:
// poly/raw-line counts, the raw text PDFium gave us, and every line we
// extracted with its bbox, hyphenation flag, and a "¶" mark when the
// splitter treated it as the end of a paragraph. Then a final "blocks"
// section with the splitter's output. Used by
// PaperController::exportExtractedText so the user can inspect exactly
// what the clusterer is working from.
QString dumpDebug(QPdfDocument &doc);

} // namespace BlockClusterer
