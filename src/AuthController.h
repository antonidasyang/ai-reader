#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <functional>

class ApiClient;
class LocalHttpServer;

// Owns the user session: register / login / refresh against the cloud backend,
// the server URL, and the current user. The refresh token persists in the OS
// keychain (qtkeychain, same as the LLM API key) so the app auto-logs-in on
// launch; the short-lived access token lives only in memory on ApiClient.
class AuthController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY userChanged)
    Q_PROPERTY(QString userEmail READ userEmail NOTIFY userChanged)
    Q_PROPERTY(QString userDisplayName READ userDisplayName NOTIFY userChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AuthController(ApiClient *api, QObject *parent = nullptr);

    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &url);
    bool authenticated() const { return m_authenticated; }
    QString userId() const { return m_userId; }
    QString userEmail() const { return m_userEmail; }
    QString userDisplayName() const { return m_userDisplayName; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    // CAS sign-in: opens the system browser; the backend validates the ticket
    // and redirects the JWTs back to our loopback (LocalHttpServer). CAS is the
    // only login method.
    Q_INVOKABLE void startCasLogin();
    Q_INVOKABLE void logout();

    // Obtain a fresh access token from the stored refresh token. Used both on
    // startup (auto-login) and by ApiClient as its 401 refresh callback.
    void refresh(std::function<void(bool)> cb);

signals:
    void serverUrlChanged();
    void authenticatedChanged();
    void userChanged();
    void statusChanged();
    void busyChanged();

private:
    void onCasResult(const QString &access, const QString &refresh,
                     const QString &state, const QString &error);
    void fetchMe();
    void applyAuthResult(const QJsonObject &obj);
    void setAuthenticated(bool v);
    void setStatus(const QString &s);
    void setBusy(bool v);
    void readRefreshFromKeychain();
    void writeRefreshToKeychain(const QString &value);
    void clearRefreshInKeychain();

    ApiClient *m_api;
    LocalHttpServer *m_loopback = nullptr;
    QSettings m_qs;
    QString m_serverUrl;
    QString m_refreshToken;
    QString m_casState;
    QString m_userId;
    QString m_userEmail;
    QString m_userDisplayName;
    bool m_authenticated = false;
    bool m_busy = false;
    QString m_status;
};
