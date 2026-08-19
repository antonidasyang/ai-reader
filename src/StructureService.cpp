#include "StructureService.h"

#include "PaperController.h"
#include "Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QLoggingCategory>

#include <utility>

Q_LOGGING_CATEGORY(lcStructure, "aireader.structure")

namespace {

// Same semantics as the update-manifest URL: an empty Settings value
// falls through to the canonical public endpoint (the dialog's
// placeholder shows it), so clearing the field never silently turns
// GROBID off — the checkbox is the on/off switch.
constexpr auto kDefaultGrobidUrl = "https://aireader.d2ssoft.com/grobid";

// TOTP gate on the public GROBID route (RFC 6238: HMAC-SHA1, 30 s
// step, 6 digits). The shared secret is an anti-abuse measure against
// anonymous internet scanners, not a user credential — it must match
// ~/.grobid-otp-secret on the server; rotate both together.
// Self-hosted GROBID instances simply ignore the extra header.
constexpr auto kGrobidOtpSecret = "7e93a24caab359c81523491966a8bccab79fa5a0";

QByteArray grobidOtp()
{
    const quint64 step =
        quint64(QDateTime::currentSecsSinceEpoch() / 30);
    QByteArray counter(8, '\0');
    for (int i = 0; i < 8; ++i)
        counter[7 - i] = char((step >> (8 * i)) & 0xff);
    const QByteArray h = QMessageAuthenticationCode::hash(
        counter, QByteArray(kGrobidOtpSecret),
        QCryptographicHash::Sha1);
    const int o = h.at(h.size() - 1) & 0xf;
    const quint32 code =
        ((quint32(quint8(h.at(o))) & 0x7f) << 24
         | quint32(quint8(h.at(o + 1))) << 16
         | quint32(quint8(h.at(o + 2))) << 8
         | quint32(quint8(h.at(o + 3)))) % 1000000;
    return QByteArray::number(code).rightJustified(6, '0');
}

// coords="1,53.4,150.6,247.2,10.7;1,53.4,163.6,..." — 1-based page,
// x/y of the upper-left corner, w, h, in PDF points (same space QtPdf
// uses). Page of the block = page of the first box; bbox = union of
// the boxes on that page.
void parseCoords(QStringView coords, int *page, QRectF *bbox)
{
    *page = -1;
    *bbox = QRectF();
    const auto boxes = coords.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QStringView box : boxes) {
        const auto f = box.split(QLatin1Char(','));
        if (f.size() < 5)
            continue;
        bool ok = false;
        const int p = f[0].toInt(&ok) - 1;
        if (!ok || p < 0)
            continue;
        const QRectF r(f[1].toDouble(), f[2].toDouble(),
                       f[3].toDouble(), f[4].toDouble());
        if (*page < 0)
            *page = p;
        if (p == *page)
            *bbox = bbox->united(r);
    }
}

struct TeiCollector {
    QVector<Block> blocks;
    int lastPage = 0;

    // Returns the id of the added block, or -1 when the text was
    // empty and nothing was added.
    int add(Block::Kind kind, const QString &text, int page,
            const QRectF &bbox)
    {
        const QString t = text.simplified();
        if (t.isEmpty())
            return -1;
        Block b;
        b.id = blocks.size();
        b.ord = blocks.size();
        b.kind = kind;
        b.text = t;
        if (page >= 0) {
            b.page = page;
            lastPage = page;
        } else {
            // No coordinates from GROBID — keep the reading order
            // monotonic so page navigation still works.
            b.page = lastPage;
        }
        b.bbox = bbox;
        blocks.append(b);
        return b.id;
    }
};

// Hierarchy depth from GROBID's @n numbering: "2" → 1, "2.1" → 2,
// "A.1.3" → 3. Trailing dots ("1.") don't count; empty → 1.
int levelFromNumbering(const QString &numbering)
{
    QString s = numbering;
    while (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    if (s.isEmpty())
        return 1;
    return qBound(1, int(s.count(QLatin1Char('.'))) + 1, 6);
}

} // namespace

