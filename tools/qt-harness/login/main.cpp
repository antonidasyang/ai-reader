// The one page of this app anybody ever sees in a browser.
//
// The sign-in round trip ends on a page the app itself serves on 127.0.0.1,
// and nothing else in the test suite ever looks at it: it is HTML built by
// string substitution in C++, which is exactly where a stray placeholder or a
// percent sign that QString::arg does not escape goes unnoticed until a user
// sends a screenshot of a page reading "height:100%%".

#include "LocalHttpServer.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QRegularExpression>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <QtGlobal>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

static void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Ask the loopback server for one URL and read the whole response.
//
// Through a real event loop, not waitForReadyRead: the server lives in this
// same process, and its accept and its readyRead are delivered by the loop.
// Blocking on the client socket would leave the request unanswered for ever.
static QString fetch(quint16 port, const QString &target)
{
    QTcpSocket sock;
    QByteArray out;
    QEventLoop loop;
    QObject::connect(&sock, &QIODevice::readyRead, &loop,
                     [&] { out += sock.readAll(); });
    QObject::connect(&sock, &QAbstractSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&sock, &QAbstractSocket::connected, &loop, [&] {
        sock.write("GET " + target.toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                   "Connection: close\r\n\r\n");
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);   // never hang the run
    sock.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();
    out += sock.readAll();
    return QString::fromUtf8(out);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    LocalHttpServer server;
    check("the app can listen for the browser to come back", server.listen(),
          QStringLiteral("port %1").arg(server.port()));
    if (!server.isListening())
        return 2;

    // ── The page after a successful sign-in ─────────────────────────
    const QString ok = fetch(server.port(),
                             QStringLiteral("/login/cas?access=a&refresh=b&state=s"
                                            "&name=%E6%9D%A8%E5%A5%9A%E8%AF%9A"));
    pump(50);
    check("the browser gets a page rather than an error",
          ok.startsWith(QLatin1String("HTTP/1.1 200"))
              && ok.contains(QLatin1String("Content-Type: text/html; charset=utf-8")));
    check("it says the sign-in worked",
          ok.contains(QLatin1String("<h1>")) && !ok.contains(QLatin1String("failed")));
    check("it greets the person who just signed in",
          ok.contains(QString::fromUtf8("杨奚诚")));
    check("it offers a way out of the browser",
          ok.contains(QLatin1String("closeMe()")) && ok.contains(QLatin1String("<button")));
    check("it carries the app's own mark, inline",
          ok.contains(QLatin1String("data:image/svg+xml;base64,")));
    check("nothing on the page is still a placeholder",
          !ok.contains(QRegularExpression(QStringLiteral("%[1-9]"))),
          ok.contains(QRegularExpression(QStringLiteral("%[1-9]")))
              ? QStringLiteral("found one") : QString());
    check("and no doubled percent leaked out of the template",
          !ok.contains(QLatin1String("%%")));
    check("the stylesheet survived the substitution",
          ok.contains(QLatin1String("height:100%;"))
              && ok.contains(QLatin1String("border-radius:50%;")));
    check("the page fetches nothing a browser could fail to load",
          !ok.contains(QLatin1String("http://")) && !ok.contains(QLatin1String("https://")));

    // ── The page when it did not work ───────────────────────────────
    const QString bad = fetch(server.port(),
                              QStringLiteral("/login/cas?error=cas%20failed&state=s"));
    pump(50);
    check("a failed sign-in says so instead of pretending",
          bad.startsWith(QLatin1String("HTTP/1.1 200"))
              && bad.contains(QLatin1String("<h1>"))
              && !bad.contains(QString::fromUtf8("杨奚诚")));
    check("...and still offers the way out",
          bad.contains(QLatin1String("<button")));

    // ── A name is text from elsewhere, not markup ───────────────────
    const QString evil = fetch(server.port(),
                               QStringLiteral("/login/cas?access=a&name=%3Cscript%3Ealert(1)%3C/script%3E"));
    pump(50);
    check("a name cannot smuggle markup into the page",
          !evil.contains(QLatin1String("<script>alert(1)")),
          evil.contains(QLatin1String("&lt;script&gt;")) ? QStringLiteral("escaped")
                                                         : QStringLiteral("absent"));

    check("an unknown path is still a 404",
          fetch(server.port(), QStringLiteral("/nope"))
              .startsWith(QLatin1String("HTTP/1.1 404")));

    // Leave the page itself next to the binary: assertions cannot tell you
    // whether it looks right, and a file a browser can open can.
    {
        const int blank = ok.indexOf(QLatin1String("\r\n\r\n"));
        QFile out(QCoreApplication::applicationDirPath()
                  + QStringLiteral("/login-page.html"));
        if (blank > 0 && out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(ok.mid(blank + 4).toUtf8());
            qInfo().noquote() << "the page itself:" << out.fileName();
        }
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
