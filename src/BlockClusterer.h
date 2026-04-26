#pragma once

#include "Block.h"
#include <QVector>

class QPdfDocument;

namespace BlockClusterer {

QVector<Block> extract(QPdfDocument &doc);

} // namespace BlockClusterer
