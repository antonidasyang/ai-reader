#pragma once

#include "Block.h"
#include "Section.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QNetworkReply;
class PaperController;
class Settings;

// GROBID-backed paragraph segmentation. Whenever PaperController runs a
// fresh automatic extraction (cache miss or explicit rebuild), this
// service POSTs the PDF to a GROBID instance
// (<grobidUrl>/api/processFulltextDocument) and, if the TEI response
// parses into a sane block list, swaps it in via
// PaperController::applyStructuredBlocks. GROBID's document model
// (title / section heads / paragraphs / captions / formulas /
// references, with PDF coordinates) is far better on academic papers
// than the geometric BlockClusterer, which stays as the immediate
// result and the fallback when the service is off, unreachable, or
// returns garbage.
class StructureService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    StructureService(Settings *settings,
                     PaperController *paper,
                     QObject *parent = nullptr);

    bool busy() const { return m_reply != nullptr; }
    QString lastError() const { return m_lastError; }

    // One entry of the section outline recovered from TEI <head>
    // elements — everything the parser can tell about a heading
    // beyond the Block it emits. `y` is the top edge of the heading's
    // box on `page` in PDF points (-1 when GROBID sent no
    // coordinates); it is captured for future precise jumps but the
    // TOC pipeline currently navigates by page only.
    struct OutlineEntry {
        QString title;        // heading text, numbering stripped
        QString numbering;    // e.g. "2.1"; empty for unnumbered heads
        int     level = 1;    // dot depth of numbering; 1 if unnumbered
        int     page = 0;     // 0-based, same convention as Block::page
        qreal   y = -1.0;
        int     blockId = -1; // id of the matching Heading block
    };

    // Parse a GROBID TEI document into blocks. Exposed for testing.
    // When `outline` is non-null it receives the section outline of
    // the document (left empty when the document was rejected).
    static QVector<Block> parseTei(const QByteArray &tei,
                                   QVector<OutlineEntry> *outline = nullptr);

signals:
    void busyChanged();
    void lastErrorChanged();
    // Emitted when a GROBID result actually replaced the block list.
    void upgraded();
    // Emitted right after upgraded() when the applied TEI also
    // carried a usable outline (>= 2 headings), converted to the
    // Section model the TOC pipeline speaks. Never emitted when the
    // blocks were rejected as stale.
    void outlineExtracted(const QVector<Section> &sections);

private:
    void onAutoExtracted();
    void startRequest(const QString &pdfPath, const QString &paperId,
                      bool isRetry);
    void onFinished(QNetworkReply *reply, const QString &pdfPath,
                    const QString &paperId, bool wasRetry);
    void setLastError(const QString &err);

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_lastError;
};
