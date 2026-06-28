#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "LibraryDb.h"

class ProjectController;

// Full-text search over the current project's library, backed by LibraryDb's
// local FTS5 index (so it works offline). Returns lightweight result maps for
// QML. Indexing happens in SyncEngine as objects are applied.
class SearchService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    SearchService(LibraryDb *db, ProjectController *projects,
                  QObject *parent = nullptr);

    bool available() const;
    Q_INVOKABLE QVariantList search(const QString &query) const;

private:
    LibraryDb *m_db;
    ProjectController *m_projects;
};
