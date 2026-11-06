// "Download from a link": what a pasted string resolves to, and the download
// itself — against a QTcpServer standing in for the web, so it runs offline:
// a PDF, a landing page that names its PDF in citation_pdf_url, a page that
// doesn't, a 404, and a server that hands back the same PDF twice.
//
// Where the file goes: the folder open in the folder pane, else Downloads —
// both temporary directories here, never the user's own. The real Library
// remembers its folder in the settings file, so that is pointed at a scratch
// directory before anything is constructed.
// URLIMPORT_LIVE=1 additionally fetches one real arXiv paper.

#include "Library.h"
#include "StorageIdentity.h"
#include "UrlImportService.h"

#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

static const QByteArray kPdf =
    "%PDF-1.4\n1 0 obj << /Type /Catalog >> endobj\ntrailer << /Root 1 0 R >>\n%%EOF\n";

// One request per connection; the path picks the answer.
class FakeWeb : public QTcpServer
{
public:
    int hits = 0;
    FakeWeb()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray req = s->readAll();
                    if (!req.contains("\r\n\r\n"))
                        return;
                    ++hits;
                    const QByteArray path = req.split(' ').value(1);
                    s->write(answer(path));
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

private:
    static QByteArray reply(const QByteArray &status, const QByteArray &type,
                            const QByteArray &body, const QByteArray &extra = {})
    {
        return "HTTP/1.1 " + status + "\r\nContent-Type: " + type
             + "\r\nContent-Length: " + QByteArray::number(body.size())
             + "\r\n" + extra + "Connection: close\r\n\r\n" + body;
    }
    QByteArray answer(const QByteArray &path) const
    {
        if (path == "/files/attention.pdf")
            return reply("200 OK", "application/pdf", kPdf);
        if (path == "/download?id=7")
            return reply("200 OK", "application/octet-stream", kPdf,
                         "Content-Disposition: attachment; filename=\"Deep Nets: a study.pdf\"\r\n");
        if (path == "/landing")
            return reply("200 OK", "text/html",
                         "<html><head><meta content=\"/files/attention.pdf\" "
                         "name=\"citation_pdf_url\"></head></html>");
        if (path == "/loop")      // a page naming another page, not a PDF
            return reply("200 OK", "text/html",
                         "<meta name='citation_pdf_url' content='/loop2'>");
        if (path == "/loop2")
            return reply("200 OK", "text/html",
                         "<meta name='citation_pdf_url' content='/loop'>");
        if (path == "/page")
            return reply("200 OK", "text/html", "<html>just a page</html>");
        return reply("404 Not Found", "text/plain", "no");
    }
};

struct Outcome {
    bool ok = false;
    QString path;      // local file on success
    QString message;   // reason on failure
};

static Outcome run(UrlImportService &svc, const QString &input, int timeoutMs = 10000)
{
    Outcome o;
    QEventLoop loop;
    auto c1 = QObject::connect(&svc, &UrlImportService::downloaded, &loop,
                               [&](const QString &url, bool) {
                                   o.ok = true;
                                   o.path = QUrl(url).toLocalFile();
                                   loop.quit();
                               });
    auto c2 = QObject::connect(&svc, &UrlImportService::failed, &loop,
                               [&](const QString &m) {
                                   o.message = m;
                                   loop.quit();
                               });
    QTimer::singleShot(timeoutMs, &loop, [&] { o.message = "timed out"; loop.quit(); });
    svc.fetch(input);
    if (svc.busy())
        loop.exec();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    return o;
}

