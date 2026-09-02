#pragma once

// A minimal stand-in for the sync backend: enough HTTP for ApiClient to pull,
// push and list projects, with an in-memory object store the test can seed and
// inspect. Lets the harness drive the real SyncEngine / PaperSyncService code
// paths instead of poking at the local SQLite behind their backs.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

// No Q_OBJECT: it declares no signals or slots of its own, and the harness
// links against the app's aggregated moc output, which knows nothing about it.
class FakeSync : public QObject
{
public:
    explicit FakeSync(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                hook(s);
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    // The sockets are the server's children and outlive m_buf, which is
    // declared after it: their disconnected() slot then removed a key from
    // a hash that was already gone, and every run ended in a segfault after
    // its last check had passed. Taken down first, with their slots
    // detached, while everything they reach is still alive.
    ~FakeSync() override
    {
        for (QTcpSocket *s : m_server.findChildren<QTcpSocket *>()) {
            s->disconnect(this);
            s->abort();
            delete s;
        }
        m_server.close();
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    // Put an object into the store as if some other member had pushed it.
    void seed(const QString &id, const QString &type, const QJsonObject &data)
    {
        ++m_version;
        m_store.insert(id, QJsonObject{
            {"id", id}, {"type", type}, {"data", data},
            {"version", QString::number(m_version)}, {"deleted", false},
            {"updatedAt", data.value("updatedAt")}, {"updatedBy", data.value("author")}});
    }

    // The project this backend serves; ProjectController drops its current
    // selection if the listing doesn't contain it.
    void setProject(const QString &id, const QString &role)
    {
        m_project = QJsonObject{{"id", id}, {"name", "Harness"},
                                {"description", ""}, {"role", role},
                                {"version", "0"}};
    }

    // 0 leaves the field out entirely — what a server from before the limit
    // was raised looks like on the wire.
    void setPushLimit(qint64 bytes) { m_pushLimit = bytes; }

    QList<QJsonObject> pushed() const { return m_pushed; }
    // How many live objects of a type the server holds. Enough to assert
    // that something really reached the project rather than only the local
    // mirror.
    // Every live object of a type, as the server holds it.
    QList<QJsonObject> objectsOfType(const QString &type) const
    {
        QList<QJsonObject> out;
        for (const QJsonObject &o : m_store) {
            if (o.value("type").toString() == type
                && !o.value("deleted").toBool())
                out.append(o.value("data").toObject());
        }
        return out;
    }

    int count(const QString &type) const
    {
        int n = 0;
        for (const QJsonObject &o : m_store) {
            if (o.value("type").toString() == type
                && !o.value("deleted").toBool())
                ++n;
        }
        return n;
    }
    void clearPushed() { m_pushed.clear(); }
    int pullCount() const { return m_pulls; }

private:
    void hook(QTcpSocket *s)
    {
        connect(s, &QTcpSocket::readyRead, this, [this, s] {
            m_buf[s] += s->readAll();
            QByteArray &buf = m_buf[s];
            const int end = buf.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QByteArray head = buf.left(end);
            int len = 0;
            for (const QByteArray &line : head.split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    len = line.mid(15).trimmed().toInt();
            }
            if (buf.size() < end + 4 + len)
                return;   // body still arriving
            const QByteArray body = buf.mid(end + 4, len);
            const QList<QByteArray> req = head.split('\n').value(0).trimmed().split(' ');
            respond(s, req.value(0), QString::fromUtf8(req.value(1)), body);
            buf.clear();
        });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            m_buf.remove(s);
            s->deleteLater();
        });
    }

    void reply(QTcpSocket *s, int code, const QJsonDocument &doc)
    {
        const QByteArray body = doc.toJson(QJsonDocument::Compact);
        QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n";
        out += "Content-Type: application/json\r\n";
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        out += "Connection: keep-alive\r\n\r\n";
        out += body;
        s->write(out);
        s->flush();
    }

    void respond(QTcpSocket *s, const QByteArray &verb, const QString &target,
                 const QByteArray &body)
    {
        const QUrl u(target);
        const QString path = u.path();
        if (verb == "GET" && path.endsWith(QLatin1String("/sync"))) {
            ++m_pulls;
            const qint64 since =
                QUrlQuery(u).queryItemValue(QStringLiteral("since")).toLongLong();
            QJsonArray objs;
            for (const QJsonObject &o : m_store) {
                if (o.value("version").toString().toLongLong() > since)
                    objs.append(o);
            }
            QJsonObject body{{"newVersion", QString::number(m_version)},
                             {"objects", objs},
                             {"hasMore", false}};
            if (m_pushLimit > 0)
                body.insert("pushLimitBytes", double(m_pushLimit));
            reply(s, 200, QJsonDocument(body));
            return;
        }
        if (verb == "POST" && path.endsWith(QLatin1String("/push"))) {
            const QJsonArray in =
                QJsonDocument::fromJson(body).object().value("objects").toArray();
            QJsonArray applied;
            for (const QJsonValue &v : in) {
                const QJsonObject o = v.toObject();
                m_pushed.append(o);
                ++m_version;
                const QString id = o.value("id").toString();
                m_store.insert(id, QJsonObject{
                    {"id", id}, {"type", o.value("type")}, {"data", o.value("data")},
                    {"version", QString::number(m_version)},
                    {"deleted", o.value("deleted")},
                    {"updatedAt", o.value("data").toObject().value("updatedAt")},
                    {"updatedBy", o.value("data").toObject().value("author")}});
                applied.append(id);
            }
            reply(s, 200, QJsonDocument(QJsonObject{
                {"newVersion", QString::number(m_version)},
                {"applied", applied},
                {"conflicts", QJsonArray{}}}));
            return;
        }
        if (verb == "GET" && path == QLatin1String("/projects")) {
            reply(s, 200, QJsonDocument(m_project.isEmpty()
                                            ? QJsonArray{}
                                            : QJsonArray{m_project}));
            return;
        }
        // Everything else the client may probe (member lists, the WS upgrade).
        reply(s, 200, QJsonDocument(QJsonArray{}));
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QMap<QString, QJsonObject> m_store;
    QJsonObject m_project;
    QList<QJsonObject> m_pushed;
    qint64 m_version = 0;
    qint64 m_pushLimit = 32 * 1024 * 1024;
    int m_pulls = 0;
};
