#include "LocalHttpServer.h"

#include <QFile>
#include <QHostAddress>
#include <QLocale>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

LocalHttpServer::LocalHttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this,
            &LocalHttpServer::onNewConnection);
}

LocalHttpServer::~LocalHttpServer() = default;

bool LocalHttpServer::isListening() const
{
    return m_server && m_server->isListening();
}

bool LocalHttpServer::listen(quint16 first, quint16 last)
{
    if (isListening())
        return true;
    for (quint16 p = first; p <= last; ++p) {
        if (m_server->listen(QHostAddress::LocalHost, p)) {
            m_port = p;
            return true;
        }
    }
    return false;
}

void LocalHttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { handle(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void LocalHttpServer::handle(QTcpSocket *sock)
{
    // Accumulate until the request line (first CRLF) is complete; the whole
    // payload (access/refresh tokens) lives in that line's query string.
    QByteArray buf = sock->property("buf").toByteArray();
    buf += sock->readAll();
    sock->setProperty("buf", buf);
    const int eol = buf.indexOf("\r\n");
    if (eol < 0)
        return;

    const QByteArray line = buf.left(eol);
    const int sp1 = line.indexOf(' ');
    const int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) {
        respond(sock, 400, "Bad request");
        return;
    }
    const QByteArray target = line.mid(sp1 + 1, sp2 - sp1 - 1);
    const QUrl url(QString::fromUtf8(target));
    if (url.path() != QLatin1String("/login/cas")) {
        respond(sock, 404, "Not found");
        return;
    }

    const QUrlQuery q(url);
    const QString access = q.queryItemValue(QStringLiteral("access"), QUrl::FullyDecoded);
    const QString refresh = q.queryItemValue(QStringLiteral("refresh"), QUrl::FullyDecoded);
    const QString state = q.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
    const QString error = q.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);

    const QString name = q.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded);
    respond(sock, 200, resultPage(error, name));
   emit loginResult(access, refresh, state, error);
}

