#include "Library.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QModelIndex>
#include <QtConcurrent>

namespace {
constexpr auto kKeyLastFolder = "library/lastFolder";

// How many PDFs one walk collects before it declares the subtree too big
// to list. A folder that hits this is marked truncated: its listing is a
// subset, so it may be counted but never ticked in one go.
constexpr int kMaxPdfsPerScan = 20000;

// Second belt: a tree can be huge in directories while holding almost no
// PDFs, and then the cap above never fires. This one bounds the walk
// itself so a pathological share cannot keep a worker busy for ever.
constexpr int kMaxDirsPerScan = 200000;

// A directory we must not descend into. Symlinks are the obvious case;
// NTFS junctions are the dangerous one, because QFileInfo::isSymLink()
// says false for them and Windows profiles and cloud-drive mounts are
// full of junctions that point back at an ancestor. Descending one of
// those is not "slow", it never ends. Only directories are tested: a
// symlinked *file* is still a file worth listing, as it always was.
bool isDirectoryLink(const QFileInfo &fi)
{
    if (fi.isSymLink())
        return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    if (fi.isJunction())
        return true;
#endif
    return false;
}

// "/a/b" -> "/a/b/", but "C:/" stays "C:/". Used to test whether one
// path lies under another: a drive root already ends in the separator,
// and appending a second one would match nothing.
QString asPrefix(const QString &dir)
{
    return dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/');
}

// The walk itself, run on a worker thread by QtConcurrent. It touches
// nothing but its arguments — no model, no cache, no `this` — so the
// GUI thread stays free to repaint while it runs.
//
// Hand-rolled rather than QDirIterator(..., QDirIterator::Subdirectories)
// on purpose: that iterator only maintains its visited-links guard when
// FollowSymlinks is set, so without it a junction loop spins for ever.
// Here every directory is remembered by its canonical path before it is
// listed, so a loop — however the filesystem manufactures it — ends.
Library::Scan walkPdfs(const QString &dir, int generation,
                       std::shared_ptr<std::atomic_bool> cancelled)
{
    Library::Scan scan;
    scan.dir = dir;
    scan.generation = generation;

    QSet<QString> visited;
    QStringList pending{dir};
    int dirsSeen = 0;

    while (!pending.isEmpty()) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            // The folder was closed or swapped under us. Drop the partial
            // listing rather than hand back half a subtree.
            scan.abandoned = true;
            return scan;
        }

        const QString current = pending.takeLast();
        // canonicalFilePath() resolves links and junctions, so two paths
        // that reach the same directory collapse to one entry here. It is
        // empty for something that vanished mid-walk — skip that too.
        const QString real = QFileInfo(current).canonicalFilePath();
        if (real.isEmpty() || visited.contains(real))
            continue;
        visited.insert(real);
        if (++dirsSeen > kMaxDirsPerScan) {
            scan.truncated = true;
            break;
        }

        QDirIterator it(current, QDir::AllEntries | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) {
                if (!isDirectoryLink(fi))
                    pending.append(it.filePath());
                continue;
            }
            if (!fi.isFile())
                continue;   // broken link, socket, device — nothing to list
            if (fi.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
                continue;
            scan.pdfs.append(it.filePath());
            if (scan.pdfs.size() >= kMaxPdfsPerScan) {
                scan.truncated = true;
                pending.clear();
                break;
            }
        }
    }

    scan.pdfs.sort(Qt::CaseInsensitive);
    return scan;
}
} // namespace

