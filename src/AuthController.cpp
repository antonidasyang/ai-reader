#include "AuthController.h"
#include "ApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// qtkeychain (same in-tree header the LLM API key uses).
#include <keychain.h>

using QKeychain::DeletePasswordJob;
using QKeychain::Job;
using QKeychain::ReadPasswordJob;
using QKeychain::WritePasswordJob;

namespace {
constexpr auto kRefreshKey = "server/refreshToken";

QString errorMessage(const QJsonDocument &doc, int status)
{
    const QJsonObject o = doc.object();
    const QJsonValue m = o.value(QStringLiteral("message"));
    if (m.isString())
        return m.toString();
    if (m.isArray()) {
        QStringList parts;
        for (const QJsonValue &v : m.toArray())
            parts << v.toString();
        if (!parts.isEmpty())
            return parts.join(QStringLiteral("; "));
    }
    return QObject::tr("Request failed (HTTP %1)").arg(status);
}
} // namespace

AuthController::AuthController(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    m_serverUrl = m_qs.value(QStringLiteral("server/url"),
                             QStringLiteral("http://localhost:3000"))
                      .toString();
    m_api->setBaseUrl(m_serverUrl);
    m_api->setRefreshFn([this](std::function<void(bool)> cb) { refresh(cb); });
    // Only touch the keychain on launch if a cloud session was previously
    // established. Otherwise users who never sign in to the cloud would get a
    // spurious keychain prompt every launch.
    if (m_qs.value(QStringLiteral("server/sessionActive"), false).toBool())
        readRefreshFromKeychain();
}

void AuthController::setServerUrl(const QString &url)
{
    if (url == m_serverUrl)
        return;
    m_serverUrl = url;
    m_qs.setValue(QStringLiteral("server/url"), url);
    m_qs.sync();
    m_api->setBaseUrl(url);
    emit serverUrlChanged();
}

void AuthController::login(const QString &email, const QString &password)
{
    setStatus(QString());
    setBusy(true);
    QJsonObject body{{QStringLiteral("email"), email},
                     {QStringLiteral("password"), password}};
    m_api->post(QStringLiteral("/auth/login"), body,
                [this](bool ok, int status, const QJsonDocument &doc) {
                    setBusy(false);
                    if (ok) {
                        applyAuthResult(doc.object());
                        setStatus(tr("Signed in."));
                    } else {
                        setStatus(errorMessage(doc, status));
                    }
                });
}

void AuthController::registerUser(const QString &email, const QString &password,
                                  const QString &displayName)
{
    setStatus(QString());
    setBusy(true);
    QJsonObject body{{QStringLiteral("email"), email},
                     {QStringLiteral("password"), password}};
    if (!displayName.isEmpty())
        body.insert(QStringLiteral("displayName"), displayName);
    m_api->post(QStringLiteral("/auth/register"), body,
                [this](bool ok, int status, const QJsonDocument &doc) {
                    setBusy(false);
                    if (ok) {
                        applyAuthResult(doc.object());
                        setStatus(tr("Account created."));
                    } else {
                        setStatus(errorMessage(doc, status));
                    }
                });
}

void AuthController::refresh(std::function<void(bool)> cb)
{
    if (m_refreshToken.isEmpty()) {
        if (cb)
            cb(false);
        return;
    }
    QJsonObject body{{QStringLiteral("refreshToken"), m_refreshToken}};
    // allowRefresh=false: never recurse the 401-refresh on the refresh call.
    m_api->post(
        QStringLiteral("/auth/refresh"), body,
        [this, cb](bool ok, int /*status*/, const QJsonDocument &doc) {
            if (ok) {
                applyAuthResult(doc.object());
                if (cb)
                    cb(true);
            } else {
                setAuthenticated(false);
                if (cb)
                    cb(false);
            }
        },
        /*allowRefresh=*/false);
}

void AuthController::logout()
{
    m_refreshToken.clear();
    m_api->setAccessToken(QString());
    m_qs.setValue(QStringLiteral("server/sessionActive"), false);
    clearRefreshInKeychain();
    m_userId.clear();
    m_userEmail.clear();
    emit userChanged();
    setAuthenticated(false);
    setStatus(tr("Signed out."));
}

void AuthController::applyAuthResult(const QJsonObject &obj)
{
    m_api->setAccessToken(obj.value(QStringLiteral("accessToken")).toString());
    m_refreshToken = obj.value(QStringLiteral("refreshToken")).toString();
    writeRefreshToKeychain(m_refreshToken);

    const QJsonObject user = obj.value(QStringLiteral("user")).toObject();
    m_userId = user.value(QStringLiteral("id")).toString();
    m_userEmail = user.value(QStringLiteral("email")).toString();
    // Remember a session exists so the next launch may auto-login (and only
    // then reads the keychain).
    m_qs.setValue(QStringLiteral("server/sessionActive"), true);
    emit userChanged();
    setAuthenticated(true);
}

void AuthController::setAuthenticated(bool v)
{
    if (v == m_authenticated)
        return;
    m_authenticated = v;
    emit authenticatedChanged();
}

void AuthController::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void AuthController::setBusy(bool v)
{
    if (v == m_busy)
        return;
    m_busy = v;
    emit busyChanged();
}

void AuthController::readRefreshFromKeychain()
{
    auto *job = new ReadPasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QLatin1String(kRefreshKey));
    job->setInsecureFallback(true);
    connect(job, &Job::finished, this, [this](Job *j) {
        auto *r = static_cast<ReadPasswordJob *>(j);
        if (r->error() == QKeychain::NoError && !r->textData().isEmpty()) {
            m_refreshToken = r->textData();
            // Auto-login: exchange the stored refresh token for an access token.
            refresh([](bool) {});
        }
    });
    job->start();
}

void AuthController::writeRefreshToKeychain(const QString &value)
{
    auto *job = new WritePasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QLatin1String(kRefreshKey));
    job->setTextData(value);
    job->setInsecureFallback(true);
    job->start();
}

void AuthController::clearRefreshInKeychain()
{
    auto *job = new DeletePasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QLatin1String(kRefreshKey));
    job->setInsecureFallback(true);
    job->start();
}
