#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>

class QAbstractItemModel;
class QFileSystemModel;
class QModelIndex;

// Library holds a single "open folder" — analogous to VSCode's open
// workspace folder. It owns a QFileSystemModel filtered to directories
// + *.pdf files; QML consumes the model via a TreeView. The last open
// folder is remembered across sessions.
class Library : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentFolder       READ currentFolder NOTIFY currentFolderChanged)
    Q_PROPERTY(QAbstractItemModel *model   READ model         CONSTANT)

public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    QString currentFolder() const { return m_currentFolder; }
    QAbstractItemModel *model() const;

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

signals:
    void currentFolderChanged();

private:
    QFileSystemModel *m_fs;
    QString m_currentFolder;
    QSettings m_qs;
};
