#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <functional>

// Thin JSON-over-HTTP client for the ai-reader cloud backend. Holds the base
// URL + access token; transparently refreshes the token once on a 401 (via a
// refresh callback installed by AuthController) and retries the request.
class ApiClient : public QObject
{
    Q_OBJECT

public:
    // (ok, httpStatus, parsedJsonBody)
    using Handler =
        std::function<void(bool ok, int status, const QJsonDocument &body)>;
    using RefreshFn = std::function<void(std::function<void(bool)>)>;

    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    QString baseUrl() const { return m_baseUrl; }

    void setAccessToken(const QString &token) { m_accessToken = token; }
    QString accessToken() const { return m_accessToken; }

    void setRefreshFn(RefreshFn fn) { m_refreshFn = std::move(fn); }

    void get(const QString &path, Handler h, bool allowRefresh = true);
    void post(const QString &path, const QJsonObject &body, Handler h,
              bool allowRefresh = true);
    void patch(const QString &path, const QJsonObject &body, Handler h,
               bool allowRefresh = true);
    void del(const QString &path, Handler h, bool allowRefresh = true);

private:
    void send(const QByteArray &verb, const QString &path,
              const QByteArray &data, Handler h, bool allowRefresh);

    QNetworkAccessManager m_nam;
    QString m_baseUrl;
    QString m_accessToken;
    RefreshFn m_refreshFn;
};
