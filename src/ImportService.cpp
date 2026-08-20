#include "ImportService.h"

#include "FileSyncService.h"
#include "LibraryModel.h"
#include "MetadataService.h"
#include "PaperController.h"
#include "ProjectController.h"

#include <QFileInfo>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QTimer>
#include <QUrl>

ImportService::ImportService(LibraryModel *library, FileSyncService *files,
                             MetadataService *metadata,
                             ProjectController *projects, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_files(files)
    , m_metadata(metadata)
    , m_projects(projects)
{
    // Pace the queue on upload completions. The id check keeps a manual
    // "+ Add" upload that lands mid-import from advancing our queue.
    connect(m_files, &FileSyncService::paperUploaded, this,
            [this](const QString &itemId, bool ok) {
                if (!m_running || itemId.isEmpty()
                        || itemId != m_pendingItemId)
                    return;
                m_pendingItemId.clear();
                if (ok)
                    ++m_added;
                else
                    ++m_failed;
                ++m_done;
                emit progressChanged();
                stepLater();
            });
}

void ImportService::importFiles(const QStringList &paths)
{
    if (m_running)
        return;
    if (!m_projects || m_projects->currentId().isEmpty()) {
        setStatus(tr("Pick a project first."));
        return;
    }

    m_queue.clear();
    for (const QString &p : paths) {
        const QString local = p.startsWith(QLatin1String("file:"))
                            ? QUrl(p).toLocalFile() : p;
        if (!local.isEmpty())
            m_queue.enqueue(local);
    }
    if (m_queue.isEmpty())
        return;

    m_running = true;
    m_cancelled = false;
    m_total = m_queue.size();
    m_done = m_added = m_skipped = m_failed = 0;
    m_pendingItemId.clear();
    emit progressChanged();
    step();
}

void ImportService::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    m_queue.clear();
    setStatus(tr("Import cancelled."));
    // The in-flight upload still reports back and closes out the run;
    // if there is none, close it out now.
    if (m_pendingItemId.isEmpty())
        finish();
}

void ImportService::stepLater()
{
    // Never recurse into step() from inside it: uploadPaper() answers
    // synchronously on some failure paths, and a folder of hundreds of
    // files would then nest that deep.
    QTimer::singleShot(0, this, &ImportService::step);
}

void ImportService::step()
{
    if (!m_running)
        return;
    if (m_cancelled || m_queue.isEmpty()) {
        finish();
        return;
    }

    const QString path = m_queue.dequeue();
    const QFileInfo fi(path);
    setStatus(tr("Adding %1 (%2/%3)…")
                  .arg(fi.fileName())
                  .arg(m_done + 1)
                  .arg(m_total));

    const QString paperId = PaperController::paperIdForFile(path);
    if (paperId.isEmpty()) {          // unreadable / gone
        ++m_failed;
        ++m_done;
        emit progressChanged();
        stepLater();
        return;
    }
    if (!m_library->findByPaperId(paperId).isEmpty()) {
        ++m_skipped;
        ++m_done;
        emit progressChanged();
        stepLater();
        return;
    }

    // Store the URL form: that's what the library pane's open path and
    // FileSyncService both already accept, and what "+ Add" writes.
    const QString itemId =
        m_library->addPaper(fi.fileName(), paperId,
                            QUrl::fromLocalFile(fi.absoluteFilePath()).toString());
    if (itemId.isEmpty()) {           // project went away mid-run
        ++m_failed;
        ++m_done;
        emit progressChanged();
        finish();
        return;
    }

    // Bibliographic lookup is fire-and-forget: it's a small GET that
    // rewrites the title when it lands, and a paper without a DOI must
    // not hold up the rest of the queue.
    const QString head = headTextOf(path);
    if (!head.isEmpty())
        m_metadata->autoFillFromText(itemId, head);

    m_pendingItemId = itemId;
    m_files->uploadPaper(itemId, QUrl::fromLocalFile(fi.absoluteFilePath()).toString());
}

void ImportService::finish()
{
    if (!m_running)
        return;
    m_running = false;
    m_pendingItemId.clear();
    m_queue.clear();
    if (!m_cancelled) {
        setStatus(tr("Added %1, skipped %2, failed %3.")
                      .arg(m_added).arg(m_skipped).arg(m_failed));
    }
    emit progressChanged();
    emit finished(m_added, m_skipped, m_failed);
}

QString ImportService::headTextOf(const QString &localPath, int maxChars)
{
    QPdfDocument doc;
    if (doc.load(localPath) != QPdfDocument::Error::None)
        return {};
    if (doc.pageCount() < 1)
        return {};
    QString text = doc.getAllText(0).text();
    // A few papers carry the DOI on page 2 (title page is a cover);
    // one extra page is cheap and covers most of them.
    if (text.size() < maxChars && doc.pageCount() > 1)
        text += QLatin1Char('\n') + doc.getAllText(1).text();
    return text.left(maxChars);
}

void ImportService::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}