Library::Library(QObject *parent)
    : QObject(parent)
    , m_fs(new QFileSystemModel(this))
    , m_scanCancel(std::make_shared<std::atomic_bool>(false))
{
    // Tree shows: every subdirectory + only *.pdf files. setNameFilters
    // applies to files only (dirs are always shown). setNameFilterDisables
    // false so non-matching files are hidden, not greyed out.
    m_fs->setNameFilters({QStringLiteral("*.pdf")});
    m_fs->setNameFilterDisables(false);
    m_fs->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fs->setReadOnly(true);

    // The subtree walks below are only valid while the tree is; the model
    // watches the filesystem, so its row changes are the signal that a
    // cached listing may be out of date. Two things keep that from
    // throwing every listing away every few milliseconds: rows that
    // arrive before the model has finished its first pass over a
    // directory tell us nothing new (our walk read the disk directly, and
    // saw those files already), and what is left is coalesced and then
    // applied only to the directories it can actually have changed.
    connect(m_fs, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path) {
                m_loadedDirs.insert(QDir::cleanPath(path));
            });
    connect(m_fs, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &parent) { noteModelChange(parent); });
    connect(m_fs, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &parent) { noteModelChange(parent); });

    m_invalidateTimer.setSingleShot(true);
    m_invalidateTimer.setInterval(400);
    connect(&m_invalidateTimer, &QTimer::timeout,
            this, &Library::flushInvalidations);

    connect(&m_scanWatcher, &QFutureWatcher<Scan>::finished,
            this, &Library::onScanFinished);

    // Restore the previous session's folder if it still exists. Like
    // VSCode reopening the last workspace.
    const QString last = m_qs.value(kKeyLastFolder).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        openFolder(QUrl::fromLocalFile(last));
}

Library::~Library()
{
    // The worker only ever touches copies of its arguments, so it is safe
    // for it to outlive us; asking it to stop just gets the process out
    // of a long walk faster at shutdown.
    if (m_scanCancel)
        m_scanCancel->store(true);
}

QAbstractItemModel *Library::model() const
{
    return m_fs;
}

int Library::scanLimit() const
{
    return kMaxPdfsPerScan;
}

void Library::openFolder(const QUrl &url)
{
    QString path;
    if (url.isLocalFile())
        path = url.toLocalFile();
    else if (url.scheme().isEmpty())
        path = url.toString();
    else
        return; // ignore non-local URLs (no support for remote browsing)

    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;
    if (path == m_currentFolder)
        return;

    m_currentFolder = path;
    abandonScans();
    m_fs->setRootPath(path);
    m_qs.setValue(kKeyLastFolder, path);
    m_qs.sync();
    emit currentFolderChanged();

    // Walk the open folder itself straight away. Everything the tree can
    // show lives under it, so this one walk is what every row below then
    // derives its own answer from — see deriveFromAncestor().
    requestScan(QDir::cleanPath(path));
}

void Library::close()
{
    if (m_currentFolder.isEmpty())
        return;
    m_currentFolder.clear();
    abandonScans();
    m_fs->setRootPath(QString{});
    m_qs.remove(kKeyLastFolder);
    m_qs.sync();
    emit currentFolderChanged();
}

QModelIndex Library::rootIndex() const
{
    if (m_currentFolder.isEmpty())
        return {};
    return m_fs->index(m_currentFolder);
}

bool Library::isDir(const QModelIndex &index) const
{
    return m_fs->isDir(index);
}

QUrl Library::fileUrl(const QModelIndex &index) const
{
    return QUrl::fromLocalFile(m_fs->filePath(index));
}

QString Library::filePath(const QModelIndex &index) const
{
    return m_fs->filePath(index);
}

QString Library::localFile(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : QString{};
}