// The page the browser is left looking at after the sign-in round trip.
//
// It is the only part of this app a user ever sees in a browser, so it is
// worth more than a sentence in the default serif: a card with the app's own
// mark on it, what happened, who they are, and a way out. Everything is
// inline -- there is no second request this server would answer, and a page
// that fetched anything would show a broken image.
QByteArray LocalHttpServer::resultPage(const QString &error,
                                       const QString &name) const
{
    // The mark ships in the binary; a data URI keeps the page to one request.
    QString logo;
    QFile svg(QStringLiteral(":/icons/app.svg"));
    if (svg.open(QIODevice::ReadOnly))
        logo = QStringLiteral("data:image/svg+xml;base64,")
             + QString::fromLatin1(svg.readAll().toBase64());

    const bool ok = error.isEmpty();
    const QString heading = ok ? tr("Signed in") : tr("Sign-in failed");
    const QString detail =
        ok ? (name.trimmed().isEmpty()
                  ? tr("You can close this page and go back to AI Reader.")
                  : tr("Welcome back, %1").arg(name.toHtmlEscaped()))
           : tr("Go back to AI Reader and try again.");
    // The mark of the outcome: a tick, or a cross. Drawn rather than
    // fetched, and sized in the stylesheet below.
    const QString glyph = ok
        ? QStringLiteral("<svg viewBox='0 0 24 24' fill='none' stroke='#fff' "
                         "stroke-width='2.5' stroke-linecap='round' "
                         "stroke-linejoin='round'><path d='M4 12.5l5.2 5.2L20 7'/></svg>")
        : QStringLiteral("<svg viewBox='0 0 24 24' fill='none' stroke='#fff' "
                         "stroke-width='2.5' stroke-linecap='round' "
                         "stroke-linejoin='round'><path d='M6 6l12 12M18 6L6 18'/></svg>");

    const QString page = QStringLiteral(R"HTML(<!doctype html>
<html lang="%1"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%2 - AI Reader</title>
<style>
  :root { color-scheme: light dark;
          --page1:#eaf1fb; --page2:#dfe9f8; --card:#fff; --ink:#16223a;
          --dim:#6b7488; --line:#e3e8f0; --accent:#1565c0; --good:#37b24d; --bad:#e03131; }
  @media (prefers-color-scheme: dark) {
    :root { --page1:#151821; --page2:#11141b; --card:#1c2029; --ink:#e6e9ef;
            --dim:#9aa2b1; --line:#2b313c; --accent:#7aa7ff; }
  }
  html,body { height:100%; margin:0; }
  body { display:flex; align-items:center; justify-content:center;
         background:linear-gradient(160deg,var(--page1),var(--page2));
         font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",
                     "PingFang SC","Microsoft YaHei",Roboto,sans-serif;
         color:var(--ink); }
  .card { width:min(92vw,460px); box-sizing:border-box; background:var(--card);
          border-radius:18px; padding:44px 40px 36px; text-align:center;
          box-shadow:0 18px 50px rgba(20,35,70,.10); }
  .brand { display:flex; align-items:center; justify-content:center; gap:12px;
           margin-bottom:30px; }
  .brand img { width:56px; height:56px; border-radius:12px; }
  .brand span { font-size:26px; font-weight:700; color:var(--accent);
                letter-spacing:.5px; }
  .mark { width:104px; height:104px; margin:0 auto 22px; border-radius:50%;
          display:flex; align-items:center; justify-content:center;
          background:color-mix(in srgb, var(--tone) 14%, transparent); }
  .mark i { width:64px; height:64px; border-radius:50%; background:var(--tone);
            display:flex; align-items:center; justify-content:center; }
  .mark svg { width:34px; height:34px; }
  h1 { font-size:25px; margin:0 0 12px; }
  p  { margin:0; color:var(--dim); font-size:15px; line-height:1.6; }
  p b { color:var(--accent); font-weight:600; }
  button { margin-top:30px; padding:11px 34px; font-size:15px; cursor:pointer;
           color:var(--ink); background:transparent; border:1px solid var(--line);
           border-radius:8px; font-family:inherit; }
  button:hover { border-color:var(--accent); color:var(--accent); }
  .hint { margin-top:14px; font-size:13px; color:var(--dim); display:none; }
</style></head>
<body><div class="card">
  <div class="brand">%3<span>AI Reader</span></div>
  <div class="mark" style="--tone:%4"><i>%5</i></div>
  <h1>%6</h1>
  <p>%7</p>
  <button onclick="closeMe()">%8</button>
  <p class="hint" id="hint">%9</p>
</div>
<script>
  function closeMe() {
    window.close();
    // A tab the browser opened itself will not close on script's say-so, so
    // say what to do instead rather than looking broken.
    setTimeout(function () {
      document.getElementById('hint').style.display = 'block';
    }, 250);
  }
</script></body></html>
)HTML")
        .arg(QLocale().name().startsWith(QLatin1String("zh"))
                 ? QStringLiteral("zh-CN") : QStringLiteral("en"),
             heading,
             logo.isEmpty() ? QString()
                            : QStringLiteral("<img src=\"%1\" alt=\"\">").arg(logo),
             ok ? QStringLiteral("var(--good)") : QStringLiteral("var(--bad)"),
             glyph,
             heading,
             ok && !name.trimmed().isEmpty()
                 ? tr("Welcome back, <b>%1</b>").arg(name.toHtmlEscaped())
                 : detail,
             tr("Close this page"),
             tr("You can close this tab now."));
    return page.toUtf8();
}

void LocalHttpServer::respond(QTcpSocket *sock, int status, const QByteArray &body)
{
    QByteArray resp = "HTTP/1.1 " + QByteArray::number(status)
        + (status == 200 ? " OK" : " ERR") + "\r\n"
        + "Content-Type: text/html; charset=utf-8\r\n"
        + "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
        + "Connection: close\r\n\r\n" + body;
    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}