static void expectArxiv(const QString &input, const QString &pdf,
                        const QString &file, const QString &ident)
{
    const auto t = UrlImportService::resolve(input);
    check(QStringLiteral("resolve %1").arg(input),
          t.pdfUrl.toString() == pdf && t.fileName == file && t.identifier == ident,
          QStringLiteral("%1 | %2 | %3").arg(t.pdfUrl.toString(), t.fileName, t.identifier));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // ── ISOLATION (see dialogs/main.cpp) ─────────────────────────────
    QStandardPaths::setTestModeEnabled(true);
    const QString scratch =
        QDir::tempPath() + QStringLiteral("/ai-reader-urlimport-harness");
    QDir(scratch).removeRecursively();
    StorageIdentity::apply({QStringLiteral("ai-reader-urlimport-test"),
                            QStringLiteral("ai-reader-urlimport-test.invalid"),
                            QStringLiteral("UrlImportHarness")},
                           scratch);
    {
        const QSettings probe;
        const bool safe = probe.fileName().startsWith(scratch)
                       && probe.format() != QSettings::NativeFormat;
        check("the harness writes a settings file of its own, not a real one",
              safe, probe.fileName());
        if (!safe)
            return 2;
    }

    // ── What a pasted string means ───────────────────────────────────
    const QString A = QStringLiteral("https://arxiv.org/pdf/2609.02309");
    const QString F = QStringLiteral("arXiv-2609.02309.pdf");
    const QString I = QStringLiteral("arXiv:2609.02309");
    expectArxiv("https://arxiv.org/abs/2609.02309", A, F, I);
    expectArxiv("  https://arxiv.org/abs/2609.02309  ", A, F, I);
    expectArxiv("arxiv.org/abs/2609.02309", A, F, I);
    expectArxiv("http://www.arxiv.org/abs/2609.02309", A, F, I);
    expectArxiv("https://export.arxiv.org/abs/2609.02309", A, F, I);
    expectArxiv("https://arxiv.org/pdf/2609.02309.pdf", A, F, I);
    expectArxiv("https://arxiv.org/abs/2609.02309?context=cs", A, F, I);
    expectArxiv("2609.02309", A, F, I);
    expectArxiv("arXiv:2609.02309", A, F, I);
    expectArxiv("https://huggingface.co/papers/2609.02309", A, F, I);
    expectArxiv("https://www.alphaxiv.org/abs/2609.02309", A, F, I);
    expectArxiv("https://doi.org/10.48550/arXiv.2609.02309", A, F, I);
    expectArxiv("10.48550/arXiv.2609.02309", A, F, I);
    expectArxiv("https://arxiv.org/pdf/2609.02309v2", A + "v2",
                "arXiv-2609.02309v2.pdf", I);
    expectArxiv("https://arxiv.org/html/1706.03762v7",
                "https://arxiv.org/pdf/1706.03762v7", "arXiv-1706.03762v7.pdf",
                "arXiv:1706.03762");
    expectArxiv("https://arxiv.org/abs/hep-th/9901001",
                "https://arxiv.org/pdf/hep-th/9901001", "arXiv-hep-th_9901001.pdf",
                "arXiv:hep-th/9901001");
    expectArxiv("math.GT/0309136", "https://arxiv.org/pdf/math.GT/0309136",
                "arXiv-math.GT_0309136.pdf", "arXiv:math.GT/0309136");

    {
        const auto t = UrlImportService::resolve(
            "https://openreview.net/forum?id=YicbFdNTTy&noteId=abc");
        check("OpenReview forum page → its PDF",
              t.pdfUrl.toString() == "https://openreview.net/pdf?id=YicbFdNTTy"
                  && t.fileName == "OpenReview-YicbFdNTTy.pdf" && t.identifier.isEmpty(),
              t.pdfUrl.toString());
        const auto g = UrlImportService::resolve("https://example.org/a/b.pdf");
        check("any other link is fetched as it is",
              g.pdfUrl.toString() == "https://example.org/a/b.pdf" && g.fileName.isEmpty());
        const auto list = UrlImportService::resolve("https://arxiv.org/list/cs.CL/recent");
        check("an arXiv page that isn't a paper falls back to the generic path",
              list.pdfUrl.toString() == "https://arxiv.org/list/cs.CL/recent"
                  && list.identifier.isEmpty());
    }
    for (const char *junk : {"", "   ", "hello world", "not a link", "ftp://x.org/a.pdf",
                             "javascript:alert(1)", "12345", "file:///etc/passwd"}) {
        check(QStringLiteral("not a link: \"%1\"").arg(junk),
              !UrlImportService::resolve(junk).pdfUrl.isValid());
    }

    // ── The download itself ──────────────────────────────────────────
    FakeWeb web;
    if (!web.listen(QHostAddress::LocalHost)) {
        check("the stand-in web server listens", false);
        return 2;
    }
    QTemporaryDir tmp;
    const QString downloads = tmp.path() + "/Downloads";
    const QString papers = tmp.path() + "/papers";
    QDir().mkpath(papers);
    Library library;
    UrlImportService svc(nullptr, nullptr, &library);
    check("the default fallback is the system's Downloads folder",
          svc.downloadDir() == UrlImportService::defaultFallbackDir(),
          svc.downloadDir());
    svc.setFallbackDir(downloads);
    check("with no folder open, downloads go to Downloads",
          library.currentFolder().isEmpty() && svc.downloadDir() == downloads,
          svc.downloadDir());

    auto readAll = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };

    Outcome o = run(svc, web.base() + "/files/attention.pdf");
    check("a direct PDF link is saved under the URL's own name",
          o.ok && QFileInfo(o.path).fileName() == "attention.pdf", o.path + o.message);
    check("...byte for byte", readAll(o.path) == kPdf);
    check("...and the service is idle again", !svc.busy());

    const int before = web.hits;
    o = run(svc, web.base() + "/files/attention.pdf");
    check("the same PDF again reuses the file instead of making a copy",
          o.ok && QFileInfo(o.path).fileName() == "attention.pdf"
              && !QFile::exists(downloads + "/attention (2).pdf"),
          o.path);
    check("(it was still asked for — a generic link can change)", web.hits == before + 1);

    o = run(svc, web.base() + "/download?id=7");
    check("Content-Disposition names the file, sanitised",
          o.ok && QFileInfo(o.path).fileName() == "Deep Nets_ a study.pdf", o.path + o.message);

    o = run(svc, web.base() + "/landing");
    check("a landing page's citation_pdf_url is followed to the PDF",
          o.ok && readAll(o.path) == kPdf, o.path + o.message);

    o = run(svc, web.base() + "/loop");
    check("two pages pointing at each other are followed once, not for ever",
          !o.ok && o.message.contains("web page"), o.message);

    o = run(svc, web.base() + "/page");
    check("a page with no PDF in it is refused with a reason",
          !o.ok && o.message.contains("web page"), o.message);

    o = run(svc, web.base() + "/nothing-here");
    check("a 404 says so", !o.ok && o.message.contains("404"), o.message);

    o = run(svc, "hello world");
    check("text that isn't a link fails at once, without a request",
          !o.ok && !svc.busy() && !o.message.isEmpty(), o.message);

    // An arXiv file already on disk is the same paper: no request at all.
    QFile seeded(downloads + "/arXiv-2609.02309.pdf");
    check("seed an earlier download", seeded.open(QIODevice::WriteOnly));
    seeded.write(kPdf);
    seeded.close();
    o = run(svc, "https://arxiv.org/abs/2609.02309", 2000);
    check("an arXiv paper already downloaded isn't fetched again",
          o.ok && o.path == QFileInfo(seeded).absoluteFilePath(), o.path + o.message);

    // Cancelling mid-flight: a server that accepts and never answers.
    {
        QTcpServer silent;
        silent.listen(QHostAddress::LocalHost);
        int failed = 0, done = 0;
        auto c1 = QObject::connect(&svc, &UrlImportService::failed, [&] { ++failed; });
        auto c2 = QObject::connect(&svc, &UrlImportService::downloaded, [&] { ++done; });
        svc.fetch(QStringLiteral("http://127.0.0.1:%1/slow.pdf").arg(silent.serverPort()));
        check("a download in flight reports busy", svc.busy());
        QEventLoop l; QTimer::singleShot(200, &l, &QEventLoop::quit); l.exec();
        svc.cancel();
        QTimer::singleShot(200, &l, &QEventLoop::quit); l.exec();
        check("cancel stops it quietly: idle, no failure, no file",
              !svc.busy() && failed == 0 && done == 0, svc.status());
        QObject::disconnect(c1);
        QObject::disconnect(c2);
    }

    // ── With a folder open in the folder pane ────────────────────────
    {
        int changed = 0;
        auto c = QObject::connect(&svc, &UrlImportService::downloadFolderChanged,
                                  [&] { ++changed; });
        library.openFolder(QUrl::fromLocalFile(papers));
        check("opening a folder moves the download target there, and says so",
              svc.downloadDir() == QDir::cleanPath(papers) && changed == 1,
              svc.downloadDir());
        o = run(svc, web.base() + "/download?id=7");
        check("...and the PDF lands in that folder",
              o.ok && QFileInfo(o.path).absolutePath() == QFileInfo(papers).absoluteFilePath(),
              o.path + o.message);
        // The copy seeded in Downloads above is not in this folder, so this
        // has to start a real download (cancelled at once: no network here).
        svc.fetch(QStringLiteral("https://arxiv.org/abs/2609.02309"));
        check("an arXiv paper only in Downloads is not 'already downloaded' here",
              svc.busy());
        svc.cancel();
        library.close();
        check("closing the folder sends downloads back to Downloads",
              svc.downloadDir() == downloads && changed == 2, svc.downloadDir());
        QObject::disconnect(c);
    }

    if (qEnvironmentVariableIntValue("URLIMPORT_LIVE")) {
        seeded.remove();
        o = run(svc, "https://arxiv.org/abs/2609.02309", 120000);
        check("LIVE: arXiv 2609.02309 downloads as a real PDF",
              o.ok && QFileInfo(o.path).size() > 10000
                  && readAll(o.path).startsWith("%PDF-")
                  && QFileInfo(o.path).fileName() == "arXiv-2609.02309.pdf",
              o.ok ? QStringLiteral("%1 bytes").arg(QFileInfo(o.path).size()) : o.message);
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