QString Library::dirKey(const QModelIndex &index) const
{
    const QString path = index.isValid() ? m_fs->filePath(index)
                                         : m_currentFolder;
    return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

// ── The non-blocking readers QML binds to ───────────────────────────
// Both are const because they are read from QML bindings, and both do
// schedule work: the cast marks the one honest lie. Nothing they touch
// is part of what the Library *is* — only of what it has bothered to
// look up so far.

int Library::pdfCountUnder(const QModelIndex &index) const
{
    const QString dir = dirKey(index);
    if (dir.isEmpty())
        return 0;

    const QFileInfo fi(dir);
    if (fi.isFile()) {
        return fi.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
             ? 1 : 0;
    }

    const auto cached = m_pdfCache.constFind(dir);
    if (cached != m_pdfCache.constEnd())
        return cached->pdfs.size();

    const_cast<Library *>(this)->requestScan(dir);
    return -1;   // "not counted yet" — the row renders now, settles later
}

QStringList Library::pdfsUnderCached(const QModelIndex &index) const
{
    const QString dir = dirKey(index);
    if (dir.isEmpty())
        return {};

    const QFileInfo fi(dir);
    if (fi.isFile()) {
        return fi.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
             ? QStringList{fi.absoluteFilePath()} : QStringList{};
    }

    const auto cached = m_pdfCache.constFind(dir);
    if (cached != m_pdfCache.constEnd())
        return cached->pdfs;

    const_cast<Library *>(this)->requestScan(dir);
    return {};
}

bool Library::scanTruncated(const QModelIndex &index) const
{
    const QString dir = dirKey(index);
    if (dir.isEmpty())
        return false;
    const auto cached = m_pdfCache.constFind(dir);
    return cached != m_pdfCache.constEnd() && cached->truncated;
}

QStringList Library::pdfsUnder(const QModelIndex &index) const
{
    const QString dir = dirKey(index);
    if (dir.isEmpty())
        return {};
    // A file index: treat it as "just this one". This is the cheap case —
    // one QFileInfo, no walk — and the only one a delegate may ask for.
    const QFileInfo fi(dir);
    if (fi.isFile()) {
        return fi.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
             ? QStringList{fi.absoluteFilePath()} : QStringList{};
    }

    const auto cached = m_pdfCache.constFind(dir);
    if (cached != m_pdfCache.constEnd())
        return cached->pdfs;

    // Nothing cached and the caller needs the answer now: walk here, on
    // this thread, and keep the result so the rows above it settle too.
    // Same cap as the worker — a caller that blocks must at least block
    // for a bounded time.
    Scan scan = walkPdfs(dir, m_scanGeneration, {});
    m_pdfCache.insert(dir, scan);
    const_cast<Library *>(this)->notifyScanned({dir});
    return scan.pdfs;
}

// ── Scheduling ──────────────────────────────────────────────────────

void Library::requestScan(const QString &dir)
{
    if (dir.isEmpty() || m_currentFolder.isEmpty())
        return;
    if (m_pdfCache.contains(dir) || m_scanQueued.contains(dir)
        || dir == m_scanning)
        return;

    m_scanQueued.insert(dir);
    m_scanQueue.enqueue(dir);

    // Deferred on purpose. requestScan() is reached from inside a QML
    // binding, and pumping here would emit scanRevisionChanged() while
    // that very binding is being evaluated — a binding loop warning at
    // best. One trip through the event loop also lets a whole screenful
    // of rows queue up and be answered in a single pass.
    if (m_pumpQueued)
        return;
    m_pumpQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_pumpQueued = false;
        pumpScans();
    });
}

void Library::pumpScans()
{
    if (!m_scanning.isEmpty())
        return;   // one walk at a time; its handler pumps again

    QStringList landed;
    while (!m_scanQueue.isEmpty()) {
        const QString dir = m_scanQueue.dequeue();
        m_scanQueued.remove(dir);
        if (m_pdfCache.contains(dir))
            continue;   // a walk that landed meanwhile already answered it

        Scan derived;
        if (deriveFromAncestor(dir, &derived)) {
            m_pdfCache.insert(dir, derived);
            landed.append(dir);
            continue;   // free: no disk touched
        }

        m_scanning = dir;
        m_scanWatcher.setFuture(
            QtConcurrent::run(&walkPdfs, dir, m_scanGeneration, m_scanCancel));
        break;
    }

    // One notification for the whole batch: every bump re-evaluates the
    // count binding in every visible row, so bumping per derived folder
    // would be quadratic in the size of the screen.
    notifyScanned(landed);
}

void Library::onScanFinished()
{
    const QString dir = m_scanning;
    m_scanning.clear();

    if (m_scanWatcher.isCanceled() || m_scanWatcher.future().resultCount() == 0) {
        pumpScans();
        return;
    }

    const Scan scan = m_scanWatcher.result();
    // A walk started for a folder we have since closed or swapped must
    // not write into the cache the new folder is filling.
    if (!scan.abandoned && scan.generation == m_scanGeneration
        && !m_currentFolder.isEmpty()) {
        m_pdfCache.insert(scan.dir.isEmpty() ? dir : scan.dir, scan);
        notifyScanned({scan.dir.isEmpty() ? dir : scan.dir});
    }

    pumpScans();
}

