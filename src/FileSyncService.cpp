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
    setBusy(true);
    setStatus(tr("Uploading PDF…"));
    uploadOne(itemId, toLocalPath(localPath), [this, itemId](bool ok, bool deduped) {
        setBusy(false);
        if (ok) {
            setStatus(deduped ? tr("PDF already in storage (deduped).")
                              : tr("PDF uploaded."));
        }
        // else: uploadOne already set a specific message.
        emit paperUploaded(itemId, ok);
    });
}

void FileSyncService::uploadOne(const QString &itemId, const QString &path,
                                UploadDone done)
{
    if (m_projects->currentId().isEmpty() || path.isEmpty()) {
        done(false, false);
        return;
    }
    const QString sha = sha256File(path);
    if (sha.isEmpty()) {
        setStatus(tr("Could not read the PDF to upload."));
        done(false, false);
        return;
    }
    const qint64 size = QFileInfo(path).size();

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
        [this, itemId, paperId, sha, size, path, done](
            bool ok, int status, const QJsonDocument &doc) {
            if (!ok) {
                setStatus(tr("Upload check failed (HTTP %1)").arg(status));
                done(false, false);
                return;
            }
            const QJsonObject o = doc.object();
            const QString key = o.value(QStringLiteral("key")).toString();
            if (o.value(QStringLiteral("exists")).toBool()) {
                createAttachment(itemId, paperId, sha, key, size);
                done(true, true);
                return;
            }
            // The attachment record is written only once the bytes are
            // actually in storage. Recording it up front (as this did)
            // left a record claiming a blob that a failed upload never
            // produced — and other machines then tried to download it.
            putBlob(sha, path,
                    [this, itemId, paperId, sha, key, size, done](bool sent) {
                        if (sent)
                            createAttachment(itemId, paperId, sha, key, size);
                        done(sent, false);
                    });
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

void FileSyncService::putBlob(const QString &sha256, const QString &localPath,
                              std::function<void(bool)> done)
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
            setStatus(tr("Could not open the PDF."));
            done(false);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        const int s =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        const bool ok = s >= 200 && s < 300;
        if (!ok)
            setStatus(tr("PDF upload failed (HTTP %1)").arg(s));
        done(ok);
    });
}

void FileSyncService::repairAttachments()
{
    if (!m_repairQueue.isEmpty())
        return;                       // already running
    const QString projectId = m_projects->currentId();
    if (projectId.isEmpty()) {
        setStatus(tr("Open a project first."));
        return;
    }
    const QList<SyncObjectRow> rows =
        m_db->objectsByType(projectId, QStringLiteral("attachment"));
    for (const SyncObjectRow &r : rows) {
        const QString sha = r.data.value(QStringLiteral("sha256")).toString();
        if (sha.isEmpty())
            continue;
        m_repairQueue.append({r.id,
                              r.data.value(QStringLiteral("itemId")).toString(),
                              sha, r.data});
    }
    m_repairTotal = int(m_repairQueue.size());
    m_repairOk = m_repairFixed = m_repairRetired = m_repairFailed = 0;
    if (m_repairQueue.isEmpty()) {
        setStatus(tr("No PDFs to check in this project."));
        return;
    }
    setBusy(true);
    repairStep();
}

void FileSyncService::repairStep()
{
    if (m_repairQueue.isEmpty()) {
        setBusy(false);
        setStatus(tr("Checked %1 PDF(s): %2 already in storage, %3 re-uploaded, "
                     "%4 unavailable, %5 failed.")
                      .arg(m_repairTotal)
                      .arg(m_repairOk)
                      .arg(m_repairFixed)
                      .arg(m_repairRetired)
                      .arg(m_repairFailed));
        m_repairTotal = 0;
        return;
    }
    const RepairTask task = m_repairQueue.takeFirst();
    setStatus(tr("Checking PDFs… (%1 left)").arg(m_repairQueue.size() + 1));

    m_api->get(
        QStringLiteral("/projects/") + m_projects->currentId()
            + QStringLiteral("/attachments/blob-status?sha256=") + task.sha256,
        [this, task](bool ok, int, const QJsonDocument &doc) {
            if (!ok) {
                ++m_repairFailed;
                repairStep();
                return;
            }
            if (doc.object().value(QStringLiteral("exists")).toBool()) {
                ++m_repairOk;
                repairStep();
                return;
            }
            // Bytes are missing. If the original file is still on this
            // machine we can put them back; uploadOne re-hashes it, so
            // a file that changed since simply lands under its new key.
            SyncObjectRow item;
            m_db->getObject(m_projects->currentId(), task.itemId, item);
            const QString path = toLocalPath(
                item.data.value(QStringLiteral("localPath")).toString());
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                uploadOne(task.itemId, path, [this](bool sent, bool) {
                    sent ? ++m_repairFixed : ++m_repairFailed;
                    repairStep();
                });
                return;
            }
            // Nothing to upload from and nothing in storage: retire the
            // record so other machines stop trying to download it. The
            // library entry itself is untouched — only the claim that a
            // PDF is available goes away.
            m_sync->putObject(QStringLiteral("attachment"), task.attachmentId,
                              task.data, /*deleted=*/true);
            ++m_repairRetired;
            repairStep();
        });
}

QString FileSyncService::titleForFile(const QString &path) const
{
    if (path.isEmpty())
        return {};
    const QString projectId = m_projects->currentId();
    if (projectId.isEmpty())
        return {};
    const QFileInfo fi(path);

    // A blob-cache file is named after the attachment's sha256, which is the
    // link back to the item that owns it.
    QString itemId;
    if (fi.absolutePath() == QFileInfo(blobCachePath(QStringLiteral("x")))
                                 .absolutePath()) {
        const QString sha = fi.completeBaseName();
        const QList<SyncObjectRow> rows =
            m_db->objectsByType(projectId, QStringLiteral("attachment"));
        for (const SyncObjectRow &r : rows) {
            if (r.data.value(QStringLiteral("sha256")).toString() == sha) {
                itemId = r.data.value(QStringLiteral("itemId")).toString();
                break;
            }
        }
    }
    if (itemId.isEmpty()) {
        // Otherwise it may be a paper added from disk, whose item records the
        // path it was added from.
        const QString canonical = fi.canonicalFilePath();
        const QList<SyncObjectRow> rows =
            m_db->objectsByType(projectId, QStringLiteral("item"));
        for (const SyncObjectRow &r : rows) {
            const QString p =
                r.data.value(QStringLiteral("localPath")).toString();
            if (p.isEmpty())
                continue;
            const QString c = QFileInfo(p).canonicalFilePath();
            if ((!c.isEmpty() && c == canonical) || p == path)
                return r.data.value(QStringLiteral("title")).toString();
        }
        return {};
    }

    SyncObjectRow item;
    if (!m_db->getObject(projectId, itemId, item) || item.deleted)
        return {};
    return item.data.value(QStringLiteral("title")).toString();
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
