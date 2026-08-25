#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <memory>

class QAbstractItemModel;
class QFileSystemModel;
class QModelIndex;

// Library holds a single "open folder" — analogous to VSCode's open
// workspace folder. It owns a QFileSystemModel filtered to directories
// + *.pdf files; QML consumes the model via a TreeView. The last open
// folder is remembered across sessions.
//
// It also answers "how many PDFs are under this folder?" for the tree
// pane. That answer costs a recursive walk of the subtree, which on a
// big tree — or any tree behind antivirus, a network share or OneDrive
// placeholders — takes seconds, so it is never computed on the GUI
// thread for a delegate. One walk at a time runs on a worker; rows read
// the cache and re-read it when scanRevision says a walk landed.
class Library : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentFolder       READ currentFolder NOTIFY currentFolderChanged)
    Q_PROPERTY(QAbstractItemModel *model   READ model         CONSTANT)
    // Bumped every time a subtree walk lands or a cached one is dropped.
    // QML bindings that show a folder's PDF count read this first, so a
    // row that rendered with "not counted yet" refreshes by itself when
    // its walk finishes. Monotonic: never reset, not even on close().
    Q_PROPERTY(int scanRevision            READ scanRevision  NOTIFY scanRevisionChanged)
    // How many PDFs one walk collects before it gives up. Exposed so the
    // pane can name the number in its "too many to tick" tooltip.
    Q_PROPERTY(int scanLimit               READ scanLimit     CONSTANT)

public:
    // One directory's recursive *.pdf listing. `truncated` says the walk
    // stopped early (the cap below, or a pathological tree), so the list
    // is a subset and must not back a "tick everything under here".
    // `generation` records which open folder it was started for: a walk
    // that outlives its folder is dropped rather than written into the
    // next folder's cache. `abandoned` marks a walk that bailed out on
    // the cancel flag and carries no listing at all.
    struct Scan {
        QString     dir;
        QStringList pdfs;
        bool        truncated = false;
        bool        abandoned = false;
        int         generation = 0;
    };

    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    QString currentFolder() const { return m_currentFolder; }
    QAbstractItemModel *model() const;
    int scanRevision() const { return m_scanRevision; }
    int scanLimit() const;

    // Open a folder (local URL or path) and start watching it. Persists
    // the path to QSettings so the next session restores it.
    Q_INVOKABLE void openFolder(const QUrl &url);

    // Drop the current folder. Tree empties; persisted path is cleared.
    Q_INVOKABLE void close();

    // QFileSystemModel covers the whole filesystem, so the TreeView has
    // to be scoped to the chosen folder via rootIndex. Callers re-fetch
    // this whenever currentFolder changes.
    Q_INVOKABLE QModelIndex rootIndex() const;

    // Tiny wrappers so QML delegates can ask "is this a directory?" and
    // "what's the URL?" without exposing QFileSystemModel directly.
    Q_INVOKABLE bool    isDir(const QModelIndex &index) const;
    Q_INVOKABLE QUrl    fileUrl(const QModelIndex &index) const;
    Q_INVOKABLE QString filePath(const QModelIndex &index) const;

    // The local path behind a file:// URL, "" for anything else. Lets the
    // pane resolve the playing paper's path once per paper change instead
    // of asking a filesystem question per row.
    Q_INVOKABLE QString localFile(const QUrl &url) const;

    // ── Subtree PDF counts, for the tree pane ───────────────────────
    // How many PDFs live under `index`, or -1 when nobody has walked it
    // yet — in which case the walk is scheduled and scanRevision will
    // change once it lands. A file index answers immediately (one
    // QFileInfo). This is the call a delegate binding may make: it never
    // touches the filesystem for a directory.
    Q_INVOKABLE int pdfCountUnder(const QModelIndex &index) const;

    // The listing behind that count: empty while the walk is pending (and
    // the walk is scheduled), so callers must gate on pdfCountUnder() >= 0
    // before treating "empty" as "no PDFs here".
    Q_INVOKABLE QStringList pdfsUnderCached(const QModelIndex &index) const;

    // True when the cached listing for `index` stopped at the cap, i.e.
    // it is a subset of what is really there. A truncated folder must not
    // offer "select everything under here": it would silently act on part
    // of the subtree.
    Q_INVOKABLE bool scanTruncated(const QModelIndex &index) const;

    // Every *.pdf under `index` (recursively), as absolute paths, sorted.
    // An invalid index means the whole open folder. The tree only
    // materialises rows the user has expanded, so "select everything
    // here" has to come from the filesystem, not from the delegates.
    //
    // BLOCKING. It walks the subtree on the calling thread when the cache
    // has no answer, so it belongs only in a handler for something the
    // user just clicked and whose answer is needed now. A delegate must
    // never call it for a directory — that is what froze the pane on a
    // large tree; use pdfCountUnder()/pdfsUnderCached() there. Capped the
    // same way the background walk is.
    Q_INVOKABLE QStringList pdfsUnder(const QModelIndex &index) const;

