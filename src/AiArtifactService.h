#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "LibraryDb.h"

class ProjectController;
class SyncEngine;
class AuthController;
class PaperController;

// Shares AI精读 output (summary / translation / chat) into the project so
// collaborators can see it, attributed to its author. Stored as synced objects
// of type "ai_artifact", keyed deterministically by (project, paper, type,
// author) so each member keeps one artifact per paper per type (req 5.4).
class AiArtifactService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sharedCount READ sharedCount NOTIFY sharedCountChanged)
    Q_PROPERTY(bool canShare READ canShare NOTIFY contextChanged)

public:
    AiArtifactService(LibraryDb *db, ProjectController *projects,
                      SyncEngine *sync, AuthController *auth,
                      PaperController *paper, QObject *parent = nullptr);

    int sharedCount() const { return m_sharedCount; }
    bool canShare() const;

    // Publish the current paper's AI output of `type` (summary|translation|chat).
    Q_INVOKABLE void publish(const QString &type, const QString &payload,
                             const QString &model);
    // All shared artifacts for the current paper (every member), newest fields.
    Q_INVOKABLE QVariantList sharedForCurrent() const;

signals:
    void sharedCountChanged();
    void contextChanged();

private:
    void recompute();
    QString currentPaperId() const;

    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    AuthController *m_auth;
    PaperController *m_paper;
    int m_sharedCount = 0;
};
