#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

// Tiny loopback HTTP server for the desktop CAS flow (mirrors lightning-ai's
// LocalHttpServer). After the user authenticates in the browser, the ai-reader
// backend redirects to http://127.0.0.1:<port>/login/cas?access=…&refresh=…&
// state=… (or …?error=…). This catches that one request, hands the values back
// via loginResult, and shows a "you can close this tab" page.
class LocalHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalHttpServer(QObject *parent = nullptr);
    ~LocalHttpServer() override;

    bool listen(quint16 first = 9876, quint16 last = 9880);
    bool isListening() const;
    quint16 port() const { return m_port; }

signals:
    void loginResult(const QString &access, const QString &refresh,
                     const QString &state, const QString &error);

private slots:
    void onNewConnection();

private:
    void handle(QTcpSocket *sock);
    void respond(QTcpSocket *sock, int status, const QByteArray &body);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
};