signals:
    void currentFolderChanged();
    void scanRevisionChanged();
    // One directory's listing just became available (or was dropped).
    // scanRevision is the binding-friendly form of the same news; this is
    // for anything that cares about a specific folder.
    void scanned(const QString &dir);

private:
    // Cache key for an index: the cleaned absolute directory path, or the
    // open folder for an invalid index. Cleaning matters because the
    // derive-from-ancestor shortcut below compares path prefixes.
    QString dirKey(const QModelIndex &index) const;

    // Queue `dir` for a background walk unless it is already cached,
    // already queued, or derivable from a subtree we have already walked.
    void requestScan(const QString &dir);
    // Start the next queued walk, if none is running. Runs from the event
    // loop, never from inside a QML binding, so the notifications it
    // emits can't re-enter a binding that is mid-evaluation.
    void pumpScans();
    void onScanFinished();
    // A completed walk of an ancestor already lists everything under this
    // directory, so a child's answer is a prefix filter over the parent's
    // list instead of another walk of the disk.
    bool deriveFromAncestor(const QString &dir, Scan *out) const;
    void notifyScanned(const QStringList &dirs);
    // Forget every cached listing and make any in-flight walk irrelevant.
    void abandonScans();
    // A row appeared or vanished under `parent`: the listings covering it
    // may be stale. Coalesced, because the model delivers its first
    // listing of a directory in batches from its own gatherer thread.
    void noteModelChange(const QModelIndex &parent);
    void flushInvalidations();

    QFileSystemModel *m_fs;
    QString m_currentFolder;
    QSettings m_qs;
    // Walk results, keyed by directory. The tree pane asks once per
    // delegate and rebuilds delegates as it scrolls, so without this a
    // folder's subtree would be walked again on every scroll. Dropped for
    // the directories a filesystem change actually touched, and wholesale
    // on every folder switch. Mutable because the blocking pdfsUnder()
    // fills it from a const getter.
    mutable QHash<QString, Scan> m_pdfCache;

    // One walk at a time: a dozen concurrent recursive walks of the same
    // disk are slower than one, and far worse on a network share.
    QQueue<QString>     m_scanQueue;
    QSet<QString>       m_scanQueued;   // membership test for the queue
    QString             m_scanning;     // directory being walked, "" if idle
    QFutureWatcher<Scan> m_scanWatcher;
    bool                m_pumpQueued = false;

    // Which open folder in-flight work belongs to. Bumped on every folder
    // switch so a walk that was already running cannot write into the new
    // folder's cache. The flag next to it asks that walk to stop early;
    // it is best-effort — correctness comes from the generation.
    int m_scanGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_scanCancel;

    int m_scanRevision = 0;

    // Directories the model has finished its first listing of. Rows that
    // arrive before that are the model catching up with what our walk
    // already saw on disk, not a change to it.
    QSet<QString> m_loadedDirs;
    QSet<QString> m_dirty;
    QTimer m_invalidateTimer;
};
