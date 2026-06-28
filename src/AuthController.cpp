#include "AuthController.h"
#include "ApiClient.h"
#include "LocalHttpServer.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

// qtkeychain (same in-tree header the LLM API key uses).
#include <keychain.h>

using QKeychain::DeletePasswordJob;
using QKeychain::Job;
using QKeychain::ReadPasswordJob;
using QKeychain::WritePasswordJob;

namespace {
constexpr auto kRefreshKey = "server/refreshToken";
} // namespace

AuthController::AuthController(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    m_serverUrl = m_qs.value(QStringLiteral("server/url"),
                             QStringLiteral("https://aireader.d2ssoft.com"))
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

void AuthController::startCasLogin()
{
    setStatus(QString());
    if (!m_loopback) {
        m_loopback = new LocalHttpServer(this);
        connect(m_loopback, &LocalHttpServer::loginResult, this,
                &AuthController::onCasResult);
    }
    if (!m_loopback->isListening() && !m_loopback->listen()) {
        setStatus(tr("Could not start the local sign-in listener."));
        return;
    }
    m_casState = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setBusy(true);

    QUrl url(m_serverUrl + QStringLiteral("/auth/cas/login"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("port"), QString::number(m_loopback->port()));
    q.addQueryItem(QStringLiteral("state"), m_casState);
    url.setQuery(q);

    if (!QDesktopServices::openUrl(url)) {
        setBusy(false);
        setStatus(tr("Could not open a browser for CAS sign-in."));
    } else {
        setStatus(tr("Continue sign-in in your browser…"));
    }
}

void AuthController::onCasResult(const QString &access, const QString &refresh,
                                 const QString &state, const QString &error)
{
    setBusy(false);
    if (state != m_casState) {
        setStatus(tr("Sign-in rejected (state mismatch)."));
        return;
    }
    if (!error.isEmpty()) {
        setStatus(tr("CAS sign-in failed: %1").arg(error));
        return;
    }
    if (access.isEmpty()) {
        setStatus(tr("CAS sign-in returned no token."));
        return;
    }
    m_api->setAccessToken(access);
    m_refreshToken = refresh;
    writeRefreshToKeychain(refresh);
    m_qs.setValue(QStringLiteral("server/sessionActive"), true);
    setAuthenticated(true);
    setStatus(tr("Signed in."));
    fetchMe();
}

void AuthController::fetchMe()
{
    m_api->get(QStringLiteral("/auth/me"),
               [this](bool ok, int, const QJsonDocument &doc) {
                   if (!ok)
                       return;
                   const QJsonObject o = doc.object();
                   m_userId = o.value(QStringLiteral("userId")).toString();
                   m_userEmail = o.value(QStringLiteral("email")).toString();
                   m_userDisplayName =
                       o.value(QStringLiteral("displayName")).toString();
                   emit userChanged();
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
    m_userDisplayName.clear();
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
    m_userDisplayName = user.value(QStringLiteral("displayName")).toString();
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
