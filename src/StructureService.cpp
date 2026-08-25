#include "StructureService.h"

#include "PaperController.h"
#include "Settings.h"
#include "TaskManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
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
    startSegmentation(src.toLocalFile(), m_paper->paperId());
}

void StructureService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A segmentation the app closed on can be started again from nothing:
    // the PDF is all it ever needed. It still has to be the paper on
    // screen, since that is the only block list a result may replace.
    m_tasks->registerResumer(Tasks::Kind::Segment,
                             [this](const QJsonObject &resume) {
        const QString paperId =
            resume.value(QStringLiteral("paperId")).toString();
        const QString path = resume.value(QStringLiteral("path")).toString();
        // A resumer that cannot start says no and starts nothing: GROBID
        // may have been switched off since the run was interrupted, and
        // the PDF may not be where it was.
        if (!m_settings || !m_settings->grobidEnabled())
            return false;
        if (path.isEmpty() || !QFileInfo::exists(path))
            return false;
        if (!m_paper || m_paper->paperId() != paperId)
            return false;
        startSegmentation(path, paperId);
        return true;
    });
}

void StructureService::startSegmentation(const QString &pdfPath,
                                         const QString &paperId)
{
    if (!m_tasks) {
        runSegmentation(pdfPath, paperId);
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::Segment;
    req.title = tr("Segment paragraphs");
    req.paperId = paperId;
    req.paperTitle = QFileInfo(pdfPath).fileName();
    // GROBID says nothing until the whole document comes back, so there
    // is no unit to count off.
    req.steps = 0;
    req.resume = QJsonObject{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("path"), pdfPath},
    };

    const QString previous = m_taskId;
    const QString id = m_tasks->submit(req,
        [this, pdfPath, paperId] {
            // submit() only hands the id back when it returns, and it may
            // call this from inside itself -- one turn of the event loop
            // and the segmentation has its id on record.
            QTimer::singleShot(0, this, [this, pdfPath, paperId] {
                // Cancelled in the turn between being admitted and
                // starting: the stop callback has already dropped the id,
                // or a newer paper's segmentation has taken its place.
                // Neither is ours to run -- and runSegmentation would
                // clear the cancel flag and upload the PDF regardless.
                if (m_taskId.isEmpty() || m_taskPaperId != paperId)
                    return;
                if (!m_paper || m_paper->paperId() != paperId) {
                    // Admitted after the reader moved on. The result could
                    // only be refused, so do not spend the upload; nothing
                    // failed, the paper changed under it.
                    cancelTask();
                    return;
                }
                runSegmentation(pdfPath, paperId);
            });
        },
        [this, paperId] {
            // The manager is stopping us; it owns the outcome from here,
            // so drop the id rather than finishing the task ourselves --
            // and only when the id still belongs to this paper.
            if (m_taskPaperId == paperId) {
                m_taskId.clear();
                m_taskPaperId.clear();
            }
            cancel();
        });
    if (id.isEmpty())
        return;             // this paper is already being segmented
    m_taskId = id;
    m_taskPaperId = paperId;

    // Only one request is ever in the air here, so a segmentation for
    // another paper takes this one's place -- and its result would be
    // refused by PaperController anyway once the reader moved on.
    if (!previous.isEmpty() && previous != id)
        m_tasks->cancel(previous);
}

void StructureService::runSegmentation(const QString &pdfPath,
                                       const QString &paperId)
{
    m_canceled = false;
    startRequest(pdfPath, paperId, false);
}

void StructureService::cancel()
{
    m_canceled = true;
    if (!m_reply)
        return;
    // Let go of the handle before aborting: abort() raises finished(), and
    // onFinished has to see a request that is no longer ours and drop it,
    // rather than report the abort as a GROBID failure.
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->abort();
    reply->deleteLater();
    emit busyChanged();
}

void StructureService::finishTask(bool ok, const QString &error)
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_tasks->finish(id, ok, error);
}

void StructureService::cancelTask()
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_tasks->markCanceled(id);
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
        finishTask(false, m_lastError);
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
    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->setNote(m_taskId,
                         isRetry ? tr("Asking GROBID again")
                                 : tr("Asking GROBID for the structure"));
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
        if (m_tasks && !m_taskId.isEmpty())
            m_tasks->setNote(m_taskId, tr("GROBID is busy; trying again"));
        // The task stays open across the wait -- the retry is part of the
        // same piece of work, not a new one.
        QTimer::singleShot(5000, this, [this, pdfPath, paperId]() {
            // Whatever this timer settles has to be this paper's task. Open
            // another paper inside the five seconds and the id on record is
            // already the new segmentation's -- ending it here would fail
            // the wrong work while its upload is still running.
            const bool ours = (m_taskPaperId == paperId);
            if (m_canceled) {
                // Stopped while we waited. There is no request in flight to
                // report the abort, so this is the last chance to settle
                // the task -- and it was stopped, not failed.
                if (ours)
                    cancelTask();
                return;
            }
            if (m_paper && m_paper->paperId() == paperId) {
                startRequest(pdfPath, paperId, true);
                return;
            }
            // The paper changed under the wait: this segmentation is over,
            // and again nothing went wrong.
            if (ours)
                cancelTask();
        });
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        setLastError(tr("GROBID request failed: %1")
                         .arg(reply->errorString()));
        qCWarning(lcStructure) << "GROBID failed:" << reply->errorString()
                               << "HTTP" << http;
        finishTask(false, m_lastError);
        return;
    }
    if (http == 204) {
        setLastError(tr("GROBID found no extractable content."));
        finishTask(false, m_lastError);
        return;
    }

    QVector<OutlineEntry> outline;
    QVector<Block> blocks = parseTei(reply->readAll(), &outline);
    if (blocks.isEmpty()) {
        setLastError(tr("GROBID returned an unusable document structure."));
        finishTask(false, m_lastError);
        return;
    }

    if (m_paper && m_paper->applyStructuredBlocks(paperId,
                                                  std::move(blocks))) {
        setLastError({});
        qCInfo(lcStructure) << "GROBID segmentation applied";
        finishTask(true);
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
        // The run finished but changed nothing, which is not a success
        // any viewer should show as one.
        finishTask(false, tr("The segmentation was dropped: the paper "
                             "changed while it ran."));
    }
}

void StructureService::setLastError(const QString &err)
{
    if (err == m_lastError)
        return;
    m_lastError = err;
    emit lastErrorChanged();
}
