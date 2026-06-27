#include "ApiClient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
{
}

void ApiClient::setBaseUrl(const QString &url)
{
    QString u = url.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    m_baseUrl = u;
}

void ApiClient::get(const QString &path, Handler h, bool allowRefresh)
{
    send("GET", path, {}, std::move(h), allowRefresh);
}

void ApiClient::post(const QString &path, const QJsonObject &body, Handler h,
                     bool allowRefresh)
{
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact),
         std::move(h), allowRefresh);
}

void ApiClient::patch(const QString &path, const QJsonObject &body, Handler h,
                      bool allowRefresh)
{
    send("PATCH", path, QJsonDocument(body).toJson(QJsonDocument::Compact),
         std::move(h), allowRefresh);
}

void ApiClient::del(const QString &path, Handler h, bool allowRefresh)
{
    send("DELETE", path, {}, std::move(h), allowRefresh);
}

void ApiClient::send(const QByteArray &verb, const QString &path,
                     const QByteArray &data, Handler h, bool allowRefresh)
{
    QNetworkRequest req{QUrl(m_baseUrl + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    if (!m_accessToken.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());

    QNetworkReply *reply = nullptr;
    if (verb == "GET")
        reply = m_nam.get(req);
    else if (verb == "POST")
        reply = m_nam.post(req, data);
    else if (verb == "DELETE")
        reply = m_nam.deleteResource(req);
    else
        reply = m_nam.sendCustomRequest(req, verb, data);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();

        // One transparent refresh + retry on an expired access token.
        if (status == 401 && allowRefresh && m_refreshFn) {
            m_refreshFn([=](bool refreshed) {
                if (refreshed)
                    send(verb, path, data, h, /*allowRefresh=*/false);
                else if (h)
                    h(false, status, QJsonDocument::fromJson(bytes));
            });
            return;
        }

        const bool ok = status >= 200 && status < 300;
        if (h)
            h(ok, status, QJsonDocument::fromJson(bytes));
    });
}
