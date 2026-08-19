#include "FileSyncService.h"
#include "ApiClient.h"
#include "ProjectController.h"
#include "SyncEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {
const QUuid kAttNs =
    QUuid::fromString(QStringLiteral("{4a1f2e90-7b3c-4d6a-9f21-a1b2c3d40002}"));
}

FileSyncService::FileSyncService(ApiClient *api, LibraryDb *db,
                                 ProjectController *projects, SyncEngine *sync,
                                 QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_db(db)
    , m_projects(projects)
    , m_sync(sync)
{
}

QString FileSyncService::toLocalPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QString FileSyncService::sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return {};
    return QString::fromLatin1(h.result().toHex());
}

QString FileSyncService::blobCachePath(const QString &sha256) const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library/blobs");
    QDir().mkpath(dir);
    return dir + QChar('/') + sha256 + QStringLiteral(".pdf");
}

void FileSyncService::createAttachment(const QString &itemId,
                                       const QString &paperId,
                                       const QString &sha256, const QString &key,
                                       qint64 byteSize)
{
    const QString projectId = m_projects->currentId();
    const QString name = projectId + QStringLiteral("|att|") + itemId;
    const QString id =
        QUuid::createUuidV5(kAttNs, name.toUtf8()).toString(QUuid::WithoutBraces);
    QJsonObject data{{QStringLiteral("itemId"), itemId},
                     {QStringLiteral("paperId"), paperId},
                     {QStringLiteral("sha256"), sha256},
                     {QStringLiteral("storageKey"), key},
                     {QStringLiteral("contentType"), QStringLiteral("application/pdf")},
                     {QStringLiteral("byteSize"), static_cast<double>(byteSize)}};
    m_sync->putObject(QStringLiteral("attachment"), id, data);
}

bool FileSyncService::findAttachment(const QString &itemId, QString &key,
                                     QString &sha256) const
{
    const QList<SyncObjectRow> rows = m_db->objectsByType(
        m_projects->currentId(), QStringLiteral("attachment"));
    for (const SyncObjectRow &r : rows) {
        if (r.data.value(QStringLiteral("itemId")).toString() == itemId) {
            key = r.data.value(QStringLiteral("storageKey")).toString();
            sha256 = r.data.value(QStringLiteral("sha256")).toString();
            return !key.isEmpty();
        }
    }
    return false;
}

void FileSyncService::uploadPaper(const QString &itemId,
                                  const QString &localPath)
{
    const QString path = toLocalPath(localPath);
    if (m_projects->currentId().isEmpty() || path.isEmpty())
        return;
    const QString sha = sha256File(path);
    if (sha.isEmpty()) {
        setStatus(tr("Could not read the PDF to upload."));
        return;
    }
    const qint64 size = QFileInfo(path).size();
    setBusy(true);
    setStatus(tr("Uploading PDF…"));

    // We need the item's paperId for the attachment link.
    SyncObjectRow item;
    m_db->getObject(m_projects->currentId(), itemId, item);
    const QString paperId = item.data.value(QStringLiteral("paperId")).toString();

    // Ask first: identical content is stored once for everyone, so a
    // paper someone else already added needs no upload at all. Going
    // through ApiClient also refreshes an expired token before the
    // transfer below, which uses the bearer token directly.
    m_api->get(
        QStringLiteral("/projects/") + m_projects->currentId()
            + QStringLiteral("/attachments/blob-status?sha256=") + sha,
        [this, itemId, paperId, sha, size, path](bool ok, int status,
                                                 const QJsonDocument &doc) {
            if (!ok) {
                setBusy(false);
                setStatus(tr("Upload check failed (HTTP %1)").arg(status));
                return;
            }
            const QJsonObject o = doc.object();
            createAttachment(itemId, paperId, sha,
                             o.value(QStringLiteral("key")).toString(), size);
            if (o.value(QStringLiteral("exists")).toBool()) {
                setBusy(false);
                setStatus(tr("PDF already in storage (deduped)."));
                return;
            }
            putBlob(sha, path);
        });
}

