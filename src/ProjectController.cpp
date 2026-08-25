#include "ProjectController.h"
#include "ApiClient.h"
#include "AuthController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

ProjectController::ProjectController(ApiClient *api, AuthController *auth,
                                     LibraryDb *db, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_auth(auth)
    , m_db(db)
{
    // A session that was open when the app last quit is still open: the
    // store remembers whose it is, so an owner with no network still gets
    // their own library. Only a sign-out (or a refresh the server refuses)
    // closes it — see onSessionClosed().
    if (m_auth->authenticated() && !m_auth->userId().isEmpty()) {
        m_openedFor = m_auth->userId();
        m_db->openSession(m_openedFor);
    }
    m_currentId = m_qs.value(QStringLiteral("project/currentId")).toString();
    loadFromCache();
    // ...and if the remembered project is not this session's to read, it is
    // not the current project either.
    revalidateCurrent();

    // Re-fetch when the session changes; clear members on sign-out.
    connect(m_auth, &AuthController::authenticatedChanged, this, [this] {
        if (m_auth->authenticated())
            onSessionOpened();
        else
            onSessionClosed();
    });
    // CAS marks the session authenticated before /auth/me answers, so the
    // account behind it can arrive a moment later. That is when the store
    // learns whose session this is.
    connect(m_auth, &AuthController::userChanged, this, [this] {
        // Only ever to *learn* who is signed in: signing out clears the
        // user id first and the session flag second, and that first half is
        // not a session opening.
        if (m_auth->authenticated() && !m_auth->userId().isEmpty())
            onSessionOpened();
    });
    if (m_auth->authenticated())
        refresh();
}

void ProjectController::onSessionOpened()
{
    const QString uid = m_auth->userId();
    if (!uid.isEmpty()) {
        if (uid == m_openedFor)
            return;              // the same sign-in, announced twice
        m_openedFor = uid;
        m_db->openSession(uid);
        // The gate just moved: the cached list, the current project and
        // everything reading through them have to be re-decided.
        loadFromCache();
        emit lockChanged();
    }
    refresh();
}

void ProjectController::onSessionClosed()
{
    // Signing out forgets which project was open and shuts the store's
    // gate, so the library pane empties, search answers nothing and the
    // analysis views go blank. Nothing on disk is touched: an un-pushed
    // outbox is still there — and still pushes — when its user returns.
    m_openedFor.clear();
    m_db->closeSession();
    setCurrentId(QString());
    loadFromCache();
    m_members.clear();
    emit membersChanged();
    emit lockChanged();
    // The gate closed even when there was no project selected to forget,
    // and every reader keys off this signal.
    emit currentChanged();
}

bool ProjectController::libraryReadable() const
{
    return m_db && m_db->canRead(m_currentId);
}

QString ProjectController::libraryLockReason() const
{
    if (!m_db)
        return {};
    const QString reason = m_db->lockReason();
    if (reason != QLatin1String("other-account"))
        return reason;
    // Another account is signed in. Say so only while we are actually
    // looking at something of the store owner's: this user's own projects
    // read perfectly well, they are simply empty until their sync fills
    // them.
    if (!m_currentId.isEmpty() && m_db->canRead(m_currentId))
        return {};
    return reason;
}

void ProjectController::setCurrentId(const QString &id)
{
    // A project this session may not read is not a project it can be in.
    const QString next = (m_db && !m_db->canRead(id)) ? QString() : id;
    if (next == m_currentId)
        return;
    m_currentId = next;
    if (next.isEmpty())
        m_qs.remove(QStringLiteral("project/currentId"));
    else
        m_qs.setValue(QStringLiteral("project/currentId"), next);
    emit currentChanged();
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
    if (!currentStillPresent)
        setCurrentId(m_projects.isEmpty() ? QString() : m_projects.first().id);
    else
        revalidateCurrent();
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
                       // Without a list we cannot say this account belongs
                       // to whatever is selected. Keeping it is how a new
                       // sign-in ended up sitting in the previous user's
                       // project; the gate answers that, and so does this.
                       revalidateCurrent();
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
                   // The server listing these under this account's token is
                   // the only proof of membership a client gets: claim the
                   // ones nobody holds yet, so this user's own projects are
                   // theirs to read on this machine. Projects another
                   // account already claimed are never re-assigned.
                   QStringList ids;
                   ids.reserve(m_projects.size());
                   for (const ProjectRow &p : m_projects)
                       ids << p.id;
                   m_db->claimProjects(ids, m_auth->userId());
                   emit lockChanged();
                   rebuildList();
                   if (!m_currentId.isEmpty())
                       refreshMembers();
               });
}

void ProjectController::selectProject(const QString &id)
{
    if (id == m_currentId)
        return;
    setCurrentId(id);
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
                    // Through the setter: assigning the field directly left
                    // every view bound to `currentChanged` — the library
                    // pane included — listing the project we just left.
                    setCurrentId(id);
                    refresh();
                });
}

void ProjectController::updateProject(const QString &id, const QString &name,
                                      const QString &description)
{
    if (id.isEmpty())
        return;
    QJsonObject body{{QStringLiteral("name"), name},
                     {QStringLiteral("description"), description}};
    m_api->patch(QStringLiteral("/projects/") + id, body,
                 [this](bool ok, int status, const QJsonDocument &) {
                     if (!ok) {
                         setStatus(status == 403
                                       ? tr("You don't have permission to "
                                            "edit this project.")
                                       : tr("Save failed (HTTP %1)").arg(status));
                         return;
                     }
                     setStatus(tr("Project updated."));
                     refresh();
                 });
}

int ProjectController::unsyncedCount(const QString &id) const
{
    if (id.isEmpty() || !m_db)
        return 0;
    return int(m_db->dirtyObjects(id).size());
}

QString ProjectController::descriptionOf(const QString &id) const
{
    for (const ProjectRow &p : m_projects)
        if (p.id == id)
            return p.description;
    return {};
}

void ProjectController::deleteProject(const QString &id)
{
    if (id.isEmpty())
        return;
    m_api->del(QStringLiteral("/projects/") + id,
               [this, id](bool ok, int status, const QJsonDocument &) {
                   if (!ok) {
                       setStatus(status == 403
                                     ? tr("Only the project owner can delete "
                                          "it.")
                                     : tr("Delete failed (HTTP %1)").arg(status));
                       return;
                   }
                   // The server cascaded its side; drop ours too, or the
                   // rows sit in the local DB forever — unreachable
                   // (every query is scoped by the current project) yet
                   // still counted, indexed and searched.
                   if (m_db)
                       m_db->purgeProject(id);
                   if (m_currentId == id)
                       setCurrentId(QString());
                   setStatus(tr("Project deleted."));
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
