#pragma once

#include "Block.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

class FileSyncService;
class LibraryDb;
class LibraryModel;
class ProjectController;

// The paragraphs of a paper nobody has opened.
//
// Interpreting a library in bulk (§7) cannot go through PaperController: that
// one owns the paper on screen, and yanking it around to service a batch would
// be visible and wrong. This walks the same three sources the reader's own
// paper does, cheapest first:
//
//   1. this machine's paragraph cache for that paperId;
//   2. a segmentation a collaborator already published to the project;
//   3. failing both, the PDF itself -- downloading the bytes from the project
//      when this machine has never seen the file, then running the clusterer
//      on a worker thread.
//
// Requests are served one at a time on purpose: extraction and page rendering
// both funnel through PDFium's global lock, so running several at once would
// stall the reader's window for the length of the batch.
class PaperSource : public QObject
{
    Q_OBJECT
public:
    PaperSource(LibraryDb *db, LibraryModel *library,
                ProjectController *projects, FileSyncService *files,
                QObject *parent = nullptr);
    ~PaperSource() override;

    // Queue one library item. Answers with ready() or failed().
    void request(const QString &itemId);
    void cancel();
    bool busy() const { return m_busy; }

signals:
    void ready(const QString &itemId, const QString &paperId,
               const QString &title, const QVector<Block> &blocks);
    void failed(const QString &itemId, const QString &reason);
    // "downloading", "segmenting" — for the batch's status line.
    void progress(const QString &itemId, const QString &what);

private:
    void step();
    bool serveFromCache(const QString &paperId);
    bool serveFromProject(const QString &paperId);
    void extractFrom(const QString &path);
    void finishOk(const QVector<Block> &blocks);
    void finishErr(const QString &reason);

    LibraryDb *m_db;
    LibraryModel *m_library;
    ProjectController *m_projects;
    FileSyncService *m_files;

    QString m_itemId;
    QString m_paperId;
    QString m_title;
    QString m_password;
    bool m_busy = false;
    bool m_cancelled = false;
    QFutureWatcher<QVector<Block>> m_watcher;
};
