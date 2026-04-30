#pragma once

#include "BlockCache.h"
#include "BlockListModel.h"

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
    QString errorString() const { return m_errorString; }
    int pageCount() const { return m_doc.pageCount(); }
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

    // Discard any saved (auto-extracted + manually-edited) paragraphs
    // for the current paper and re-run the clusterer. Use this when
    // the user wants to start over after manual edits, or to pick up
    // an improved splitter.
    Q_INVOKABLE void rebuildBlocks();

signals:
    void pdfSourceChanged();
    void pdfPasswordChanged();
    void blocksChanged();
    void statusChanged();
    void passwordRequired();
    void currentSelectionChanged();

private:
    void reload();
    void setStatus(Status s, const QString &err = {});

    QPdfDocument m_doc;
    BlockListModel m_model;
    BlockCache m_blockCache;
    QUrl m_source;
    QString m_password;
    QString m_paperId;
    QString m_currentSelection;
    int m_currentSelectionPage = -1;
    Status m_status = Empty;
    QString m_errorString;
    QSettings m_qs;
};
