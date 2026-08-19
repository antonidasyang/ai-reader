#include "UpdateChecker.h"

#include "Settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace {

constexpr auto kDefaultManifestUrl =
    "https://aireader.d2ssoft.com/update/manifest";

} // namespace

UpdateChecker::UpdateChecker(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_nam(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral(AIREADER_VERSION);
}

bool UpdateChecker::updateAvailable() const
{
    if (m_latestVersion.isEmpty()) return false;
    return compareVersions(m_latestVersion, currentVersion()) > 0;
}

QString UpdateChecker::effectiveManifestUrl() const
{
    if (m_settings) {
        const QString s = m_settings->updateManifestUrl().trimmed();
        if (!s.isEmpty()) return s;
    }
    return QString::fromLatin1(kDefaultManifestUrl);
}

QString UpdateChecker::platformKey()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows-x64");
#elif defined(Q_OS_MAC)
#  if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("macos-arm64");
#  else
    return QStringLiteral("macos-x64");
#  endif
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux-x64");
#else
    return QStringLiteral("unknown");
#endif
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    // Component-wise integer compare. Anything non-numeric in a
    // component is treated as 0 — keeps "0.1.0-rc1" comparable
    // (it'll equal "0.1.0", which is the safe direction: we won't
    // pester the user with a phantom upgrade to a release-candidate
    // tag).
    const QStringList aa = a.split(QChar('.'));
    const QStringList bb = b.split(QChar('.'));
    const int n = qMax(aa.size(), bb.size());
    for (int i = 0; i < n; ++i) {
        bool okA = false, okB = false;
        const int va = i < aa.size() ? aa.at(i).toInt(&okA) : 0;
        const int vb = i < bb.size() ? bb.at(i).toInt(&okB) : 0;
        const int aVal = okA ? va : 0;
        const int bVal = okB ? vb : 0;
        if (aVal < bVal) return -1;
        if (aVal > bVal) return  1;
    }
    return 0;
}

void UpdateChecker::checkNow()
{
    if (m_reply) return;  // already in flight
    const QString url = effectiveManifestUrl();
    if (url.isEmpty()) {
        m_lastError = tr("No manifest URL configured.");
        emit checkFinished();
        return;
    }

    QNetworkRequest req{ QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("AiReader/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_nam->get(req);
    emit checkingChanged();
    connect(m_reply, &QNetworkReply::finished,
            this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::onReplyFinished()
{
    if (!m_reply) return;
    QNetworkReply *reply = m_reply;
    m_reply.clear();

    m_lastError.clear();
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        reply->deleteLater();
        emit checkingChanged();
        emit checkFinished();
        return;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = tr("Manifest is not valid JSON: %1").arg(pe.errorString());
        emit checkingChanged();
        emit checkFinished();
        return;
    }

    const QJsonObject root = doc.object();
    m_latestVersion = root.value(QStringLiteral("latestVersion")).toString();
    m_releaseNotes  = root.value(QStringLiteral("releaseNotes")).toString();
    m_releaseDate   = root.value(QStringLiteral("releaseDate")).toString();

    // Pick the platform-specific download URL. When the manifest
    // doesn't list our platform we still surface latestVersion so
    // the user sees there's a new release upstream — they just
    // can't auto-jump to a download.
    const QJsonObject platforms = root.value(QStringLiteral("platforms")).toObject();
    const QJsonObject mine = platforms.value(platformKey()).toObject();
    m_downloadUrl = mine.value(QStringLiteral("downloadUrl")).toString();

    if (m_latestVersion.isEmpty())
        m_lastError = tr("Manifest is missing latestVersion.");

    emit checkingChanged();
    emit checkFinished();
}

void UpdateChecker::dismiss()
{
    if (m_dismissed) return;
    m_dismissed = true;
    emit dismissedChanged();
}

void UpdateChecker::openDownload()
{
    if (m_downloadUrl.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(m_downloadUrl));
}

void UpdateChecker::downloadAndInstall()
{
    if (m_dlReply || m_installing || m_downloadUrl.isEmpty())
        return;
#ifndef Q_OS_WIN
    // Only the Windows pipeline ships an installer we can run
    // silently; elsewhere hand off to the browser.
    openDownload();
    return;
#else
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = dir + QStringLiteral("/AiReader-Setup-")
                         + m_latestVersion + QStringLiteral(".exe");
    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = tr("Cannot write the update to %1").arg(path);
        file->deleteLater();
        emit checkFinished();
        return;
    }
    m_dlFile = file;
    m_dlProgress = 0;

    QNetworkRequest req{QUrl(m_downloadUrl)};
    req.setTransferTimeout(10 * 60 * 1000);   // large file, slow links
    m_dlReply = m_nam->get(req);
    connect(m_dlReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_dlFile && m_dlReply)
            m_dlFile->write(m_dlReply->readAll());
    });
    connect(m_dlReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_dlProgress = total > 0 ? double(received) / double(total)
                                         : 0;
                emit downloadStateChanged();
            });
    connect(m_dlReply, &QNetworkReply::finished,
            this, &UpdateChecker::onDownloadFinished);
    emit downloadStateChanged();
#endif
}

void UpdateChecker::onDownloadFinished()
{
    QNetworkReply *reply = m_dlReply;
    m_dlReply.clear();
    if (!reply || !m_dlFile) {
        emit downloadStateChanged();
        return;
    }
    m_dlFile->write(reply->readAll());
    m_dlFile->close();
    const QString path = m_dlFile->fileName();
    const bool ok = reply->error() == QNetworkReply::NoError
                    && m_dlFile->size() > 0;
    const QString netErr = reply->errorString();
    reply->deleteLater();
    m_dlFile->deleteLater();
    m_dlFile = nullptr;

    if (!ok) {
        QFile::remove(path);
        m_lastError = tr("Update download failed: %1").arg(netErr);
        emit checkFinished();
        emit downloadStateChanged();
        return;
    }

    // Hand over to the installer, then EXIT IMMEDIATELY — the
    // official Inno self-update pattern ("After starting Setup.exe,
    // exit your application as soon as possible"). Keeping the app
    // alive made the installer force-close us mid-replace via the
    // Restart Manager, and the silent [Run] relaunch entry could then
    // fail invisibly (per-entry exceptions are swallowed under
    // /SUPPRESSMSGBOXES). /FORCECLOSEAPPLICATIONS stays as a safety
    // net; /LOG makes the next failure diagnosable from AppData.
    m_installing = true;
    emit downloadStateChanged();
    const QString installLog =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/update-install.log");
    QProcess::startDetached(path,
                            {QStringLiteral("/VERYSILENT"),
                             QStringLiteral("/SUPPRESSMSGBOXES"),
                             QStringLiteral("/NORESTART"),
                             QStringLiteral("/FORCECLOSEAPPLICATIONS"),
                             QStringLiteral("/LOG=") + installLog});
    QTimer::singleShot(400, qApp, &QCoreApplication::quit);
}