bool Library::deriveFromAncestor(const QString &dir, Scan *out) const
{
    if (!out || m_currentFolder.isEmpty())
        return false;

    const QString root = QDir::cleanPath(m_currentFolder);
    const QString prefix = asPrefix(dir);

    QString parent = QFileInfo(dir).path();
    for (;;) {
        const auto cached = m_pdfCache.constFind(parent);
        if (cached != m_pdfCache.constEnd()) {
            // A truncated ancestor stopped early, so "not in its list"
            // does not mean "not on disk".
            if (cached->truncated)
                return false;
            QStringList mine;
            for (const QString &p : cached->pdfs)
                if (p.startsWith(prefix))
                    mine.append(p);
            // An empty result is exactly what a directory the ancestor
            // never entered looks like — a symlinked or junctioned
            // subfolder, which our walk deliberately steps over. Refusing
            // to derive "nothing" costs one walk of a folder that has no
            // PDFs and buys back the right answer for the linked ones.
            if (mine.isEmpty())
                return false;
            out->dir = dir;
            out->pdfs = std::move(mine);
            out->truncated = false;
            out->abandoned = false;
            out->generation = m_scanGeneration;
            return true;
        }
        if (parent.size() <= root.size())
            return false;
        const QString up = QFileInfo(parent).path();
        if (up == parent || up.isEmpty())
            return false;
        parent = up;
    }
}

void Library::notifyScanned(const QStringList &dirs)
{
    if (dirs.isEmpty())
        return;
    ++m_scanRevision;
    for (const QString &dir : dirs)
        emit scanned(dir);
    emit scanRevisionChanged();
}

void Library::abandonScans()
{
    // Whatever is walking now belongs to the folder we are leaving. The
    // generation bump is what makes its result unusable; the flag only
    // asks it to notice sooner. A fresh flag goes with the new
    // generation so the next walk is not born cancelled.
    if (m_scanCancel)
        m_scanCancel->store(true);
    m_scanCancel = std::make_shared<std::atomic_bool>(false);
    ++m_scanGeneration;

    m_scanQueue.clear();
    m_scanQueued.clear();
    // m_scanning is deliberately left alone: the in-flight walk still
    // owes us a finished(), and its handler is what clears it and starts
    // the new folder's first walk.
    m_pdfCache.clear();
    m_loadedDirs.clear();
    m_dirty.clear();
    m_invalidateTimer.stop();

    ++m_scanRevision;
    emit scanRevisionChanged();
}

void Library::noteModelChange(const QModelIndex &parent)
{
    if (m_currentFolder.isEmpty())
        return;
    const QString dir = QDir::cleanPath(m_fs->filePath(parent));
    if (dir.isEmpty())
        return;
    // Rows delivered before the model announces the directory as loaded
    // are its first listing, arriving in batches from the model's own
    // gatherer thread. Our walk read the disk directly and already saw
    // those files, so treating each batch as a change would throw the
    // listing away over and over and re-walk the tree from cold.
    if (!m_loadedDirs.contains(dir))
        return;

    m_dirty.insert(dir);
    if (!m_invalidateTimer.isActive())
        m_invalidateTimer.start();
}

void Library::flushInvalidations()
{
    if (m_dirty.isEmpty())
        return;
    const QSet<QString> dirty = std::move(m_dirty);
    m_dirty.clear();

    bool dropped = false;
    for (const QString &dir : dirty) {
        // A file appearing under `dir` changes the answer for `dir`, for
        // every folder above it up to the open folder, and — since the
        // model reports a whole subtree's arrival at its top — for
        // anything cached below it. Everything else stays.
        const QString root = QDir::cleanPath(m_currentFolder);
        QString up = dir;
        for (;;) {
            dropped |= m_pdfCache.remove(up) > 0;
            if (up.size() <= root.size())
                break;
            const QString next = QFileInfo(up).path();
            if (next == up || next.isEmpty())
                break;
            up = next;
        }

        const QString prefix = asPrefix(dir);
        for (auto it = m_pdfCache.begin(); it != m_pdfCache.end();) {
            if (it.key().startsWith(prefix)) {
                it = m_pdfCache.erase(it);
                dropped = true;
            } else {
                ++it;
            }
        }
    }

    if (dropped) {
        // The rows those listings backed go back to "not counted yet" and
        // ask for a fresh walk on their next binding evaluation.
        ++m_scanRevision;
        emit scanRevisionChanged();
    }
}
