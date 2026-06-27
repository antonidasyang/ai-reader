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

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectProject(const QString &id);
    Q_INVOKABLE void createProject(const QString &name,
                                   const QString &description);
    Q_INVOKABLE void deleteProject(const QString &id);

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

private:
    void loadFromCache();
    void setStatus(const QString &s);
    void rebuildList();

    ApiClient *m_api;
    AuthController *m_auth;
    LibraryDb *m_db;
    QSettings m_qs;

    QList<ProjectRow> m_projects;
    QVariantList m_list;
    QString m_currentId;
    QVariantList m_members;
    QString m_status;
};
