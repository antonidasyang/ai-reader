#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "LibraryDb.h"

class ApiClient;
class ProjectController;
class SyncEngine;

// Syncs the actual PDF bytes (not just metadata). On add, the file is hashed
// (sha256), uploaded to content-addressed object storage via a presigned URL
// (deduped server-side), and an "attachment" object links the item to the blob.
// On open, if the local file is gone (e.g. another machine) the blob is fetched
// to a local cache and opened. Direct S3 transfers use this class's own NAM;
// presign requests go through ApiClient.
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
    Q_INVOKABLE void openItem(const QString &itemId, const QString &localPath);

signals:
    void openReady(const QString &path);
    void statusChanged();
    void busyChanged();

private:
    static QString toLocalPath(const QString &pathOrUrl);
    static QString sha256File(const QString &path);
    QString blobCachePath(const QString &sha256) const;
    void createAttachment(const QString &itemId, const QString &paperId,
                          const QString &sha256, const QString &key,
                          qint64 byteSize);
    bool findAttachment(const QString &itemId, QString &key, QString &sha256) const;
    void putBlob(const QString &uploadUrl, const QString &localPath);
    void downloadBlob(const QString &key, const QString &sha256);
    void setStatus(const QString &s);
    void setBusy(bool v);

    ApiClient *m_api;
    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    QNetworkAccessManager m_nam;
    bool m_busy = false;
    QString m_status;
};