QVector<Block> StructureService::parseTei(const QByteArray &tei,
                                          QVector<OutlineEntry> *outline)
{
    if (outline)
        outline->clear();

    QXmlStreamReader xml(tei);
    TeiCollector out;
    QVector<OutlineEntry> toc;

    // Record an outline entry for a heading block that was just
    // added. The block's stored page already includes the
    // last-known-page fallback, so read it back rather than using the
    // raw coords page.
    auto addOutline = [&out, &toc](int blockId, const QString &title,
                                   const QString &numbering,
                                   int coordsPage, const QRectF &bbox) {
        if (blockId < 0)
            return;
        OutlineEntry e;
        e.title = title.simplified();
        if (e.title.isEmpty())
            e.title = out.blocks.at(blockId).text;
        e.numbering = numbering;
        e.level = levelFromNumbering(numbering);
        e.page = out.blocks.at(blockId).page;
        e.y = (coordsPage >= 0 && !bbox.isNull()) ? bbox.top() : -1.0;
        e.blockId = blockId;
        toc.append(e);
    };

    int inHeader = 0, inAbstract = 0, inBody = 0, inBack = 0, inFigure = 0;
    bool sawAbstractHeading = false;
    bool sawReferencesHeading = false;
    bool sawTitle = false;

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("teiHeader")) { ++inHeader; continue; }
            if (name == QLatin1String("abstract")) { ++inAbstract; continue; }
            if (name == QLatin1String("body"))     { ++inBody; continue; }
            if (name == QLatin1String("back"))     { ++inBack; continue; }
            if (name == QLatin1String("figure"))   { ++inFigure; continue; }

            int page = -1;
            QRectF bbox;
            parseCoords(xml.attributes().value(QLatin1String("coords")),
                        &page, &bbox);

            // Header: the main title only (authors/affiliations are
            // metadata, not reading matter).
            if (inHeader && !inAbstract && name == QLatin1String("title")) {
                const bool isMain = !sawTitle
                    && xml.attributes().value(QLatin1String("type"))
                           != QLatin1String("sub");
                const QString t =
                    xml.readElementText(QXmlStreamReader::IncludeChildElements);
                if (isMain) {
                    out.add(Block::Heading, t, page, bbox);
                    sawTitle = true;
                }
                continue;
            }
            if (inAbstract && name == QLatin1String("p")) {
                if (!sawAbstractHeading) {
                    const int blockId =
                        out.add(Block::Heading, QStringLiteral("Abstract"),
                                page, QRectF());
                    addOutline(blockId, QStringLiteral("Abstract"),
                               QString(), -1, QRectF());
                    sawAbstractHeading = true;
                }
                out.add(Block::Paragraph,
                        xml.readElementText(
                            QXmlStreamReader::IncludeChildElements),
                        page, bbox);
                continue;
            }
            if (inFigure) {
                if (name == QLatin1String("figDesc")) {
                    out.add(Block::Caption,
                            xml.readElementText(
                                QXmlStreamReader::IncludeChildElements),
                            page, bbox);
                }
                continue;   // skip everything else nested in figures
            }
            if (inBody) {
                if (name == QLatin1String("head")) {
                    // GROBID strips the section number into @n; put it
                    // back so headings read "3.2 Method" not "Method".
                    const QString n = xml.attributes()
                                          .value(QLatin1String("n"))
                                          .toString()
                                          .simplified();
                    const QString clean = xml.readElementText(
                        QXmlStreamReader::IncludeChildElements);
                    QString t = clean;
                    if (!n.isEmpty())
                        t = n + QLatin1Char(' ') + t;
                    const int blockId =
                        out.add(Block::Heading, t, page, bbox);
                    addOutline(blockId, clean, n, page, bbox);
                    continue;
                }
                if (name == QLatin1String("p")
                    || name == QLatin1String("note")) {
                    out.add(Block::Paragraph,
                            xml.readElementText(
                                QXmlStreamReader::IncludeChildElements),
                            page, bbox);
                    continue;
                }
                if (name == QLatin1String("formula")) {
                    out.add(Block::Equation,
                            xml.readElementText(
                                QXmlStreamReader::IncludeChildElements),
                            page, bbox);
                    continue;
                }
                continue;
            }
            if (inBack && name == QLatin1String("note")
                && xml.attributes().value(QLatin1String("type"))
                       == QLatin1String("raw_reference")) {
                if (!sawReferencesHeading) {
                    const int blockId =
                        out.add(Block::Heading,
                                QStringLiteral("References"),
                                page, QRectF());
                    addOutline(blockId, QStringLiteral("References"),
                               QString(), -1, QRectF());
                    sawReferencesHeading = true;
                }
                out.add(Block::Paragraph,
                        xml.readElementText(
                            QXmlStreamReader::IncludeChildElements),
                        page, bbox);
                continue;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const auto name = xml.name();
            if (name == QLatin1String("teiHeader") && inHeader) --inHeader;
            else if (name == QLatin1String("abstract") && inAbstract) --inAbstract;
            else if (name == QLatin1String("body") && inBody) --inBody;
            else if (name == QLatin1String("back") && inBack) --inBack;
            else if (name == QLatin1String("figure") && inFigure) --inFigure;
        }
    }

    if (xml.hasError()) {
        qCWarning(lcStructure) << "TEI parse error:" << xml.errorString();
        return {};
    }

    // Sanity: a real paper yields a handful of substantial paragraphs.
    // Anything less means GROBID misfired — keep the clusterer output.
    int substantial = 0;
    qsizetype totalChars = 0;
    for (const Block &b : out.blocks) {
        totalChars += b.text.size();
        if (b.kind == Block::Paragraph && b.text.size() > 80)
            ++substantial;
    }
    if (substantial < 3 || totalChars < 500) {
        qCWarning(lcStructure)
            << "TEI result too thin (" << out.blocks.size() << "blocks,"
            << substantial << "substantial paragraphs) — rejected";
        return {};
    }
    if (outline)
        *outline = std::move(toc);
    return out.blocks;
}

