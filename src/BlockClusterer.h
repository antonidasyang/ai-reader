#pragma once

#include "Block.h"
#include <QString>
#include <QVector>

class QPdfDocument;

namespace BlockClusterer {

QVector<Block> extract(QPdfDocument &doc);

// Diagnostic dump. Returns a UTF-8 text report covering, per page:
// poly/raw-line counts, the raw text PDFium gave us, every line we
// extracted with its bbox + hyphenation flag, and the gap statistics
// the splitter computed (medianHeight, tightGap, medianGap, threshold,
// sorted gaps). Then a final "blocks" section with the splitter's
// output. Used by PaperController::exportExtractedText so the user
// can inspect exactly what the clusterer is working from.
QString dumpDebug(QPdfDocument &doc);

} // namespace BlockClusterer
