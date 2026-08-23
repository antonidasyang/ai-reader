#pragma once

// An OpenAI-compatible endpoint that streams slowly and never finishes on its
// own, so a test can catch translations mid-flight and cancel them.
//
// It also counts what matters for that: how many requests arrived, how many
// sockets are still open, and how many chunks went out. A cancel that really
// aborts shows up here as the sockets closing and the chunk count going flat.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class FakeLlm : public QObject
{
public:
    explicit FakeLlm(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                hook(s);
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    int requests() const { return m_requests; }
    int openStreams() const { return int(m_timers.size()); }
    int chunksSent() const { return m_chunks; }
    // How many chunks each stream sends before it would finish. Left high so
    // nothing completes by itself while a test is looking at it.
    void setChunkLimit(int n) { m_chunkLimit = n; }

private:
    void hook(QTcpSocket *s)
    {
        connect(s, &QTcpSocket::readyRead, this, [this, s] {
            m_buf[s] += s->readAll();
            if (!m_buf[s].contains("\r\n\r\n"))
                return;                       // headers still arriving
            if (m_timers.contains(s))
                return;                       // already streaming
            ++m_requests;
            s->write("HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/event-stream\r\n"
                     "Cache-Control: no-cache\r\n"
                     "Connection: keep-alive\r\n\r\n");
            s->flush();

            auto *t = new QTimer(this);
            t->setInterval(120);
            int *sent = new int(0);
            connect(t, &QTimer::timeout, this, [this, s, t, sent] {
                if (s->state() != QAbstractSocket::ConnectedState) {
                    stop(s);
                    delete sent;
                    return;
                }
                if (*sent >= m_chunkLimit) {
                    s->write("data: [DONE]\n\n");
                    s->flush();
                    s->disconnectFromHost();
                    stop(s);
                    delete sent;
                    return;
                }
                const QJsonObject frame{
                    {"choices", QJsonArray{QJsonObject{
                         {"delta", QJsonObject{{"content",
                              QStringLiteral("chunk%1 ").arg(*sent)}}}}}}};
                s->write("data: "
                         + QJsonDocument(frame).toJson(QJsonDocument::Compact)
                         + "\n\n");
                s->flush();
                ++*sent;
                ++m_chunks;
            });
            m_timers.insert(s, t);
            t->start();
        });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            stop(s);
            m_buf.remove(s);
            s->deleteLater();
        });
    }

    void stop(QTcpSocket *s)
    {
        if (QTimer *t = m_timers.take(s)) {
            t->stop();
            t->deleteLater();
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QHash<QTcpSocket *, QTimer *> m_timers;
    int m_requests = 0;
    int m_chunks = 0;
    int m_chunkLimit = 10000;
};
