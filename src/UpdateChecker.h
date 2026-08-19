#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// Pulls a tiny manifest off the network and surfaces a single
// "is there a newer version?" boolean to QML, plus one-click
// updating: downloadAndInstall() streams the installer to a temp
// file and (on Windows) launches it silently — Inno Setup's
// CloseApplications/RestartApplications then swaps the files and
// relaunches the app via the Restart Manager, so the whole upgrade
// is a single click. Elsewhere it falls back to the browser.
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
    Q_PROPERTY(bool    downloading      READ downloading      NOTIFY downloadStateChanged)
    Q_PROPERTY(double  downloadProgress READ downloadProgress NOTIFY downloadStateChanged)
    Q_PROPERTY(bool    installing       READ installing       NOTIFY downloadStateChanged)

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
    bool    downloading()       const { return m_dlReply != nullptr; }
    double  downloadProgress()  const { return m_dlProgress; }
    bool    installing()        const { return m_installing; }

public slots:
    // Triggers a fetch even if auto-check is off. No-op while a
    // request is already in flight.
    void checkNow();
    // Hides the "update available" banner for the rest of this
    // process. Re-evaluated on the next checkNow() / restart.
    void dismiss();
    // Opens downloadUrl in the user's default browser (fallback path
    // and non-Windows platforms).
    void openDownload();
    // One-click update: download the installer with progress, then
    // (Windows) run it silently — the installer closes and relaunches
    // the app itself. No-op while a download is already running.
    void downloadAndInstall();

signals:
    void checkingChanged();
    void checkFinished();
    void dismissedChanged();
    void downloadStateChanged();

private:
    void onReplyFinished();
    void onDownloadFinished();
    static int compareVersions(const QString &a, const QString &b);
    static QString platformKey();
    QString effectiveManifestUrl() const;

    QPointer<Settings>             m_settings;
    QNetworkAccessManager         *m_nam = nullptr;
    QPointer<QNetworkReply>        m_reply;
    QPointer<QNetworkReply>        m_dlReply;
    class QFile                   *m_dlFile = nullptr;
    double  m_dlProgress = 0;
    bool    m_installing = false;

    QString m_latestVersion;
    QString m_downloadUrl;
    QString m_releaseNotes;
    QString m_releaseDate;
    QString m_lastError;
    bool    m_dismissed = false;
};
