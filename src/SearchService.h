#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "LibraryDb.h"

class ProjectController;

// Full-text search over the current project's library, backed by LibraryDb's
// local FTS5 index (so it works offline). Returns lightweight result maps for
// QML. Indexing happens in SyncEngine as objects are applied.
//
// Both halves of a hit -- the index lookup and the row it names -- are read
// through LibraryDb, so the store's account gate answers first: a signed-out
// window, or one signed in on somebody else's store, searches nothing.
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
