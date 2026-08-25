#include "UserPrefsSync.h"

#include "ApiClient.h"
#include "AuthController.h"
#include "Settings.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>

namespace {
constexpr auto kPath = "/me/prefs";

// Long enough that dragging a font-size spinner is one write instead of
// twenty, short enough that switching machines right after a change does
// what the reader expects.
constexpr int kPushDebounceMs = 4000;
// How long we wait for /auth/me to say who signed in before syncing anyway.
// Syncing without knowing the user is safe (the merge below never lets the
// account overwrite local values silently); it only costs us the ability to
// notice that a *different* person signed in on this machine.
constexpr int kIdentityGraceMs = 5000;

// Persisted next to every other QSettings key, under a prefix of its own.
constexpr auto kQsUserId        = "prefsSync/userId";
constexpr auto kQsVersion       = "prefsSync/version";
constexpr auto kQsBase          = "prefsSync/basePayload";
constexpr auto kQsForeign       = "prefsSync/foreignKeys";
constexpr auto kQsPushedAt      = "prefsSync/lastPushedAt";
constexpr auto kQsLocalChangeAt = "prefsSync/lastLocalChangeAt";
constexpr auto kQsSyncedAt      = "prefsSync/lastSyncedAt";

QJsonObject objFromText(const QString &text)
{
    if (text.isEmpty())
        return {};
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QString textOf(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// The contract says the version is a string. A backend that ever answers with
// a bare number means the same thing, and losing the version would cost every
// later write a pointless 409, so read both shapes.
QString versionOf(const QJsonObject &o, const QString &fallback = QString())
{
    const QJsonValue v = o.value(QStringLiteral("version"));
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(qint64(v.toDouble()));
    return fallback;
}

QDateTime dateFromText(const QString &text)
{
    if (text.isEmpty())
        return {};
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}
} // namespace

UserPrefsSync::UserPrefsSync(ApiClient *api, AuthController *auth,
                             Settings *settings, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_auth(auth)
    , m_settings(settings)
{
    loadState();

    m_pushTimer.setSingleShot(true);
    m_pushTimer.setInterval(kPushDebounceMs);
    connect(&m_pushTimer, &QTimer::timeout, this, [this] { maybePush(); });

    m_identityGrace.setSingleShot(true);
    m_identityGrace.setInterval(kIdentityGraceMs);
    connect(&m_identityGrace, &QTimer::timeout, this, [this] {
        if (m_auth && m_auth->authenticated() && !m_pulled)
            pullNow();
    });

    if (m_settings) {
        m_lastSeen = m_settings->exportAccountSettings();

        // Every signal an account-class setting can raise. Two of them are
        // shared with values that never leave this machine -- an API key
        // arriving from the keychain during startup raises
        // translationConfigChanged -- which is exactly why onLocalChange()
        // compares payloads before it believes anything changed. Over-
        // connecting is therefore free; under-connecting would mean a
        // setting that quietly stops following the account.
        using Sig = void (Settings::*)();
        const Sig sigs[] = {
            &Settings::providerChanged,
            &Settings::modelChanged,
            &Settings::baseUrlChanged,
            &Settings::temperatureChanged,
            &Settings::maxTokensChanged,
            &Settings::contextWindowChanged,
            &Settings::translationConfigChanged,
            &Settings::targetLangChanged,
            &Settings::translationConcurrencyChanged,
            &Settings::translationPromptChanged,
            &Settings::tocPromptChanged,
            &Settings::visionPromptChanged,
            &Settings::chatPromptChanged,
            &Settings::toolBudgetChanged,
            &Settings::chatIncludePaperTextChanged,
            &Settings::chatSendKeyChanged,
            &Settings::analysisConfigChanged,
            &Settings::tocFontSizeChanged,
            &Settings::summaryFontSizeChanged,
            &Settings::paragraphFontSizeChanged,
            &Settings::chatFontSizeChanged,
            &Settings::uiLanguageChanged,
            &Settings::autoSegmentChanged,
            &Settings::sharePaperDataChanged,
            &Settings::grobidEnabledChanged,
            &Settings::autoCheckUpdatesChanged,
        };
        for (const Sig s : sigs)
            connect(m_settings, s, this, &UserPrefsSync::onLocalChange);
    }

    if (m_auth) {
        connect(m_auth, &AuthController::authenticatedChanged, this,
                &UserPrefsSync::onAuthChanged);
        connect(m_auth, &AuthController::userChanged, this,
                &UserPrefsSync::onUserChanged);
        // Constructed after an auto-login has already finished? Then no
        // signal is coming and we would sit idle until the first change.
        if (m_auth->authenticated())
            onAuthChanged();
    }
}

// ── session ────────────────────────────────────────────────────────────

void UserPrefsSync::onAuthChanged()
{
    if (!m_auth)
        return;
    if (m_auth->authenticated()) {
        m_identityGrace.start();
        startIfPossible();
        return;
    }
    // Signed out. Everything local stays exactly as it is -- the app has no
    // idea any of this happened -- and the persisted version/base survive so
    // signing back in does not re-push a payload the account already holds.
    m_identityGrace.stop();
    m_pushTimer.stop();
    m_pulled = false;
    m_syncedUserId.clear();
    m_conflictRetried = false;
}

void UserPrefsSync::onUserChanged()
{
    // CAS sign-in flips authenticated before /auth/me answers, so this is
    // where the user's identity usually arrives.
    if (m_auth && m_auth->authenticated())
        startIfPossible();
}

void UserPrefsSync::startIfPossible()
{
    if (!m_auth || !m_auth->authenticated())
        return;
    const QString user = m_auth->userId();
    // Wait for the identity when we can: a pull that does not know whose
    // account it is cannot tell "my own settings coming back" from "somebody
    // else's account on this machine". The grace timer above stops that wait
    // from becoming permanent if the backend never tells us.
    if (user.isEmpty() && m_identityGrace.isActive())
        return;
    if (m_pulled && user == m_syncedUserId)
        return;
    m_syncedUserId = user;
    m_identityGrace.stop();
    pullNow();
}

// ── local changes ──────────────────────────────────────────────────────

void UserPrefsSync::onLocalChange()
{
    if (m_applyingRemote || !m_settings)
        return;

    const QJsonObject now = m_settings->exportAccountSettings();
    if (now == m_lastSeen)
        return;                  // the signal was about something local-only
    m_lastSeen = now;

    // The moment of the change, kept across restarts. It is what tells a
    // first pull that this machine has moved on since it last agreed with
    // the account.
    m_lastLocalChangeAt = QDateTime::currentDateTimeUtc();
    // A fresh change earns a fresh conflict retry.
    m_conflictRetried = false;
    saveState();
    m_pushTimer.start();
}

void UserPrefsSync::flushPending()
{
    m_pushTimer.stop();
    maybePush();
}

void UserPrefsSync::maybePush()
{
    if (!m_api || !m_auth || !m_settings)
        return;
    // Offline or signed out is not an error and not a failure: the value is
    // already saved locally and the app is already using it. The next
    // successful pull re-runs the merge and pushes whatever is still ahead.
    if (!m_auth->authenticated())
        return;
    if (m_inFlight) {
        m_changedWhileInFlight = true;
        return;
    }
    // Never push before this session has seen what the account holds.
    // Pushing against a version from a previous run would at best cost a
    // 409 round trip and at worst mean we merged against a base the account
    // has since moved past. The pull ends in reconcile(), which pushes.
    if (!m_pulled) {
        pullNow();
        return;
    }

    const QJsonObject payload = withForeign(m_settings->exportAccountSettings());
    if (payload == m_lastPushed)
        return;                  // the account already holds exactly this
    push(payload, m_version);
}

// ── the two requests ───────────────────────────────────────────────────

void UserPrefsSync::pullNow()
{
    if (!m_api || !m_auth || !m_settings)
        return;
    if (!m_auth->authenticated() || m_inFlight)
        return;

    m_inFlight = true;
    setBusy(true);
    m_api->get(QString::fromLatin1(kPath),
               [this](bool ok, int status, const QJsonDocument &doc) {
                   m_inFlight = false;
                   if (!ok) {
                       setBusy(false);
                       setLastError(tr("Could not read the account settings "
                                       "(HTTP %1).")
                                        .arg(status));
                       return;
                   }
                   const QJsonObject o = doc.object();
                   setLastError(QString());
                   m_pulled = true;
                   touchSynced();
                   reconcile(o.value(QStringLiteral("data")).toObject(),
                             versionOf(o, m_version));
                   // reconcile() may have started a push of its own; only
                   // clear busy when it did not.
                   if (!m_inFlight)
                       setBusy(false);
               });
}

void UserPrefsSync::push(const QJsonObject &data, const QString &expectedVersion)
{
    if (!m_api || !m_auth || !m_auth->authenticated())
        return;
    if (m_inFlight) {
        m_changedWhileInFlight = true;
        return;
    }

    m_inFlight = true;
    setBusy(true);
    const QJsonObject body{
        {QStringLiteral("data"), data},
        {QStringLiteral("expectedVersion"), expectedVersion},
    };
    m_api->put(QString::fromLatin1(kPath), body,
               [this, data](bool ok, int status, const QJsonDocument &doc) {
                   m_inFlight = false;
                   const QJsonObject o = doc.object();

                   if (ok) {
                       rememberBase(data, versionOf(o, m_version));
                       setLastError(QString());
                       touchSynced();
                       m_conflictRetried = false;
                       if (m_changedWhileInFlight) {
                           m_changedWhileInFlight = false;
                           m_pushTimer.start();
                       }
                       setBusy(false);
                       return;
                   }

                   if (status == 409) {
                       onConflict(o);
                       if (!m_inFlight)
                           setBusy(false);
                       return;
                   }

                   setBusy(false);
                   setLastError(
                       tr("Could not save the account settings (HTTP %1).")
                           .arg(status));
               });
}

void UserPrefsSync::onConflict(const QJsonObject &body)
{
    if (!m_settings)
        return;

    const QString version = versionOf(body, m_version);
    const QJsonObject theirs = body.value(QStringLiteral("data")).toObject();
    m_version = version;
    m_foreignKeys = foreignKeysOf(theirs);
    saveState();

    if (m_conflictRetried) {
        // Two collisions in a row means another machine is writing at the
        // same time as this one. Stop: local is already correct for the app
        // in front of the reader, and the next change -- or the next launch's
        // pull -- runs the same merge again from a fresh version. A sync that
        // will not stop retrying is worse than one that is a minute stale.
        setLastError(tr("The account settings changed on another machine "
                        "while saving; kept the local ones for now."));
        return;
    }
    m_conflictRetried = true;

    const QJsonObject base = knownKeyCount(m_lastPushed) > 0
                                 ? m_lastPushed
                                 : Settings::defaultAccountSettings();
    const QJsonObject merged =
        mergeThreeWay(base, m_settings->exportAccountSettings(), theirs);
    applyLocally(merged);

    const QJsonObject after = m_settings->exportAccountSettings();
    if (sameKnownKeys(after, theirs)) {
        // The merge landed on exactly what the server already holds.
        rememberBase(withForeign(after), version);
        setLastError(QString());
        return;
    }
    push(withForeign(after), version);
}

// ── reconciliation ─────────────────────────────────────────────────────

void UserPrefsSync::reconcile(const QJsonObject &serverData,
                              const QString &version)
{
    if (!m_settings)
        return;

    // The rule, in full, because this is the part that can annoy a reader:
    //
    //  * The account has nothing yet (version "0") -> this machine seeds it.
    //  * A different user signed in here -> the account copy wins outright.
    //    What is on this disk is the previous user's configuration, and the
    //    question the merge asks ("did *this* user change this key?") has no
    //    answer. Their settings are safe on their own account.
    //  * Otherwise a three-way merge, key by key, against the payload we
    //    last agreed with the account on:
    //      - a key we have not touched since then     -> the account wins;
    //      - a key we have touched since then         -> this machine wins,
    //        and the merged result is pushed.
    //    This is the "changed more recently than lastPushedAt" rule made
    //    per-key rather than per-payload: a key that differs from the base
    //    is, by definition, one changed here after the last push.
    //  * When there is no such base -- this machine has never synced, which
    //    is every machine on the first run after an update -- the factory
    //    defaults stand in for it. A key still at its default is a key
    //    nobody configured here, so the account wins it; a key the reader
    //    changed on this machine differs from the default, so it wins and is
    //    pushed. That is what keeps a first pull from quietly replacing a
    //    configuration somebody typed here, without letting a brand-new
    //    install push its defaults over a working account.

    m_version = version;
    m_foreignKeys = foreignKeysOf(serverData);

    const QString currentUser = m_auth ? m_auth->userId() : QString();
    // Only a *known* identity that differs from the stored one counts as a
    // switch. An identity we do not know yet (the backend has not said, or
    // this is the first run since the feature shipped) must never take the
    // adopt-outright path below -- that is how a first pull would quietly
    // replace a configuration somebody typed on this machine.
    const bool differentUser = !currentUser.isEmpty() && !m_userId.isEmpty()
                               && currentUser != m_userId;
    if (!currentUser.isEmpty())
        m_userId = currentUser;
    if (differentUser) {
        // The stored base answers a question about somebody else's account.
        // Drop it on disk, not just in memory, so a failed request below
        // cannot leave it to mislead the next launch.
        m_lastPushed = QJsonObject{};
        saveState();
    }

    const QJsonObject local = m_settings->exportAccountSettings();

    if (knownKeyCount(serverData) == 0) {
        // Nobody has ever saved settings on this account, so there is nothing
        // to adopt and nothing to lose: seed it from this machine. That holds
        // even when a different user just signed in -- the configuration on
        // this disk is the one they are reading with right now, and an empty
        // account would otherwise leave their other machines with nothing.
        saveState();
        push(withForeign(local), m_version);
        return;
    }

    if (differentUser) {
        applyLocally(serverData);
        const QJsonObject adopted = m_settings->exportAccountSettings();
        if (sameKnownKeys(adopted, serverData)) {
            rememberBase(withForeign(adopted), m_version);
        } else {
            // Something in the account copy was out of range and got clamped
            // on the way in; tell the account what we actually stored rather
            // than leaving the two sides permanently disagreeing.
            push(withForeign(adopted), m_version);
        }
        return;
    }

    const QJsonObject base = knownKeyCount(m_lastPushed) > 0
                                 ? m_lastPushed
                                 : Settings::defaultAccountSettings();
    const QJsonObject merged = mergeThreeWay(base, local, serverData);
    applyLocally(merged);

    const QJsonObject after = m_settings->exportAccountSettings();
    if (sameKnownKeys(after, serverData)) {
        rememberBase(withForeign(after), m_version);
        return;
    }
    push(withForeign(after), m_version);
}

void UserPrefsSync::applyLocally(const QJsonObject &payload)
{
    if (!m_settings)
        return;
    // Settings does the clamping and raises the ordinary change signals, so
    // the panes on screen follow along without a restart. The guard keeps
    // those signals from being read back as a local edit and bouncing
    // straight into another push.
    m_applyingRemote = true;
    m_settings->importAccountSettings(payload);
    m_applyingRemote = false;
    m_lastSeen = m_settings->exportAccountSettings();
}

QJsonObject UserPrefsSync::mergeThreeWay(const QJsonObject &base,
                                         const QJsonObject &local,
                                         const QJsonObject &server) const
{
    QJsonObject out;
    for (const QString &key : Settings::accountSettingKeys()) {
        const QJsonValue mine = local.value(key);
        const QJsonValue theirs = server.value(key);
        const QJsonValue was = base.value(key);
        if (theirs.isUndefined()) {
            out.insert(key, mine);   // the account has no opinion on this key
        } else if (mine == was) {
            out.insert(key, theirs); // untouched here since we last agreed
        } else {
            out.insert(key, mine);   // changed here: this machine wins
        }
    }
    return out;
}

QJsonObject UserPrefsSync::withForeign(const QJsonObject &known) const
{
    // Keys a newer build of the app syncs and this one has never heard of are
    // carried straight back. Without this, running an older version once
    // would silently strip whatever the newer one had added.
    QJsonObject out = known;
    for (auto it = m_foreignKeys.begin(); it != m_foreignKeys.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

bool UserPrefsSync::sameKnownKeys(const QJsonObject &a, const QJsonObject &b)
{
    for (const QString &key : Settings::accountSettingKeys()) {
        if (a.value(key) != b.value(key))
            return false;
    }
    return true;
}

QJsonObject UserPrefsSync::foreignKeysOf(const QJsonObject &server)
{
    QJsonObject out;
    const QStringList &known = Settings::accountSettingKeys();
    for (auto it = server.begin(); it != server.end(); ++it) {
        if (!known.contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

int UserPrefsSync::knownKeyCount(const QJsonObject &o)
{
    int n = 0;
    for (const QString &key : Settings::accountSettingKeys()) {
        if (o.contains(key))
            ++n;
    }
    return n;
}

// ── persisted state ────────────────────────────────────────────────────

void UserPrefsSync::rememberBase(const QJsonObject &payload,
                                 const QString &version)
{
    m_lastPushed = payload;
    m_version = version;
    m_lastPushedAt = QDateTime::currentDateTimeUtc();
    if (m_auth && !m_auth->userId().isEmpty())
        m_userId = m_auth->userId();
    saveState();
}

void UserPrefsSync::loadState()
{
    m_userId = m_qs.value(QString::fromLatin1(kQsUserId)).toString();
    m_version = m_qs.value(QString::fromLatin1(kQsVersion),
                           QStringLiteral("0"))
                    .toString();
    m_lastPushed = objFromText(
        m_qs.value(QString::fromLatin1(kQsBase)).toString());
    m_foreignKeys = objFromText(
        m_qs.value(QString::fromLatin1(kQsForeign)).toString());
    m_lastPushedAt =
        dateFromText(m_qs.value(QString::fromLatin1(kQsPushedAt)).toString());
    m_lastLocalChangeAt = dateFromText(
        m_qs.value(QString::fromLatin1(kQsLocalChangeAt)).toString());
    m_lastSyncedAt =
        dateFromText(m_qs.value(QString::fromLatin1(kQsSyncedAt)).toString());
}

// lastPushedAt and lastLocalChangeAt are kept because they are the record of
// what happened here and when -- a status line and a support question both
// want them. They are deliberately not what the merge consults: the stored
// base payload answers the same question ("has this machine moved on since it
// last agreed with the account?") per key instead of per payload, which is
// strictly the better answer whenever only one setting was touched.
void UserPrefsSync::saveState()
{
    m_qs.setValue(QString::fromLatin1(kQsUserId), m_userId);
    m_qs.setValue(QString::fromLatin1(kQsVersion), m_version);
    m_qs.setValue(QString::fromLatin1(kQsBase), textOf(m_lastPushed));
    m_qs.setValue(QString::fromLatin1(kQsForeign), textOf(m_foreignKeys));
    m_qs.setValue(QString::fromLatin1(kQsPushedAt),
                  m_lastPushedAt.isValid()
                      ? m_lastPushedAt.toString(Qt::ISODateWithMs)
                      : QString());
    m_qs.setValue(QString::fromLatin1(kQsLocalChangeAt),
                  m_lastLocalChangeAt.isValid()
                      ? m_lastLocalChangeAt.toString(Qt::ISODateWithMs)
                      : QString());
    m_qs.setValue(QString::fromLatin1(kQsSyncedAt),
                  m_lastSyncedAt.isValid()
                      ? m_lastSyncedAt.toString(Qt::ISODateWithMs)
                      : QString());
    m_qs.sync();
}

// ── small properties ───────────────────────────────────────────────────

void UserPrefsSync::setBusy(bool v)
{
    if (v == m_busy)
        return;
    m_busy = v;
    emit busyChanged();
}

void UserPrefsSync::setLastError(const QString &e)
{
    if (e == m_lastError)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

void UserPrefsSync::touchSynced()
{
    m_lastSyncedAt = QDateTime::currentDateTimeUtc();
    m_qs.setValue(QString::fromLatin1(kQsSyncedAt),
                  m_lastSyncedAt.toString(Qt::ISODateWithMs));
    emit lastSyncedAtChanged();
}
