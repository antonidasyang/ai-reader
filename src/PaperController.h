#pragma once

#include "BlockCache.h"
#include "BlockListModel.h"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPdfDocument>
#include <QSettings>
#include <QString>
#include <QUrl>

class PaperController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QUrl pdfSource READ pdfSource NOTIFY pdfSourceChanged)
    Q_PROPERTY(QString pdfPassword READ pdfPassword NOTIFY pdfPasswordChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY pdfSourceChanged)
    Q_PROPERTY(BlockListModel *blocks READ blocks CONSTANT)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY blocksChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QString paperId READ paperId NOTIFY blocksChanged)
    Q_PROPERTY(QString currentSelection READ currentSelection NOTIFY currentSelectionChanged)
    Q_PROPERTY(int currentSelectionPage READ currentSelectionPage NOTIFY currentSelectionChanged)
    // True while the background paragraph extraction runs (initial
    // open on a cache miss, or an explicit re-segment). Status stays
    // Ready the whole time — this is the property UI feedback binds to.
    Q_PROPERTY(bool extracting READ extracting NOTIFY extractingChanged)

public:
    enum Status { Empty, Loading, Ready, Error };
    Q_ENUM(Status)

    explicit PaperController(QObject *parent = nullptr);

    QUrl pdfSource() const { return m_source; }
    QString pdfPassword() const { return m_password; }
    QString fileName() const;
    BlockListModel *blocks() { return &m_model; }
    int blockCount() const { return m_model.blockCount(); }
    Status status() const { return m_status; }
    bool extracting() const { return m_extracting; }
    QString errorString() const { return m_errorString; }
    int pageCount() const { return m_doc.pageCount(); }
    // The loaded document — PdfSelectionModel drives text selection
    // hit-testing off it (C++-side only, not exposed to QML).
    QPdfDocument *document() { return &m_doc; }
    QString paperId() const { return m_paperId; }
    QString currentSelection() const { return m_currentSelection; }
    int currentSelectionPage() const { return m_currentSelectionPage; }

    // Rasterize a page at approximately `targetWidthPx` wide. Returns a null
    // image when the page is out of range or the document isn't loaded.
    QImage renderPage(int page, int targetWidthPx = 1280) const;

public slots:
    void openPdf(const QUrl &url);
    void setPassword(const QString &password);
    void clear();
    // Re-open the PDF that was loaded last session, if it still exists.
    // Called by main.cpp once the QML scene is up so QML Connections
    // (password dialog, etc.) can react to the load.
    Q_INVOKABLE void restoreLast();
    // Pushed from QML whenever the user's PDF selection changes; the chat
    // tool `get_user_selection` reads the latest value.
    Q_INVOKABLE void setCurrentSelection(const QString &text, int page);
    // Diagnostic: write a UTF-8 text report (raw PDFium text per page,
    // line bboxes, splitter stats, final blocks) to `dest`. Returns
    // true on success.
    Q_INVOKABLE bool exportExtractedText(const QUrl &dest);
    // First blocks' text, used for DOI/arXiv identifier extraction.
    Q_INVOKABLE QString headText(int maxChars = 6000) const;

    // Discard any saved (auto-extracted + manually-edited) paragraphs
    // for the current paper and re-run the clusterer. Use this when
    // the user wants to start over after manual edits, or to pick up
    // an improved splitter.
    Q_INVOKABLE void rebuildBlocks();

    // Replace the block list with StructureService's GROBID result.
    // Refused (returns false) when the paper changed since the request
    // started, the user already split/merged/deleted a paragraph, or
    // any translation exists — those must not be yanked away.
    bool applyStructuredBlocks(const QString &paperId,
                               QVector<Block> blocks);

signals:
    void pdfSourceChanged();
    void pdfPasswordChanged();
    void blocksChanged();
    void statusChanged();
    void passwordRequired();
    void currentSelectionChanged();
    void extractingChanged();
    // A fresh automatic extraction just ran (cache miss or explicit
    // rebuild) — StructureService listens and tries to upgrade the
    // segmentation via GROBID.
    void autoExtracted();

private:
    void reload();
    void setStatus(Status s, const QString &err = {});
    // Paragraph extraction runs on a worker thread with its OWN
    // QPdfDocument: extraction and page rendering both funnel through
    // QtPdf's global PDFium lock, so doing it on the GUI thread froze
    // the window for seconds while the first pages rendered.
    void startAsyncExtraction();
    void onExtractionFinished();

    QPdfDocument m_doc;
    BlockListModel m_model;
    BlockCache m_blockCache;
    QUrl m_source;
    QString m_password;
    QString m_paperId;
    QString m_currentSelection;
    int m_currentSelectionPage = -1;
    // True once the user split/merged/deleted a paragraph in this
    // paper — blocks applyStructuredBlocks from clobbering edits.
    bool m_blocksEdited = false;
    // Set by rebuildBlocks(): the user explicitly asked for a fresh
    // segmentation, so the finished extraction and the GROBID upgrade
    // that follows must apply even where the automatic path would
    // yield (existing blocks, cached translations). Consumed by the
    // next applyStructuredBlocks(); reset on paper switch.
    bool m_forceExtract = false;
    bool m_extracting = false;
    QFutureWatcher<QVector<Block>> m_extractWatcher;
    QString m_extractPaperId;   // which paper the running job is for
    Status m_status = Empty;
    QString m_errorString;
    QSettings m_qs;
};
