#include "ProjectController.h"
#include "ApiClient.h"
#include "AuthController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

ProjectController::ProjectController(ApiClient *api, AuthController *auth,
                                     LibraryDb *db, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_auth(auth)
    , m_db(db)
{
    m_currentId = m_qs.value(QStringLiteral("project/currentId")).toString();
    loadFromCache();

    // Re-fetch when the session changes; clear members on sign-out.
    connect(m_auth, &AuthController::authenticatedChanged, this, [this] {
        if (m_auth->authenticated()) {
            refresh();
        } else {
            m_members.clear();
            emit membersChanged();
        }
    });
    if (m_auth->authenticated())
        refresh();
}

void ProjectController::loadFromCache()
{
    m_projects = m_db->projects();
    rebuildList();
}

void ProjectController::rebuildList()
{
    m_list.clear();
    bool currentStillPresent = false;
    for (const ProjectRow &p : m_projects) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.name);
        m.insert(QStringLiteral("description"), p.description);
        m.insert(QStringLiteral("role"), p.role);
        m.insert(QStringLiteral("version"), QString::number(p.version));
        m_list.append(m);
        if (p.id == m_currentId)
            currentStillPresent = true;
    }
    if (!currentStillPresent) {
        m_currentId = m_projects.isEmpty() ? QString() : m_projects.first().id;
        m_qs.setValue(QStringLiteral("project/currentId"), m_currentId);
    }
    emit listChanged();
    emit currentChanged();
}

QString ProjectController::currentName() const
{
    for (const ProjectRow &p : m_projects)
        if (p.id == m_currentId)
            return p.name;
    return {};
}

QString ProjectController::currentRole() const
{
    for (const ProjectRow &p : m_projects)
        if (p.id == m_currentId)
            return p.role;
    return {};
}

bool ProjectController::canWrite() const
{
    const QString r = currentRole();
    return r == QLatin1String("owner") || r == QLatin1String("editor");
}

void ProjectController::refresh()
{
    m_api->get(QStringLiteral("/projects"),
               [this](bool ok, int status, const QJsonDocument &doc) {
                   if (!ok) {
                       setStatus(tr("Could not load projects (HTTP %1)").arg(status));
                       return;
                   }
                   m_projects.clear();
                   const QJsonArray arr = doc.array();
                   for (const QJsonValue &v : arr) {
                       const QJsonObject o = v.toObject();
                       ProjectRow p;
                       p.id = o.value(QStringLiteral("id")).toString();
                       p.name = o.value(QStringLiteral("name")).toString();
                       p.description =
                           o.value(QStringLiteral("description")).toString();
                       p.role = o.value(QStringLiteral("role")).toString();
                       p.version =
                           o.value(QStringLiteral("version")).toString().toLongLong();
                       m_projects.append(p);
                   }
                   m_db->replaceProjects(m_projects);
                   rebuildList();
                   if (!m_currentId.isEmpty())
                       refreshMembers();
               });
}

void ProjectController::selectProject(const QString &id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    m_qs.setValue(QStringLiteral("project/currentId"), id);
    emit currentChanged();
    refreshMembers();
}

void ProjectController::createProject(const QString &name,
                                      const QString &description)
{
    QJsonObject body{{QStringLiteral("name"), name}};
    if (!description.isEmpty())
        body.insert(QStringLiteral("description"), description);
    m_api->post(QStringLiteral("/projects"), body,
                [this](bool ok, int status, const QJsonDocument &doc) {
                    if (!ok) {
                        setStatus(tr("Create failed (HTTP %1)").arg(status));
                        return;
                    }
                    const QString id =
                        doc.object().value(QStringLiteral("id")).toString();
                    m_currentId = id;
                    m_qs.setValue(QStringLiteral("project/currentId"), id);
                    refresh();
                });
}

void ProjectController::deleteProject(const QString &id)
{
    m_api->del(QStringLiteral("/projects/") + id,
               [this](bool ok, int status, const QJsonDocument &) {
                   if (!ok) {
                       setStatus(tr("Delete failed (HTTP %1)").arg(status));
                       return;
                   }
                   refresh();
               });
}

void ProjectController::refreshMembers()
{
    if (m_currentId.isEmpty()) {
        m_members.clear();
        emit membersChanged();
        return;
    }
    m_api->get(QStringLiteral("/projects/") + m_currentId + QStringLiteral("/members"),
               [this](bool ok, int, const QJsonDocument &doc) {
                   if (!ok)
                       return;
                   m_members.clear();
                   for (const QJsonValue &v : doc.array()) {
                       const QJsonObject o = v.toObject();
                       QVariantMap m;
                       m.insert(QStringLiteral("userId"),
                                o.value(QStringLiteral("userId")).toString());
                       m.insert(QStringLiteral("email"),
                                o.value(QStringLiteral("email")).toString());
                       m.insert(QStringLiteral("displayName"),
                                o.value(QStringLiteral("displayName")).toString());
                       m.insert(QStringLiteral("role"),
                                o.value(QStringLiteral("role")).toString());
                       m_members.append(m);
                   }
                   emit membersChanged();
               });
}

void ProjectController::addMember(const QString &email, const QString &role)
{
    if (m_currentId.isEmpty())
        return;
    QJsonObject body{{QStringLiteral("email"), email},
                     {QStringLiteral("role"), role}};
    m_api->post(QStringLiteral("/projects/") + m_currentId + QStringLiteral("/members"),
                body, [this](bool ok, int status, const QJsonDocument &doc) {
                    if (!ok) {
                        const QString msg =
                            doc.object().value(QStringLiteral("message")).toString();
                        setStatus(msg.isEmpty()
                                      ? tr("Invite failed (HTTP %1)").arg(status)
                                      : msg);
                        return;
                    }
                    refreshMembers();
                });
}

void ProjectController::updateMemberRole(const QString &userId,
                                         const QString &role)
{
    if (m_currentId.isEmpty())
        return;
    QJsonObject body{{QStringLiteral("role"), role}};
    m_api->patch(QStringLiteral("/projects/") + m_currentId
                     + QStringLiteral("/members/") + userId,
                 body, [this](bool ok, int, const QJsonDocument &) {
                     if (ok)
                         refreshMembers();
                 });
}

void ProjectController::removeMember(const QString &userId)
{
    if (m_currentId.isEmpty())
        return;
    m_api->del(QStringLiteral("/projects/") + m_currentId
                   + QStringLiteral("/members/") + userId,
               [this](bool ok, int, const QJsonDocument &) {
                   if (ok)
                       refreshMembers();
               });
}

void ProjectController::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}