QNetworkRequest FileSyncService::blobRequest(const QString &path) const
{
    QNetworkRequest req{QUrl(m_api->baseUrl() + path)};
    req.setRawHeader("Authorization",
                     "Bearer " + m_api->accessToken().toUtf8());
    // PDFs are big and the link may be slow; the default 30 s transfer
    // timeout would abort perfectly healthy uploads.
    req.setTransferTimeout(600000);
    return req;
}

void FileSyncService::putBlob(const QString &sha256, const QString &localPath)
{
    // Read the PDF fully into memory, then upload from the bytes — rather than
    // handing QNetworkAccessManager a QFile that stays open for the whole
    // upload. Holding a second handle on the very file the viewer's
    // QPdfDocument is displaying can make the open PDF blank out (file
    // contention, seen on Windows especially when the upload to MinIO is slow).
    // Open → read → close here, so no foreign handle lingers on the document.
    QByteArray bytes;
    {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) {
            setBusy(false);
            setStatus(tr("Could not open the PDF."));
            return;
        }
        bytes = file.readAll();
    }
    QNetworkRequest req = blobRequest(
        QStringLiteral("/projects/") + m_projects->currentId()
        + QStringLiteral("/attachments/blob?sha256=") + sha256);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/pdf"));
    QNetworkReply *reply = m_nam.put(req, bytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int s =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        setBusy(false);
        setStatus(s >= 200 && s < 300 ? tr("PDF uploaded.")
                                      : tr("PDF upload failed (HTTP %1)").arg(s));
    });
}

void FileSyncService::openItem(const QString &itemId, const QString &localPath)
{
    const QString path = toLocalPath(localPath);
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        emit openReady(QUrl::fromLocalFile(path).toString());
        return;
    }
    QString key, sha;
    if (!findAttachment(itemId, key, sha)) {
        setStatus(tr("This paper's PDF isn't available yet."));
        return;
    }
    const QString cache = blobCachePath(sha);
    if (QFileInfo::exists(cache)) {
        emit openReady(QUrl::fromLocalFile(cache).toString());
        return;
    }
    downloadBlob(key, sha);
}

void FileSyncService::downloadBlob(const QString &key, const QString &sha256)
{
    setBusy(true);
    setStatus(tr("Downloading PDF…"));
    // Cheap authenticated probe first: confirms the blob is really
    // there (a clearer error than a failed transfer) and gives
    // ApiClient the chance to refresh an expired token before the
    // bearer-token transfer below.
    m_api->get(
        QStringLiteral("/projects/") + m_projects->currentId()
            + QStringLiteral("/attachments/blob-status?sha256=") + sha256,
        [this, key, sha256](bool ok, int status, const QJsonDocument &doc) {
            if (!ok) {
                setBusy(false);
                setStatus(tr("Download failed (HTTP %1)").arg(status));
                return;
            }
            if (!doc.object().value(QStringLiteral("exists")).toBool()) {
                setBusy(false);
                setStatus(tr("This paper's PDF isn't in storage yet."));
                return;
            }
            QNetworkReply *reply = m_nam.get(blobRequest(
                QStringLiteral("/projects/") + m_projects->currentId()
                + QStringLiteral("/attachments/blob?key=") + key));
            connect(reply, &QNetworkReply::finished, this, [this, reply, sha256] {
                const QByteArray bytes = reply->readAll();
                const int s = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                reply->deleteLater();
                setBusy(false);
                if (s < 200 || s >= 300 || bytes.isEmpty()) {
                    setStatus(tr("PDF download failed (HTTP %1)").arg(s));
                    return;
                }
                const QString cache = blobCachePath(sha256);
                QFile f(cache);
                if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size()) {
                    f.close();
                    setStatus(tr("PDF downloaded."));
                    emit openReady(QUrl::fromLocalFile(cache).toString());
                } else {
                    setStatus(tr("Could not save the downloaded PDF."));
                }
            });
        });
}

void FileSyncService::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void FileSyncService::setBusy(bool v)
{
    if (v == m_busy)
        return;
    m_busy = v;
    emit busyChanged();
}
