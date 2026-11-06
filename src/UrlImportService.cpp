#include "UrlImportService.h"

#include "ImportService.h"
#include "Library.h"
#include "ProjectController.h"
#include "Stall.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

#ifndef AIREADER_VERSION
#define AIREADER_VERSION "dev"
#endif

namespace {

// A paper is a few MB; a scanned book can be a few hundred. Past this it is
// not a paper, and it is all being held in memory.
constexpr qint64 kMaxBytes = 300LL * 1024 * 1024;

// New-style ids (0704.0001 onwards) and old-style ones (hep-th/9901001,
// math.GT/0309136), each with an optional version.
const QString kNewId = QStringLiteral("(\\d{4}\\.\\d{4,5})(v\\d+)?");
const QString kOldId =
    QStringLiteral("([a-z][a-z\\-]*(?:\\.[a-z]{2})?/\\d{7})(v\\d+)?");

// `s` is exactly an arXiv id (optionally "arXiv:"-prefixed and ".pdf"-
// suffixed); fills id and version.
bool matchArxivId(const QString &s, QString &id, QString &version)
{
    static const QRegularExpression re(
        QStringLiteral("^(?:arxiv:\\s*)?(?:%1|%2)(?:\\.pdf)?/?$")
            .arg(kNewId, kOldId),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    if (!m.captured(1).isEmpty()) {
        id = m.captured(1);
        version = m.captured(2);
    } else {
        id = m.captured(3);
        version = m.captured(4);
    }
    return true;
}

UrlImportService::Target arxivTarget(const QString &id, const QString &version)
{
    UrlImportService::Target t;
    t.pdfUrl = QUrl(QStringLiteral("https://arxiv.org/pdf/") + id + version);
    QString safe = id;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    t.fileName = QStringLiteral("arXiv-") + safe + version + QStringLiteral(".pdf");
    t.identifier = QStringLiteral("arXiv:") + id;
    return t;
}

// The string as a web URL, or an invalid QUrl when it isn't one. A missing
// scheme is allowed only for something that clearly has a host and a path
// ("arxiv.org/abs/…"), so arbitrary text never becomes a link.
QUrl asWebUrl(const QString &s)
{
    if (s.contains(QRegularExpression(QStringLiteral("\\s"))))
        return {};
    QUrl u;
    static const QRegularExpression schemeless(
        QStringLiteral("^[\\w-]+(\\.[\\w-]+)+/\\S*$"));
    if (s.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || s.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        u = QUrl(s);
    else if (schemeless.match(s).hasMatch())
        u = QUrl(QStringLiteral("https://") + s);
    if (!u.isValid() || u.host().isEmpty())
        return {};
    return u;
}

bool looksLikePdf(const QByteArray &head)
{
    // The header may legally sit anywhere in the first KB.
    return head.left(1024).contains("%PDF-");
}

bool fileLooksLikePdf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && looksLikePdf(f.read(1024));
}

// <meta name="citation_pdf_url" content="…"> — the Highwire tag Google
// Scholar reads, carried by arXiv, ACL Anthology, PMLR, NeurIPS, Springer,
// ScienceDirect and most other places papers live.
QUrl citationPdfUrl(const QByteArray &html, const QUrl &base)
{
    static const QRegularExpression metaRe(
        QStringLiteral("<meta\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nameRe(
        QStringLiteral("\\bname\\s*=\\s*[\"']citation_pdf_url[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression contentRe(
        QStringLiteral("\\bcontent\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
        QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(html);
    auto it = metaRe.globalMatch(text);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        if (!nameRe.match(tag).hasMatch())
            continue;
        const auto c = contentRe.match(tag);
        if (!c.hasMatch())
            continue;
        QString href = c.captured(1).isEmpty() ? c.captured(2) : c.captured(1);
        href.replace(QLatin1String("&amp;"), QLatin1String("&"));
        const QUrl u = base.resolved(QUrl(href.trimmed()));
        if (u.isValid() && (u.scheme() == QLatin1String("https")
                            || u.scheme() == QLatin1String("http")))
            return u;
    }
    return {};
}

QString sanitizeFileName(QString name)
{
    static const QRegularExpression bad(
        QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(bad, QStringLiteral("_"));
    name = name.trimmed();
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    if (name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        name.chop(4);
    name = name.left(120).trimmed();
    if (name.isEmpty())
        return {};
    return name + QStringLiteral(".pdf");
}

QString nameFromReply(QNetworkReply *reply)
{
    // Content-Disposition first: a /download?id=… URL says nothing useful.
    const QString cd = QString::fromUtf8(reply->rawHeader("Content-Disposition"));
    static const QRegularExpression cdRe(
        QStringLiteral("filename\\*?\\s*=\\s*(?:UTF-8'')?\"?([^\";]+)\"?"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = cdRe.match(cd);
    if (m.hasMatch()) {
        const QString n = sanitizeFileName(
            QUrl::fromPercentEncoding(m.captured(1).toUtf8()));
        if (!n.isEmpty())
            return n;
    }
    const QString n = sanitizeFileName(reply->url().fileName());
    if (!n.isEmpty())
        return n;
    return QStringLiteral("paper-%1.pdf")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

// `path`, or "name (2).pdf", "name (3).pdf"… — unless one of those already
// holds exactly these bytes, in which case that one is reused.
QString pathFor(const QString &path, const QByteArray &bytes, bool *reused)
{
    *reused = false;
    const QFileInfo fi(path);
    const QString stem = fi.completeBaseName();
    for (int n = 1; n < 1000; ++n) {
        const QString p = n == 1
            ? path
            : fi.absolutePath() + QStringLiteral("/%1 (%2).pdf").arg(stem).arg(n);
        QFile f(p);
        if (!f.exists())
            return p;
        if (f.size() == bytes.size() && f.open(QIODevice::ReadOnly)
                && f.readAll() == bytes) {
            *reused = true;
            return p;
        }
    }
    return path;
}

} // namespace

UrlImportService::UrlImportService(ImportService *importer,
                                   ProjectController *projects,
                                   Library *library, QObject *parent)
    : QObject(parent)
    , m_importer(importer)
    , m_projects(projects)
    , m_library(library)
    , m_fallbackDir(defaultFallbackDir())
{
    if (m_library)
        connect(m_library, &Library::currentFolderChanged,
                this, &UrlImportService::downloadFolderChanged);
}

UrlImportService::Target UrlImportService::resolve(const QString &input)
{
    QString s = input.trimmed();
    if (s.startsWith(QLatin1Char('<')) && s.endsWith(QLatin1Char('>')))
        s = s.mid(1, s.size() - 2).trimmed();
    if (s.isEmpty())
        return {};

    QString id, version;
    if (matchArxivId(s, id, version))
        return arxivTarget(id, version);

    // arXiv's DataCite DOI, bare or as a doi.org link.
    static const QRegularExpression arxivDoi(
        QStringLiteral("^(?:https?://(?:dx\\.)?doi\\.org/|doi:\\s*)?"
                       "10\\.48550/arxiv\\.%1$").arg(kNewId),
        QRegularExpression::CaseInsensitiveOption);
    const auto d = arxivDoi.match(s);
    if (d.hasMatch())
        return arxivTarget(d.captured(1), d.captured(2));

    const QUrl url = asWebUrl(s);
    if (!url.isValid())
        return {};

    QString host = url.host().toLower();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    const QString path = url.path();

    // /abs/<id>, /pdf/<id>.pdf, /html/<id>v2, /papers/<id>, /overview/<id>:
    // everything after the first path segment is the id.
    const bool arxivLike = host == QLatin1String("arxiv.org")
                        || host.endsWith(QLatin1String(".arxiv.org"))
                        || host == QLatin1String("alphaxiv.org")
                        || (host == QLatin1String("huggingface.co")
                            && path.startsWith(QLatin1String("/papers/")));
    if (arxivLike) {
        const int slash = path.indexOf(QLatin1Char('/'), 1);
        if (slash > 0 && matchArxivId(path.mid(slash + 1), id, version))
            return arxivTarget(id, version);
        // Some other arXiv page: let the generic path try its meta tags.
    }

    if (host == QLatin1String("openreview.net")) {
        const QString orId = QUrlQuery(url).queryItemValue(QStringLiteral("id"));
        if (!orId.isEmpty()) {
            Target t;
            QUrl pdf(QStringLiteral("https://openreview.net/pdf"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("id"), orId);
            pdf.setQuery(q);
            t.pdfUrl = pdf;
            t.fileName = sanitizeFileName(QStringLiteral("OpenReview-") + orId);
            return t;
        }
    }

    Target t;
    t.pdfUrl = url;
    return t;
}

QString UrlImportService::defaultFallbackDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/Downloads");
    return dir;
}

QString UrlImportService::downloadDir() const
{
    if (m_library) {
        const QString open = m_library->currentFolder();
        if (!open.isEmpty() && QFileInfo(open).isDir())
            return QDir::cleanPath(open);
    }
    return m_fallbackDir;
}

void UrlImportService::setFallbackDir(const QString &dir)
{
    if (dir == m_fallbackDir)
        return;
    m_fallbackDir = dir;
    emit downloadFolderChanged();
}

QString UrlImportService::downloadFolder() const
{
    return QDir::toNativeSeparators(downloadDir());
}

bool UrlImportService::recognises(const QString &input) const
{
    return resolve(input).pdfUrl.isValid();
}

QString UrlImportService::clipboardLink() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return {};
    const QString text = cb->text().trimmed();
    if (text.size() > 2048 || !recognises(text))
        return {};
    return text;
}

void UrlImportService::fetch(const QString &input)
{
    if (busy())
        return;
    const Target t = resolve(input);
    if (!t.pdfUrl.isValid()) {
        fail(tr("Not a link I can download from. Paste an arXiv, OpenReview "
                "or PDF link, or an arXiv id."));
        return;
    }
    m_target = t;

    // A named file already on disk is the same paper: no second download.
    if (!t.fileName.isEmpty()) {
        const QString existing = downloadDir() + QLatin1Char('/') + t.fileName;
        if (QFileInfo::exists(existing) && fileLooksLikePdf(existing)) {
            finishWith(existing);
            return;
        }
    }
    start(t.pdfUrl, true);
}

void UrlImportService::start(const QUrl &url, bool mayFollowPage)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QByteArray("AiReader/" AIREADER_VERSION));
    req.setRawHeader("Accept",
                     "application/pdf,text/html;q=0.9,*/*;q=0.8");
    // Inactivity, not total time: a slow mirror is fine, a dead one isn't.
    req.setTransferTimeout(60000);

    m_mayFollowPage = mayFollowPage;
    setProgress(-1);
    setStatus(tr("Downloading from %1…").arg(url.host()));

    QNetworkReply *reply = m_nam.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
                if (reply != m_reply)
                    return;
                if (received > kMaxBytes || total > kMaxBytes) {
                    fail(tr("The file is too large to be a paper (over %1 MB).")
                             .arg(kMaxBytes / (1024 * 1024)));
                    return;
                }
                setProgress(total > 0 ? double(received) / double(total) : -1);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply == m_reply)
            onFinished();
    });
    emit busyChanged();
}

void UrlImportService::onFinished()
{
    QNetworkReply *reply = m_reply;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http == 404)
            fail(tr("Nothing at that address (HTTP 404). Check the id or link."));
        else if (http > 0)
            fail(tr("Download failed (HTTP %1).").arg(http));
        else
            fail(tr("Download failed: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray bytes = reply->readAll();
    if (!looksLikePdf(bytes)) {
        // A landing page rather than the file: follow its citation_pdf_url
        // once. Never twice — two pages pointing at each other would loop.
        if (m_mayFollowPage) {
            const QUrl next = citationPdfUrl(bytes, reply->url());
            if (next.isValid() && next != reply->url()) {
                m_reply = nullptr;
                start(next, false);
                return;
            }
        }
        fail(tr("That link leads to a web page, not a PDF, and the page "
                "doesn't say where its PDF is."));
        return;
    }

    Stall::Mark mark("saving a downloaded PDF");
    const QString dir = downloadDir();
    if (!QDir().mkpath(dir)) {
        fail(tr("Couldn't create the folder %1.").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    QString path;
    bool reused = false;
    if (!m_target.fileName.isEmpty())
        path = dir + QLatin1Char('/') + m_target.fileName;
    else
        path = pathFor(dir + QLatin1Char('/') + nameFromReply(reply), bytes, &reused);

    if (!reused) {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()
                || !f.commit()) {
            fail(tr("Couldn't save the PDF to %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
    }
    finishWith(path);
}

void UrlImportService::finishWith(const QString &localPath)
{
    m_reply = nullptr;
    bool added = false;
    if (m_importer && m_projects && m_projects->canWrite()) {
        m_importer->importFile(localPath, m_target.identifier);
        added = true;
    }
    const QString shown = QDir::toNativeSeparators(localPath);
    setProgress(-1);
    setStatus(added ? tr("Saved to %1 and adding it to the library.").arg(shown)
                    : tr("Saved to %1.").arg(shown));
    emit busyChanged();
    emit downloaded(QUrl::fromLocalFile(localPath).toString(), added);
}

void UrlImportService::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    setProgress(-1);
    setStatus(tr("Download cancelled."));
    emit busyChanged();
}

void UrlImportService::fail(const QString &message)
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        if (reply->isRunning())
            reply->abort();
        reply->deleteLater();
    }
    setProgress(-1);
    setStatus(message);
    emit busyChanged();
    emit failed(message);
}

void UrlImportService::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void UrlImportService::setProgress(double p)
{
    if (p == m_progress)
        return;
    m_progress = p;
    emit progressChanged();
}
