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

    QJsonObject body{{QStringLiteral("sha256"), sha},
                     {QStringLiteral("contentType"), QStringLiteral("application/pdf")},
                     {QStringLiteral("byteSize"), static_cast<double>(size)}};
    // We need the item's paperId for the attachment link.
    SyncObjectRow item;
    m_db->getObject(m_projects->currentId(), itemId, item);
    const QString paperId = item.data.value(QStringLiteral("paperId")).toString();

    m_api->post(
        QStringLiteral("/projects/") + m_projects->currentId()
            + QStringLiteral("/attachments/upload-url"),
        body,
        [this, itemId, paperId, sha, size, path](bool ok, int status,
                                                 const QJsonDocument &doc) {
            if (!ok) {
                setBusy(false);
                setStatus(tr("Upload-url failed (HTTP %1)").arg(status));
                return;
            }
            const QJsonObject o = doc.object();
            const QString key = o.value(QStringLiteral("storageKey")).toString().isEmpty()
                                    ? o.value(QStringLiteral("key")).toString()
                                    : o.value(QStringLiteral("storageKey")).toString();
            createAttachment(itemId, paperId, sha, key, size);
            if (o.value(QStringLiteral("exists")).toBool()) {
                setBusy(false);
                setStatus(tr("PDF already in storage (deduped)."));
                return;
            }
            putBlob(o.value(QStringLiteral("uploadUrl")).toString(), path);
        });
}

void FileSyncService::putBlob(const QString &uploadUrl, const QString &localPath)
{
    if (uploadUrl.isEmpty()) {
        setBusy(false);
        return;
    }
    auto *file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        setBusy(false);
        setStatus(tr("Could not open the PDF."));
        return;
    }
    QNetworkRequest req{QUrl(uploadUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/pdf"));
    QNetworkReply *reply = m_nam.put(req, file);
    file->setParent(reply);
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
        emit openReady(path);
        return;
    }
    QString key, sha;
    if (!findAttachment(itemId, key, sha)) {
        setStatus(tr("This paper's PDF isn't available yet."));
        return;
    }
    const QString cache = blobCachePath(sha);
    if (QFileInfo::exists(cache)) {
        emit openReady(cache);
        return;
    }
    downloadBlob(key, sha);
}

void FileSyncService::downloadBlob(const QString &key, const QString &sha256)
{
    setBusy(true);
    setStatus(tr("Downloading PDF…"));
    m_api->get(
        QStringLiteral("/projects/") + m_projects->currentId()
            + QStringLiteral("/attachments/download-url?key=") + key,
        [this, sha256](bool ok, int status, const QJsonDocument &doc) {
            if (!ok) {
                setBusy(false);
                setStatus(tr("Download-url failed (HTTP %1)").arg(status));
                return;
            }
            const QString url =
                doc.object().value(QStringLiteral("downloadUrl")).toString();
            QNetworkReply *reply = m_nam.get(QNetworkRequest{QUrl(url)});
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
                    emit openReady(cache);
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
