#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>

#include "LibraryDb.h"

class ApiClient;
class AuthController;

// Client-side projects (课题) + members + the current-project context that the
// whole library UI / sync is scoped by. Lists are small, so they are exposed as
// QVariantList rather than full models. Caches the project list in LibraryDb for
// offline display and remembers the last-selected project across launches.
class ProjectController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QString currentRole READ currentRole NOTIFY currentChanged)
    Q_PROPERTY(bool canWrite READ canWrite NOTIFY currentChanged)
    Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Whether the local store will answer for the project on screen, and
    // why not when it won't: "signed-out", "other-account", or empty. The
    // library pane's empty state says which, because "no papers yet" and
    // "these papers are not yours to read" are very different sentences.
    Q_PROPERTY(bool libraryReadable READ libraryReadable NOTIFY lockChanged)
    Q_PROPERTY(QString libraryLockReason READ libraryLockReason NOTIFY lockChanged)

public:
    ProjectController(ApiClient *api, AuthController *auth, LibraryDb *db,
                      QObject *parent = nullptr);

    QVariantList list() const { return m_list; }
    QString currentId() const { return m_currentId; }
    QString currentName() const;
    QString currentRole() const;
    bool canWrite() const;
    QVariantList members() const { return m_members; }
    QString status() const { return m_status; }
    bool libraryReadable() const;
    QString libraryLockReason() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectProject(const QString &id);
    Q_INVOKABLE void createProject(const QString &name,
                                   const QString &description);
    // Rename / re-describe. Server-side this needs editor or owner
    // (assertWriter), which `canWrite` already mirrors.
    Q_INVOKABLE void updateProject(const QString &id, const QString &name,
                                   const QString &description);
    // Owner-only server-side, and irreversible: the server cascades
    // every item, annotation and note in the project, for every
    // member. Callers must confirm first — see unsyncedCount().
    Q_INVOKABLE void deleteProject(const QString &id);
    // Local changes for `id` that have not reached the server yet.
    // Deleting a project throws them away, so the confirmation says so.
    Q_INVOKABLE int unsyncedCount(const QString &id) const;
    Q_INVOKABLE QString descriptionOf(const QString &id) const;

    Q_INVOKABLE void refreshMembers();
    Q_INVOKABLE void addMember(const QString &email, const QString &role);
    Q_INVOKABLE void updateMemberRole(const QString &userId,
                                      const QString &role);
    Q_INVOKABLE void removeMember(const QString &userId);

signals:
    void listChanged();
    void currentChanged();
    void membersChanged();
    void statusChanged();
    void lockChanged();

private:
    void loadFromCache();
    void setStatus(const QString &s);
    void rebuildList();
    // The one place `m_currentId` may change. It refuses a project this
    // session is not allowed to read — leaving one selected is what let a
    // signed-out window keep listing the previous user's papers, and what
    // left a fresh sign-in pointed at the last account's project when
    // GET /projects failed — persists it, and announces the change.
    void setCurrentId(const QString &id);
    // Re-run that decision without changing anything else: after a failed
    // refresh, or when the session under us changed.
    void revalidateCurrent() { setCurrentId(m_currentId); }
    // A session opened (signed in, or the user id finally arrived) / ended
    // (signed out, or a refresh token the server refused).
    void onSessionOpened();
    void onSessionClosed();

    ApiClient *m_api;
    AuthController *m_auth;
    LibraryDb *m_db;
    QSettings m_qs;

    QList<ProjectRow> m_projects;
    QVariantList m_list;
    QString m_currentId;
    // The account the store was last opened for, so the two signals that
    // announce one sign-in don't fetch the project list twice.
    QString m_openedFor;
    QVariantList m_members;
    QString m_status;
};
