#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

#include "LibraryDb.h"

class ApiClient;
class ProjectController;
class SyncEngine;

// Syncs the actual PDF bytes (not just metadata). On add, the file is hashed
// (sha256), uploaded to content-addressed object storage (deduped server-side),
// and an "attachment" object links the item to the blob. On open, if the local
// file is gone (e.g. another machine) the blob is fetched to a local cache and
// opened.
//
// Bytes travel through the API host, not straight to object storage: the
// storage endpoint is internal, so the older presigned-URL route only worked
// on the office network. Transfers use this class's own NAM (ApiClient is
// JSON-only) with the same bearer token; every transfer is preceded by a
// small ApiClient call so an expired token is refreshed before the big one.
class FileSyncService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    FileSyncService(ApiClient *api, LibraryDb *db, ProjectController *projects,
                    SyncEngine *sync, QObject *parent = nullptr);

    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void uploadPaper(const QString &itemId, const QString &localPath);
    // The library's title for a file on disk, or empty when the library
    // doesn't know it. Papers opened from the library are served out of the
    // content-addressed blob cache, so their filename is a sha256 — which is
    // what the tab bar and the Interpret pane would otherwise show.
    Q_INVOKABLE QString titleForFile(const QString &path) const;

    Q_INVOKABLE void openItem(const QString &itemId, const QString &localPath);

    // Make sure the PDF bytes are on this machine, without opening anything.
    // Answers with the local path, downloading from the project's storage
    // first when this machine has never seen the file -- which is the normal
    // case for a batch that interprets papers nobody here has opened.
    using LocalReady = std::function<void(bool ok, const QString &path,
                                          const QString &error)>;
    void ensureLocal(const QString &itemId, const QString &localPath,
                     LocalReady done);
    // Walk the current project's attachments and reconcile them with
    // what storage actually holds: re-upload anything whose bytes are
    // missing but whose file is still on this machine, and retire the
    // records whose bytes exist nowhere (they only lead other machines
    // into failed downloads).
    Q_INVOKABLE void repairAttachments();

signals:
    // Carries a file:// URL, not a bare filesystem path: QML's
    // PdfDocument.source only accepts a real URL, and PaperController
    // treats a source whose isLocalFile() is false as remote (which
    // also skips GROBID). A bare path silently produced a blank
    // viewer with fallback-segmented paragraphs.
    void openReady(const QString &url);
    void statusChanged();
    void busyChanged();
    // One uploadPaper() call settled. The batch importer paces itself on
    // this so it never has more than one transfer in flight.
    void paperUploaded(const QString &itemId, bool ok);

private:
    static QString toLocalPath(const QString &pathOrUrl);
    static QString sha256File(const QString &path);
    QString blobCachePath(const QString &sha256) const;
    void createAttachment(const QString &itemId, const QString &paperId,
                          const QString &sha256, const QString &key,
                          qint64 byteSize);
    bool findAttachment(const QString &itemId, QString &key, QString &sha256) const;
    // Authenticated request against the API host, for the byte transfers
    // ApiClient's JSON interface can't carry.
    QNetworkRequest blobRequest(const QString &path) const;
    // (succeeded, wasAlreadyInStorage)
    using UploadDone = std::function<void(bool, bool)>;
    void uploadOne(const QString &itemId, const QString &path, UploadDone done);
    void putBlob(const QString &sha256, const QString &localPath,
                 std::function<void(bool)> done);
    void downloadBlob(const QString &key, const QString &sha256,
                      LocalReady done);
    void repairStep();
    void setStatus(const QString &s);
    void setBusy(bool v);

    ApiClient *m_api;
    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    QNetworkAccessManager m_nam;
    bool m_busy = false;
    QString m_status;

    // repairAttachments() worklist — processed one at a time so a big
    // library can't open dozens of concurrent transfers.
    struct RepairTask {
        QString attachmentId;   // sync object id of the attachment record
        QString itemId;
        QString sha256;
        QJsonObject data;       // kept so a retired record keeps its fields
    };
    QList<RepairTask> m_repairQueue;
    int m_repairTotal = 0, m_repairOk = 0, m_repairFixed = 0,
        m_repairRetired = 0, m_repairFailed = 0;
};
