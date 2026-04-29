#include "Library.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QModelIndex>

namespace {
constexpr auto kKeyLastFolder = "library/lastFolder";
} // namespace

Library::Library(QObject *parent)
    : QObject(parent)
    , m_fs(new QFileSystemModel(this))
{
    // Tree shows: every subdirectory + only *.pdf files. setNameFilters
    // applies to files only (dirs are always shown). setNameFilterDisables
    // false so non-matching files are hidden, not greyed out.
    m_fs->setNameFilters({QStringLiteral("*.pdf")});
    m_fs->setNameFilterDisables(false);
    m_fs->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fs->setReadOnly(true);

    // Restore the previous session's folder if it still exists. Like
    // VSCode reopening the last workspace.
    const QString last = m_qs.value(kKeyLastFolder).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        openFolder(QUrl::fromLocalFile(last));
}

Library::~Library() = default;

QAbstractItemModel *Library::model() const
{
    return m_fs;
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
    m_fs->setRootPath(path);
    m_qs.setValue(kKeyLastFolder, path);
    m_qs.sync();
    emit currentFolderChanged();
}

void Library::close()
{
    if (m_currentFolder.isEmpty())
        return;
    m_currentFolder.clear();
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
