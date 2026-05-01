#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// Pulls a tiny manifest.json off the network and surfaces a single
// "is there a newer version?" boolean to QML, plus the download URL
// the user can click. The actual update is handled out-of-process by
// the OS-native installer (the manifest's downloadUrl points at the
// signed Inno Setup .exe) — we deliberately don't run the installer
// from the running app since that would race with the in-place
// upgrade and surprise the user.
//
// Settings owns the auto-check flag and the manifest URL; this class
// just consumes them. Auto-check fires once per process at startup
// when enabled; a "Check now" button on the Settings dialog calls
// checkNow() any time.
class UpdateChecker : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    checking         READ checking         NOTIFY checkingChanged)
    Q_PROPERTY(QString currentVersion   READ currentVersion   CONSTANT)
    Q_PROPERTY(QString latestVersion    READ latestVersion    NOTIFY checkFinished)
    Q_PROPERTY(QString downloadUrl      READ downloadUrl      NOTIFY checkFinished)
    Q_PROPERTY(QString releaseNotes     READ releaseNotes     NOTIFY checkFinished)
    Q_PROPERTY(QString releaseDate      READ releaseDate      NOTIFY checkFinished)
    Q_PROPERTY(bool    updateAvailable  READ updateAvailable  NOTIFY checkFinished)
    Q_PROPERTY(bool    dismissed        READ dismissed        NOTIFY dismissedChanged)
    Q_PROPERTY(QString lastError        READ lastError        NOTIFY checkFinished)

public:
    UpdateChecker(Settings *settings, QObject *parent = nullptr);
    ~UpdateChecker() override;

    bool    checking()        const { return m_reply != nullptr; }
    QString currentVersion()  const;
    QString latestVersion()   const { return m_latestVersion; }
    QString downloadUrl()     const { return m_downloadUrl; }
    QString releaseNotes()    const { return m_releaseNotes; }
    QString releaseDate()     const { return m_releaseDate; }
    bool    updateAvailable() const;
    bool    dismissed()       const { return m_dismissed; }
    QString lastError()       const { return m_lastError; }

public slots:
    // Triggers a fetch even if auto-check is off. No-op while a
    // request is already in flight.
    void checkNow();
    // Hides the "update available" banner for the rest of this
    // process. Re-evaluated on the next checkNow() / restart.
    void dismiss();
    // Opens downloadUrl in the user's default browser. Deliberately
    // does not download or run the installer in-process — the user
    // controls when to upgrade.
    void openDownload();

signals:
    void checkingChanged();
    void checkFinished();
    void dismissedChanged();

private:
    void onReplyFinished();
    static int compareVersions(const QString &a, const QString &b);
    static QString platformKey();
    QString effectiveManifestUrl() const;

    QPointer<Settings>             m_settings;
    QNetworkAccessManager         *m_nam = nullptr;
    QPointer<QNetworkReply>        m_reply;

    QString m_latestVersion;
    QString m_downloadUrl;
    QString m_releaseNotes;
    QString m_releaseDate;
    QString m_lastError;
    bool    m_dismissed = false;
};
