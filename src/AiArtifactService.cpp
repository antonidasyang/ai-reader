#include "AiArtifactService.h"
#include "AuthController.h"
#include "PaperController.h"
#include "ProjectController.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QJsonObject>
#include <QUuid>
#include <QVariantMap>

namespace {
// Fixed namespace so the same (project, paper, type, author) always maps to the
// same artifact id (each member keeps one artifact per paper per type).
const QUuid kNs =
    QUuid::fromString(QStringLiteral("{4a1f2e90-7b3c-4d6a-9f21-a1b2c3d40001}"));
} // namespace

AiArtifactService::AiArtifactService(LibraryDb *db, ProjectController *projects,
                                     SyncEngine *sync, AuthController *auth,
                                     PaperController *paper, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_projects(projects)
    , m_sync(sync)
    , m_auth(auth)
    , m_paper(paper)
{
    connect(m_sync, &SyncEngine::projectSynced, this,
            [this](const QString &) { recompute(); });
    connect(m_projects, &ProjectController::currentChanged, this, [this] {
        emit contextChanged();
        recompute();
    });
    connect(m_paper, &PaperController::pdfSourceChanged, this, [this] {
        emit contextChanged();
        recompute();
    });
    connect(m_paper, &PaperController::blocksChanged, this, [this] {
        emit contextChanged(); // paperId resolves once blocks are ready
        recompute();
    });
    connect(m_auth, &AuthController::authenticatedChanged, this,
            [this] { emit contextChanged(); });
}

QString AiArtifactService::currentPaperId() const
{
    return m_paper->paperId();
}

bool AiArtifactService::canShare() const
{
    return m_auth->authenticated() && m_projects->canWrite()
           && !currentPaperId().isEmpty();
}

void AiArtifactService::publish(const QString &type, const QString &payload,
                               const QString &model)
{
    if (!canShare() || payload.trimmed().isEmpty())
        return;
    const QString projectId = m_projects->currentId();
    const QString paperId = currentPaperId();
    const QString authorId = m_auth->userId();
    const QString name = projectId + QChar('|') + paperId + QChar('|') + type
                         + QChar('|') + authorId;
    const QString id =
        QUuid::createUuidV5(kNs, name.toUtf8()).toString(QUuid::WithoutBraces);

    QJsonObject data{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("type"), type},
        {QStringLiteral("author"), authorId},
        {QStringLiteral("authorEmail"), m_auth->userEmail()},
        {QStringLiteral("model"), model},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("updatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_sync->putObject(QStringLiteral("ai_artifact"), id, data);
    recompute();
}

QVariantList AiArtifactService::sharedForCurrent() const
{
    QVariantList out;
    const QString projectId = m_projects->currentId();
    const QString paperId = currentPaperId();
    if (projectId.isEmpty() || paperId.isEmpty())
        return out;
    const QList<SyncObjectRow> rows =
        m_db->objectsByType(projectId, QStringLiteral("ai_artifact"));
    for (const SyncObjectRow &r : rows) {
        if (r.data.value(QStringLiteral("paperId")).toString() != paperId)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("author"),
                 r.data.value(QStringLiteral("author")).toString());
        m.insert(QStringLiteral("authorEmail"),
                 r.data.value(QStringLiteral("authorEmail")).toString());
        m.insert(QStringLiteral("type"),
                 r.data.value(QStringLiteral("type")).toString());
        m.insert(QStringLiteral("model"),
                 r.data.value(QStringLiteral("model")).toString());
        m.insert(QStringLiteral("payload"),
                 r.data.value(QStringLiteral("payload")).toString());
        m.insert(QStringLiteral("isMine"),
                 r.data.value(QStringLiteral("author")).toString()
                     == m_auth->userId());
        out.append(m);
    }
    return out;
}

void AiArtifactService::recompute()
{
    const int n = sharedForCurrent().size();
    if (n != m_sharedCount) {
        m_sharedCount = n;
        emit sharedCountChanged();
    }
}
