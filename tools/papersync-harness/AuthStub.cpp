// Harness-only AuthController. The real one signs in through CAS in a system
// browser, which a headless test can't drive; this is the same class from the
// same header with a different implementation, where startCasLogin() simply
// adopts the identity in TEST_USER_ID / TEST_USER_EMAIL.
#include "AuthController.h"

#include "ApiClient.h"

AuthController::AuthController(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    m_serverUrl = qEnvironmentVariable("TEST_SERVER_URL");
    if (m_api) {
        m_api->setBaseUrl(m_serverUrl);
        m_api->setAccessToken(QStringLiteral("harness-token"));
    }
}

void AuthController::setServerUrl(const QString &url)
{
    m_serverUrl = url;
    if (m_api)
        m_api->setBaseUrl(url);
    emit serverUrlChanged();
}

void AuthController::startCasLogin()
{
    m_userId = qEnvironmentVariable("TEST_USER_ID");
    m_userEmail = qEnvironmentVariable("TEST_USER_EMAIL");
    m_userDisplayName = m_userEmail;
    emit userChanged();
    setAuthenticated(true);
}

void AuthController::logout()
{
    m_userId.clear();
    m_userEmail.clear();
    emit userChanged();
    setAuthenticated(false);
}

void AuthController::refresh(std::function<void(bool)> cb)
{
    if (cb)
        cb(m_authenticated);
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
