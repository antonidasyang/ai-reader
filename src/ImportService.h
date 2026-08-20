#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

class FileSyncService;
class LibraryModel;
class MetadataService;
class ProjectController;

// Bulk "add these PDFs to the project library" — the folder pane's
// answer to adding papers one at a time, which meant opening each one
// (and waiting out its segmentation) just to press + Add.
//
// Files are processed one after another: hash the file for its paperId,
// skip it when the project already has that paper, create the library
// item, kick off the DOI/arXiv lookup from the PDF's own first page,
// and upload the bytes. The next file starts when the previous upload
// settles, so a folder of fifty papers never has fifty transfers in
// flight. Nothing here opens the paper in the reader.
class ImportService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    ImportService(LibraryModel *library, FileSyncService *files,
                  MetadataService *metadata, ProjectController *projects,
                  QObject *parent = nullptr);

    bool busy() const { return m_running; }
    int total() const { return m_total; }
    int done() const { return m_done; }
    QString status() const { return m_status; }

    // `paths` may be filesystem paths or file:// URLs; non-PDFs and
    // unreadable entries are counted as failures, not silently dropped.
    Q_INVOKABLE void importFiles(const QStringList &paths);
    // Stop after the transfer already in flight.
    Q_INVOKABLE void cancel();

signals:
    void progressChanged();
    void statusChanged();
    // added: new library items, skipped: already in the project,
    // failed: unreadable file or a transfer that didn't complete.
    void finished(int added, int skipped, int failed);

private:
    void step();
    void stepLater();
    void finish();
    void setStatus(const QString &s);
    // First page's text, for the DOI / arXiv id. Opens its own document
    // and closes it again — the reader's paper is never touched.
    static QString headTextOf(const QString &localPath, int maxChars = 6000);

    LibraryModel *m_library;
    FileSyncService *m_files;
    MetadataService *m_metadata;
    ProjectController *m_projects;

    QQueue<QString> m_queue;
    // Item whose upload we're waiting on; empty between files.
    QString m_pendingItemId;
    bool m_running = false;
    bool m_cancelled = false;
    int m_total = 0;
    int m_done = 0;
    int m_added = 0;
    int m_skipped = 0;
    int m_failed = 0;
    QString m_status;
};
