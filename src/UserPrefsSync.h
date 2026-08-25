#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QTimer>

class ApiClient;
class AuthController;
class Settings;

// Makes the account-class settings follow the user from machine to machine.
//
// It is an additive layer and nothing else: Settings stays the authority for
// the running app, every setter still saves locally the instant it is called,
// and a reader who is offline or never signs in sees exactly the behaviour
// they saw before this class existed. Nothing here can block a save, refuse
// one, or put a dialog in front of anybody -- the worst a failure does is
// leave a sentence in lastError for a status line that may not exist yet.
//
// Two endpoints, both behind the bearer token ApiClient already sends:
//
//   GET  /me/prefs  -> { data: {...}, version: "<n>" }   ("0" and {} if new)
//   PUT  /me/prefs  <- { data: {...}, expectedVersion: "<n>" }
//                   -> { version: "<n>" }   or   409 { version, data }
//
// It pulls once each time the user becomes authenticated (and on demand),
// and pushes on a few seconds' debounce after an account-class setting
// changes here. The version the server last reported, and the exact payload
// we last put there, are both persisted: without them a restart would either
// re-push a payload the account already holds or push with a stale version
// and take a 409 for no reason.
//
// Who wins when the two sides disagree is decided by a three-way merge whose
// base is that stored payload -- see mergeThreeWay() and reconcile() for the
// whole rule, which is the part of this class most able to annoy a user.
class UserPrefsSync : public QObject
{
    Q_OBJECT
    // For a status line somebody may add later. There is deliberately no
    // QML here: a settings sync that interrupts the reader has missed the
    // point of syncing settings.
    Q_PROPERTY(QDateTime lastSyncedAt READ lastSyncedAt NOTIFY lastSyncedAtChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    UserPrefsSync(ApiClient *api, AuthController *auth, Settings *settings,
                  QObject *parent = nullptr);

    QDateTime lastSyncedAt() const { return m_lastSyncedAt; }
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }

    // Read the account copy and reconcile it with this machine. Safe to call
    // at any time; a no-op when signed out.
    Q_INVOKABLE void pullNow();
    // Send whatever the debounce is still holding, right now -- for quitting,
    // or for a Sync button. Also a no-op when signed out.
    Q_INVOKABLE void flushPending();

signals:
    void lastSyncedAtChanged();
    void busyChanged();
    void lastErrorChanged();

private:
    void onAuthChanged();
    void onUserChanged();
    void startIfPossible();
    void onLocalChange();
    void maybePush();
    void push(const QJsonObject &data, const QString &expectedVersion);
    void onConflict(const QJsonObject &body);
    void reconcile(const QJsonObject &serverData, const QString &version);
    // Applies a payload to Settings without mistaking the resulting change
    // signals for something the reader did on this machine.
    void applyLocally(const QJsonObject &payload);

    // The merge. base = what the account held when we last agreed with it.
    QJsonObject mergeThreeWay(const QJsonObject &base, const QJsonObject &local,
                              const QJsonObject &server) const;
    // A payload of our own values plus whatever keys the server holds that
    // this build has never heard of, carried back untouched.
    QJsonObject withForeign(const QJsonObject &known) const;
    // True when two payloads agree on every key this build knows about.
    static bool sameKnownKeys(const QJsonObject &a, const QJsonObject &b);
    // The keys of `server` that are not in Settings::accountSettingKeys().
    static QJsonObject foreignKeysOf(const QJsonObject &server);
    static int knownKeyCount(const QJsonObject &o);

    void rememberBase(const QJsonObject &payload, const QString &version);
    void loadState();
    void saveState();

    void setBusy(bool v);
    void setLastError(const QString &e);
    void touchSynced();

    // Borrowed, all three; they outlive us in main() but QPointer keeps a
    // late reply callback from walking into a dangling object during
    // teardown.
    QPointer<ApiClient> m_api;
    QPointer<AuthController> m_auth;
    QPointer<Settings> m_settings;

    QSettings m_qs;
    QTimer m_pushTimer;
    // Auto-login learns who the user is a moment after it learns that there
    // is one; this waits that moment out so the first reconcile knows whose
    // account it is looking at. It gives up after a few seconds and syncs
    // anyway rather than never syncing at all.
    QTimer m_identityGrace;

    QString m_userId;               // whose account the stored state describes
    QString m_version;              // the version the server last reported
    QJsonObject m_lastPushed;       // the exact payload the account holds
    QJsonObject m_foreignKeys;      // keys from a newer client, passed through
    QDateTime m_lastPushedAt;
    QDateTime m_lastLocalChangeAt;
    QDateTime m_lastSyncedAt;

    // The account payload as it looked the last time we looked. Shared change
    // signals (an API key arriving from the keychain bumps the same signal a
    // model name does) would otherwise stamp a local edit that never happened.
    QJsonObject m_lastSeen;

    QString m_syncedUserId;         // who we pulled for in this session
    bool m_pulled = false;          // a pull has succeeded since sign-in
    bool m_inFlight = false;
    bool m_changedWhileInFlight = false;
    bool m_applyingRemote = false;
    bool m_conflictRetried = false;
    bool m_busy = false;
    QString m_lastError;
};
