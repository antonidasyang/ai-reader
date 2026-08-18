#pragma once

#include "Block.h"

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

    // Parse a GROBID TEI document into blocks. Exposed for testing.
    static QVector<Block> parseTei(const QByteArray &tei);

signals:
    void busyChanged();
    void lastErrorChanged();
    // Emitted when a GROBID result actually replaced the block list.
    void upgraded();

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
