#pragma once

#include "BlockListModel.h"

#include <QImage>
#include <QObject>
#include <QPdfDocument>
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

    // Rasterize a page at approximately `targetWidthPx` wide. Returns a null
    // image when the page is out of range or the document isn't loaded.
    QImage renderPage(int page, int targetWidthPx = 1280) const;

public slots:
    void openPdf(const QUrl &url);
    void setPassword(const QString &password);
    void clear();

signals:
    void pdfSourceChanged();
    void pdfPasswordChanged();
    void blocksChanged();
    void statusChanged();
    void passwordRequired();

private:
    void reload();
    void setStatus(Status s, const QString &err = {});

    QPdfDocument m_doc;
    BlockListModel m_model;
    QUrl m_source;
    QString m_password;
    QString m_paperId;
    Status m_status = Empty;
    QString m_errorString;
};
