#include "LocalHttpServer.h"

#include <QHostAddress>
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

    const QByteArray html =
        error.isEmpty()
            ? QByteArrayLiteral(
                  "<!doctype html><meta charset=utf-8><body style='font-family:"
                  "sans-serif;text-align:center;padding-top:60px'><h2>登录成功"
                  "</h2><p>可以关闭此页面,返回 ai-reader。</p></body>")
            : QByteArrayLiteral(
                  "<!doctype html><meta charset=utf-8><body style='font-family:"
                  "sans-serif;text-align:center;padding-top:60px'><h2>登录失败"
                  "</h2><p>请返回 ai-reader 重试。</p></body>");
    respond(sock, 200, html);
    emit loginResult(access, refresh, state, error);
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