StructureService::StructureService(Settings *settings,
                                   PaperController *paper,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
{
    connect(paper, &PaperController::autoExtracted,
            this, &StructureService::onAutoExtracted);
}

void StructureService::onAutoExtracted()
{
    if (!m_settings || !m_paper)
        return;
    if (!m_settings->grobidEnabled())
        return;
    const QUrl src = m_paper->pdfSource();
    if (!src.isLocalFile())
        return;
    startRequest(src.toLocalFile(), m_paper->paperId(), false);
}

void StructureService::startRequest(const QString &pdfPath,
                                    const QString &paperId,
                                    bool isRetry)
{
    if (m_reply) {
        // A newer paper superseded the in-flight request.
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QFile *file = new QFile(pdfPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        setLastError(tr("Cannot read PDF for GROBID: %1").arg(pdfPath));
        return;
    }

    QString base = m_settings->grobidUrl().trimmed();
    if (base.isEmpty())
        base = QString::fromLatin1(kDefaultGrobidUrl);
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl endpoint(base + QStringLiteral("/api/processFulltextDocument"));

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/pdf"));
    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"input\"; filename=\"%1\"")
            .arg(QFileInfo(pdfPath).fileName()));
    filePart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(filePart);

    auto addField = [multi](const char *name, const QString &value) {
        QHttpPart part;
        part.setHeader(
            QNetworkRequest::ContentDispositionHeader,
            QStringLiteral("form-data; name=\"%1\"")
                .arg(QLatin1String(name)));
        part.setBody(value.toUtf8());
        multi->append(part);
    };
    // No external consolidation services — keep it local and fast.
    addField("consolidateHeader", QStringLiteral("0"));
    addField("consolidateCitations", QStringLiteral("0"));
    addField("includeRawCitations", QStringLiteral("1"));
    for (const char *el : {"title", "head", "p", "figure", "formula", "note"})
        addField("teiCoordinates", QLatin1String(el));

    QNetworkRequest req(endpoint);
    req.setRawHeader("Accept", "application/xml");
    req.setRawHeader("X-Grobid-Otp", grobidOtp());
    req.setTransferTimeout(120000);

    m_reply = m_nam.post(req, multi);
    multi->setParent(m_reply);
    emit busyChanged();
    qCInfo(lcStructure) << "GROBID request" << endpoint.toString()
                        << "for" << QFileInfo(pdfPath).fileName()
                        << (isRetry ? "(retry)" : "");

    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, pdfPath, paperId, isRetry]() {
                onFinished(reply, pdfPath, paperId, isRetry);
            });
}

void StructureService::onFinished(QNetworkReply *reply,
                                  const QString &pdfPath,
                                  const QString &paperId,
                                  bool wasRetry)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;
    reply->deleteLater();
    emit busyChanged();

    const int http =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 503 = all GROBID worker threads busy; the docs recommend a short
    // retry. One retry, then give up (clusterer output stands).
    if (http == 503 && !wasRetry) {
        qCInfo(lcStructure) << "GROBID busy (503), retrying in 5s";
        QTimer::singleShot(5000, this, [this, pdfPath, paperId]() {
            if (m_paper && m_paper->paperId() == paperId)
                startRequest(pdfPath, paperId, true);
        });
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        setLastError(tr("GROBID request failed: %1")
                         .arg(reply->errorString()));
        qCWarning(lcStructure) << "GROBID failed:" << reply->errorString()
                               << "HTTP" << http;
        return;
    }
    if (http == 204) {
        setLastError(tr("GROBID found no extractable content."));
        return;
    }

    QVector<OutlineEntry> outline;
    QVector<Block> blocks = parseTei(reply->readAll(), &outline);
    if (blocks.isEmpty()) {
        setLastError(tr("GROBID returned an unusable document structure."));
        return;
    }

    if (m_paper && m_paper->applyStructuredBlocks(paperId,
                                                  std::move(blocks))) {
        setLastError({});
        qCInfo(lcStructure) << "GROBID segmentation applied";
        emit upgraded();
        // The outline only leaves this service when the blocks it
        // refers to were actually applied — a stale/rejected result
        // must never reach the TOC pipeline. Fewer than 2 headings is
        // no outline worth showing.
        if (outline.size() >= 2) {
            QVector<Section> sections;
            sections.reserve(outline.size());
            for (const OutlineEntry &e : std::as_const(outline)) {
                Section s;
                s.id = QStringLiteral("g%1").arg(sections.size() + 1);
                s.level = e.level;
                s.title = e.title;
                s.startBlockId = e.blockId;
                s.startPage = e.page;
                sections.append(s);
            }
            qCInfo(lcStructure) << "GROBID outline:" << sections.size()
                                << "sections";
            emit outlineExtracted(sections);
        } else {
            qCInfo(lcStructure)
                << "GROBID outline too thin (" << outline.size()
                << "headings) — TOC pipeline left alone";
        }
    } else {
        qCInfo(lcStructure)
            << "GROBID result dropped (paper changed, edits or "
               "translations exist)";
    }
}

void StructureService::setLastError(const QString &err)
{
    if (err == m_lastError)
        return;
    m_lastError = err;
    emit lastErrorChanged();
}
