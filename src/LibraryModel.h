#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QVariantMap>

#include "LibraryDb.h"

class ProjectController;
class SyncEngine;

// List of bibliographic items in the current project, backed by the local
// LibraryDb (objects of type "item"). Edits go through SyncEngine.putObject so
// they enter the outbox and sync. Reloads when the project changes or a sync
// completes.
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CreatorsRole,
        YearRole,
        PublicationRole,
        ItemTypeRole,
        PaperIdRole,
        LocalPathRole,
        DoiRole,
        ArxivRole,
    };

    LibraryModel(LibraryDb *db, ProjectController *projects, SyncEngine *sync,
                 QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    // Create an item for a PDF. Returns the new item's id, or empty when
    // there's no current project.
    Q_INVOKABLE QString addPaper(const QString &title,
                                 const QString &paperId,
                                 const QString &localPath);
    // Id of the item already carrying `paperId`, or empty. Lets a batch
    // import skip files the project already has instead of piling up
    // duplicates on every re-run over the same folder.
    Q_INVOKABLE QString findByPaperId(const QString &paperId) const;
    // The path a paper was imported from, or empty. A paper opened out of
    // the project's blob cache is playing from a sha256-named file, so
    // "is this row the paper on screen" cannot be answered by comparing
    // paths alone -- this is the other end of that comparison.
    Q_INVOKABLE QString localPathForPaperId(const QString &paperId) const;
    Q_INVOKABLE QVariantMap itemFields(const QString &id) const;
    Q_INVOKABLE void updateItem(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void removeItem(const QString &id);

signals:
    void countChanged();

private:
    static QString creatorsDisplay(const QJsonObject &data);

    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    QList<SyncObjectRow> m_items;
};
