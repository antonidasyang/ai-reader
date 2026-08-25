#include "LibraryAnalysisService.h"

#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LlmClient.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "StructuredLlm.h"
#include "TaskManager.h"
#include "TaskTypes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QUuid>

namespace {

// Categories are matched across regenerations by a normalised form of the
// name they were born with, not by the name they currently carry -- so a
// category the reader renamed still recognises itself next time.
QString normKey(const QString &dimension, const QString &name)
{
    QString k;
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            k.append(c.toLower());
    }
    return dimension + QChar('|') + k;
}

QString newCategoryId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

QString sha1(const QString &s)
{
    return QString::fromLatin1(
               QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1)
                   .toHex())
        .left(16);
}

} // namespace

LibraryAnalysisService::LibraryAnalysisService(
    Settings *settings, AnalysisStore *store, ProjectController *projects,
    ProjectProfileController *profile, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_projects(projects)
    , m_profile(profile)
    , m_clients(settings, this)
{
    connect(m_store, &AnalysisStore::changed, this,
            &LibraryAnalysisService::stateChanged);
    connect(m_projects, &ProjectController::currentChanged, this,
            &LibraryAnalysisService::stateChanged);
}

void LibraryAnalysisService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A task stopped while it was still queued never calls its stop callback
    // -- there was nothing to stop -- so this is the only word the service
    // gets that an id it is holding is dead, and a kind left in the table
    // would look busy to everything that asks.
    connect(m_tasks, &TaskManager::taskFinished, this,
            [this](const QString &id, bool, const QString &) {
                if (id.isEmpty())
                    return;
                for (auto it = m_taskIds.begin(); it != m_taskIds.end(); ++it) {
                    if (it.value() != id)
                        continue;
                    const QString kind = it.key();
                    m_taskIds.erase(it);
                    // Whatever it was waiting for goes with it.
                    m_queue.removeAll(kind);
                    for (int i = m_deferred.size() - 1; i >= 0; --i) {
                        if (m_deferred.at(i).kind == kind)
                            m_deferred.removeAt(i);
                    }
                    emit stateChanged();
                    return;
                }
            });

    // Everything here is built out of the digests in the project, so a task
    // from the last session only means anything if the reader is still in
    // that project; the kind is the whole of the rest of the payload.
    m_tasks->registerResumer(
        Tasks::Kind::LibraryAnalysis, [this](const QJsonObject &resume) {
            if (resume.value(QStringLiteral("projectId")).toString()
                != m_store->projectId())
                return false;
            const QString kind = resume.value(QStringLiteral("kind")).toString();
            if (kind.isEmpty() || !canSubmit())
                return false;
            // This question is already being asked. generate() would be
            // refused by the exclusion key and leave the running task's id
            // in the table, so answering from it would claim a run this one
            // never started.
            if (m_taskIds.contains(kind))
                return false;
            if (kind == QLatin1String("classify"))
                classifyNewPapers();
            else
                generate(kind);
            return m_taskIds.contains(kind);
        });
}

int LibraryAnalysisService::digestCount() const
{
    return m_store->paperAnalyses(Analysis::KindQuick).size();
}

bool LibraryAnalysisService::canSubmit() const
{
    return m_settings && m_settings->isConfigured()
           && m_store->canWrite() && digestCount() >= 2;
}

bool LibraryAnalysisService::canRun() const
{
    return canSubmit() && m_call.isNull();
}

QString LibraryAnalysisService::titleOf(const QString &kind) const
{
    return Analysis::libraryKindTitle(kind);
}

QJsonArray LibraryAnalysisService::briefs() const
{
    QJsonArray out;
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick)) {
        if (r.payload.isEmpty())
            continue;
        out.append(AnalysisPrompts::digestBrief(r.paperId, r.title, r.payload));
    }
    return out;
}

QString LibraryAnalysisService::inputHashNow() const
{
    // The set of interpretations this rests on: add a paper, or re-interpret
    // one, and everything derived from them is out of date.
    QStringList parts;
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick))
        parts.append(r.paperId + QChar(':') + r.updatedAt);
    parts.sort();
    return Analysis::inputHash(sha1(parts.join(QChar(','))),
                               AnalysisPrompts::promptVersion(),
                               m_profile->hash(),
                               m_settings ? m_settings->model()
                                          : QString());
}

QVariantMap LibraryAnalysisService::result(const QString &kind) const
{
    return m_store->libraryAnalysis(kind).payload.toVariantMap();
}

bool LibraryAnalysisService::has(const QString &kind) const
{
    return !m_store->libraryAnalysis(kind).payload.isEmpty();
}

QString LibraryAnalysisService::authorOf(const QString &kind) const
{
    return m_store->libraryAnalysis(kind).authorEmail;
}

QString LibraryAnalysisService::updatedAtOf(const QString &kind) const
{
    return m_store->libraryAnalysis(kind).updatedAt;
}

int LibraryAnalysisService::paperCountOf(const QString &kind) const
{
    return m_store->libraryAnalysis(kind).paperCount;
}

bool LibraryAnalysisService::isStale(const QString &kind) const
{
    const AnalysisRecord rec = m_store->libraryAnalysis(kind);
    if (!rec.valid || rec.payload.isEmpty() || rec.inputHash.isEmpty())
        return false;
    return rec.inputHash != inputHashNow();
}

QVariantList LibraryAnalysisService::history(const QString &kind) const
{
    QVariantList out;
    for (const QJsonValue &v : m_store->libraryHistoryIndex(kind))
        out.append(v.toObject().toVariantMap());
    return out;
}

bool LibraryAnalysisService::restoreVersion(const QString &kind, int index)
{
    return m_store->restoreLibraryVersion(kind, QString(), index);
}

QString LibraryAnalysisService::paperTitle(const QString &paperId) const
{
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick)) {
        if (r.paperId == paperId)
            return r.title.isEmpty() ? paperId : r.title;
    }
    return paperId;
}

// ── running one ──────────────────────────────────────────────────────

void LibraryAnalysisService::generate(const QString &kind)
{
    // One call at a time is all this service can do on its own; with a
    // manager the task is submitted now and started when the queue reaches
    // it, so a call in flight is no reason to refuse.
    if (m_call && !m_tasks)
        return;
    if (!canSubmit()) {
        setError(digestCount() < 2
                     ? tr("Interpret at least two papers first — everything "
                          "here is built out of those interpretations.")
                     : tr("No model is configured, or this project is "
                          "read-only."));
        return;
    }
    QJsonObject extra;
    if (kind == Analysis::KindTaxonomy) {
        // Hand the model what the reader has already settled on, so a
        // regeneration extends the system instead of redrawing it.
        const QJsonObject tax = taxonomy();
        if (!tax.isEmpty()) {
            QJsonArray keep;
            for (const QJsonValue &dv : tax.value(QStringLiteral("dimensions"))
                                            .toArray()) {
                const QJsonObject d = dv.toObject();
                for (const QJsonValue &cv :
                     d.value(QStringLiteral("categories")).toArray()) {
                    const QJsonObject c = cv.toObject();
                    if (!c.value(QStringLiteral("locked")).toBool()
                        && c.value(QStringLiteral("source")).toString()
                               != QLatin1String("user"))
                        continue;
                    keep.append(QJsonObject{
                        {QStringLiteral("dimension"),
                         d.value(QStringLiteral("dimension"))},
                        {QStringLiteral("name"), c.value(QStringLiteral("name"))},
                        {QStringLiteral("description"),
                         c.value(QStringLiteral("description"))}});
                }
            }
            if (!keep.isEmpty()) {
                extra.insert(QStringLiteral("categoriesTheReaderHasFixed"), keep);
                extra.insert(
                    QStringLiteral("instruction"),
                    QStringLiteral("Keep these categories exactly as they are, "
                                   "under the same names, and fit the papers "
                                   "around them."));
            }
        }
    }
    if (kind == Analysis::KindTaxonomy) {
        run(kind, kind, briefs(), extra,
            [this](const QJsonObject &fresh) { return mergeTaxonomy(fresh); });
        return;
    }
    run(kind, kind, briefs(), extra, nullptr);
}

void LibraryAnalysisService::run(const QString &kind, const QString &storeKind,
                                 const QJsonArray &briefs,
                                 const QJsonObject &extra,
                                 PostProcess postProcess)
{
    if (briefs.isEmpty()) {
        setError(tr("There is nothing to work from yet."));
        finishTaskFor(kind, false, m_lastError);
        return;
    }
    if (!m_tasks) {
        startCall(kind, storeKind, briefs, extra, postProcess);
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::LibraryAnalysis;
    req.title = taskTitleFor(kind);
    req.projectId = m_store->projectId();
    // Project work, not one paper's: the key names the project and the
    // question, so the seven kinds queue beside each other but no kind is
    // ever asked twice at once.
    req.exclusiveKey = QStringLiteral("library_analysis|") + req.projectId
                       + QChar('|') + kind;
    req.steps = 1;
    req.resume = QJsonObject{{QStringLiteral("projectId"), req.projectId},
                             {QStringLiteral("kind"), kind}};

    const QString project = req.projectId;
    const QString id = m_tasks->submit(
        req,
        // submit() may call this before it returns; the hop through the
        // event loop keeps the id ahead of anything that reports against it,
        // and is where a task that waited out a project switch is dropped.
        [this, kind, storeKind, briefs, extra, postProcess, project] {
            QTimer::singleShot(0, this,
                               [this, kind, storeKind, briefs, extra,
                                postProcess, project] {
                                   if (!m_taskIds.contains(kind))
                                       return;   // cancelled while it waited
                                   if (project != m_store->projectId()) {
                                       // The reader is somewhere else now;
                                       // nothing went wrong here.
                                       cancelTaskFor(kind);
                                       return;
                                   }
                                   startCall(kind, storeKind, briefs, extra,
                                             postProcess);
                               });
        },
        [this, kind] { cancelKind(kind); });
    if (id.isEmpty())
        return;                    // this question is already being asked
    m_taskIds.insert(kind, id);
    emit stateChanged();
}

void LibraryAnalysisService::startCall(const QString &kind,
                                       const QString &storeKind,
                                       const QJsonArray &briefs,
                                       const QJsonObject &extra,
                                       PostProcess postProcess)
{
    if (m_call) {
        // The manager may admit two of these at once; a StructuredCall is
        // not shareable, so the second waits here instead of trampling the
        // first. Its task is running either way -- what it is waiting for is
        // the model, which is what a task queue is about.
        m_deferred.append(QueuedRun{kind, storeKind, briefs, extra, postProcess});
        emit stateChanged();
        return;
    }
    // Rebuilt when the model configuration moved (one project-wide analysis runs at a time).
    m_client = m_clients.client(m_call.isNull());

    StructuredCall::Request req;
    req.system = AnalysisPrompts::librarySystem(
        kind, AnalysisPrompts::languageName(m_settings->targetLang()),
        m_profile->promptBlock());
    req.user = AnalysisPrompts::libraryUser(kind, briefs, extra);
    req.schema = AnalysisPrompts::librarySchema(kind);
    req.toolName = QStringLiteral("emit_analysis");
    req.toolDescription = QStringLiteral("Return the analysis.");
    req.maxTokens = m_settings->analysisMaxTokens();
    req.temperature = 0.15;

    setError(QString());
    m_runningKind = kind;
    const int count = briefs.size();
    m_call = StructuredCall::start(m_client, req, this);
    if (m_tasks && m_taskIds.contains(kind)) {
        m_tasks->setProgress(m_taskIds.value(kind), 0, 1);
        m_tasks->setNote(m_taskIds.value(kind),
                         tr("reading %1 interpretations").arg(count));
    }
    emit stateChanged();

    connect(m_call, &StructuredCall::succeeded, this,
            [this, kind, storeKind, count, postProcess](const QJsonObject &raw) {
                m_call.clear();
                m_runningKind.clear();
                QJsonObject payload = postProcess ? postProcess(raw) : raw;
                if (payload.isEmpty()) {
                    // Nothing usable came back. It is not an error the model
                    // reported, but it is not a result either, and the six
                    // behind it should not wait on it.
                    finishTaskFor(kind, false,
                                  tr("The model returned nothing usable."));
                    emit stateChanged();
                    runNextQueued();
                    return;
                }
                payload.insert(QStringLiteral("generatedAt"),
                               QDateTime::currentDateTimeUtc().toString(
                                   Qt::ISODate));
                m_store->putLibraryAnalysis(
                    storeKind, QString(), payload,
                    m_settings->model(), inputHashNow(), count);
                finishTaskFor(kind, true);
                emit resultChanged(storeKind);
                emit stateChanged();
                runNextQueued();
            });
    connect(m_call, &StructuredCall::failed, this,
            [this, kind](const QString &e) {
                m_call.clear();
                m_runningKind.clear();
                setError(e);
                finishTaskFor(kind, false, e);
                emit stateChanged();
                // One kind failing is not a reason to abandon the other six.
                runNextQueued();
            });
}

QString LibraryAnalysisService::taskTitleFor(const QString &kind) const
{
    // Placing new papers is not one of the seven questions; it borrows the
    // category system's schema, so titleOf() has nothing to say about it.
    if (kind == QLatin1String("classify"))
        return tr("Place new papers");
    return titleOf(kind);
}

void LibraryAnalysisService::finishTaskFor(const QString &kind, bool ok,
                                           const QString &error)
{
    if (!m_tasks)
        return;
    // Taken out first: a cancel and an answer can arrive for the same kind.
    const QString id = m_taskIds.take(kind);
    if (id.isEmpty())
        return;
    m_tasks->finish(id, ok, error);
}

void LibraryAnalysisService::cancelTaskFor(const QString &kind)
{
    if (!m_tasks)
        return;
    const QString id = m_taskIds.take(kind);
    if (id.isEmpty())
        return;
    m_tasks->markCanceled(id);
}

void LibraryAnalysisService::cancelAllTasks()
{
    if (!m_tasks) {
        m_taskIds.clear();
        return;
    }
    const QHash<QString, QString> ids = m_taskIds;
    m_taskIds.clear();
    for (auto it = ids.constBegin(); it != ids.constEnd(); ++it)
        m_tasks->markCanceled(it.value());
}

void LibraryAnalysisService::generateAll()
{
    // The whole set, in the order a reader would want to read it: how the
    // papers group, then the map, then where they agree and disagree, how it
    // moved, what is missing, what that opens up, and what to do next.
    const QStringList all{Analysis::KindTaxonomy,  Analysis::KindMap,
                          Analysis::KindConsensus, Analysis::KindEvolution,
                          Analysis::KindCoverage,  Analysis::KindOpportunities,
                          Analysis::KindActions};
    if (m_tasks) {
        // Seven tasks, submitted in reading order. The manager's queue is
        // what paces them now -- and what the reader can watch and stop one
        // of, which a list private to this service never was.
        for (const QString &kind : all)
            generate(kind);
        emit stateChanged();
        return;
    }
    m_queue = all;
    if (!m_call)
        runNextQueued();
    emit stateChanged();
}

void LibraryAnalysisService::runNextQueued()
{
    if (m_call)
        return;
    if (!m_deferred.isEmpty()) {
        // A run the manager already admitted: its task is running, so it
        // goes straight to the model rather than being submitted again.
        const QueuedRun next = m_deferred.takeFirst();
        startCall(next.kind, next.storeKind, next.briefs, next.extra,
                  next.postProcess);
        return;
    }
    if (m_queue.isEmpty())
        return;
    const QString kind = m_queue.takeFirst();
    generate(kind);
    if (!m_call && !m_queue.isEmpty()) {
        // generate() refused this one (nothing to work from, or it is
        // already running); do not stall the rest behind it.
        runNextQueued();
    }
}

void LibraryAnalysisService::cancel()
{
    m_queue.clear();
    m_deferred.clear();
    // Stopping everything means every task, not only the one at the model:
    // the ones still waiting would otherwise sit in the viewer forever. The
    // reader stopped them, so they end Canceled -- a Failed row with no
    // reason under it says nothing about what happened.
    cancelAllTasks();
    if (m_call) {
        m_call->abort();
        m_call.clear();
        m_runningKind.clear();
    }
    emit stateChanged();
}

void LibraryAnalysisService::cancelKind(const QString &kind)
{
    // What the viewer's stop button means for one row: drop it if it is
    // waiting, abort it if it is the one talking to the model, and leave the
    // other six alone.
    m_queue.removeAll(kind);
    for (int i = m_deferred.size() - 1; i >= 0; --i) {
        if (m_deferred.at(i).kind == kind)
            m_deferred.removeAt(i);
    }
    if (m_call && m_runningKind == kind) {
        m_call->abort();
        m_call.clear();
        m_runningKind.clear();
    }
    cancelTaskFor(kind);
    emit stateChanged();
    runNextQueued();
}

void LibraryAnalysisService::setError(const QString &e)
{
    if (e == m_lastError)
        return;
    m_lastError = e;
    emit stateChanged();
}

// ── the category system (§8) ─────────────────────────────────────────

QJsonObject LibraryAnalysisService::taxonomy() const
{
    return m_store->libraryAnalysis(Analysis::KindTaxonomy).payload;
}

void LibraryAnalysisService::saveTaxonomy(const QJsonObject &tax)
{
    const AnalysisRecord cur =
        m_store->libraryAnalysis(Analysis::KindTaxonomy);
    m_store->putLibraryAnalysis(
        Analysis::KindTaxonomy, QString(), tax,
        m_settings ? m_settings->model() : QString(),
        cur.inputHash.isEmpty() ? inputHashNow() : cur.inputHash,
        digestCount());
    emit resultChanged(Analysis::KindTaxonomy);
}

QJsonObject LibraryAnalysisService::mergeTaxonomy(const QJsonObject &fresh) const
{
    const QJsonObject old = taxonomy();

    // Everything the reader has touched, by the key it was born with.
    QHash<QString, QJsonObject> keptByKey;
    QHash<QString, QString> keptDimension;
    for (const QJsonValue &dv : old.value(QStringLiteral("dimensions")).toArray()) {
        const QJsonObject d = dv.toObject();
        const QString dim = d.value(QStringLiteral("dimension")).toString();
        for (const QJsonValue &cv : d.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject c = cv.toObject();
            const QString key = c.value(QStringLiteral("key")).toString();
            keptByKey.insert(key, c);
            keptDimension.insert(key, dim);
        }
    }

    QJsonArray outDims;
    QSet<QString> used;
    for (const QJsonValue &dv : fresh.value(QStringLiteral("dimensions")).toArray()) {
        const QJsonObject d = dv.toObject();
        const QString dim = d.value(QStringLiteral("dimension")).toString();
        QJsonArray cats;
        for (const QJsonValue &cv : d.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject c = cv.toObject();
            const QString name = c.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            const QString key = normKey(dim, name);
            used.insert(key);
            const auto it = keptByKey.constFind(key);
            if (it != keptByKey.constEnd()) {
                QJsonObject merged = it.value();
                // A locked category is the reader's; nothing about it moves.
                if (!merged.value(QStringLiteral("locked")).toBool()) {
                    merged.insert(QStringLiteral("description"),
                                  c.value(QStringLiteral("description")));
                    merged.insert(QStringLiteral("paperIds"),
                                  c.value(QStringLiteral("paperIds")));
                }
                cats.append(merged);
                continue;
            }
            cats.append(QJsonObject{
                {QStringLiteral("id"), newCategoryId()},
                {QStringLiteral("key"), key},
                {QStringLiteral("name"), name},
                {QStringLiteral("description"),
                 c.value(QStringLiteral("description"))},
                {QStringLiteral("paperIds"), c.value(QStringLiteral("paperIds"))},
                {QStringLiteral("locked"), false},
                {QStringLiteral("confirmed"), false},
                {QStringLiteral("source"), QStringLiteral("ai")}});
        }
        if (!cats.isEmpty())
            outDims.append(QJsonObject{{QStringLiteral("dimension"), dim},
                                       {QStringLiteral("categories"), cats}});
    }

    // Anything the reader locked or made themselves survives a regeneration
    // even when the model forgot about it.
    for (auto it = keptByKey.constBegin(); it != keptByKey.constEnd(); ++it) {
        if (used.contains(it.key()))
            continue;
        const QJsonObject c = it.value();
        if (!c.value(QStringLiteral("locked")).toBool()
            && c.value(QStringLiteral("source")).toString() != QLatin1String("user"))
            continue;
        const QString dim = keptDimension.value(it.key());
        bool placed = false;
        for (int i = 0; i < outDims.size(); ++i) {
            QJsonObject d = outDims.at(i).toObject();
            if (d.value(QStringLiteral("dimension")).toString() != dim)
                continue;
            QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
            cats.append(c);
            d.insert(QStringLiteral("categories"), cats);
            outDims.replace(i, d);
            placed = true;
            break;
        }
        if (!placed) {
            outDims.append(QJsonObject{
                {QStringLiteral("dimension"), dim},
                {QStringLiteral("categories"), QJsonArray{c}}});
        }
    }

    QJsonObject out = fresh;
    out.insert(QStringLiteral("dimensions"), outDims);
    out.insert(QStringLiteral("generatedAt"),
               QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return out;
}

void LibraryAnalysisService::renameCategory(const QString &categoryId,
                                            const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            c.insert(QStringLiteral("name"), name.trimmed());
            c.insert(QStringLiteral("confirmed"), true);
            cats.replace(j, c);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

void LibraryAnalysisService::setCategoryLocked(const QString &categoryId,
                                               bool locked)
{
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            c.insert(QStringLiteral("locked"), locked);
            if (locked)
                c.insert(QStringLiteral("confirmed"), true);
            cats.replace(j, c);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

void LibraryAnalysisService::setCategoryConfirmed(const QString &categoryId,
                                                  bool confirmed)
{
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            c.insert(QStringLiteral("confirmed"), confirmed);
            cats.replace(j, c);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

void LibraryAnalysisService::mergeCategories(const QString &intoId,
                                             const QString &fromId)
{
    if (intoId == fromId)
        return;
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    QJsonArray moved;
    // Take the papers out of the source category and drop it.
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            const QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != fromId)
                continue;
            moved = c.value(QStringLiteral("paperIds")).toArray();
            cats.removeAt(j);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            break;
        }
    }
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != intoId)
                continue;
            QJsonArray ids = c.value(QStringLiteral("paperIds")).toArray();
            QSet<QString> have;
            for (const QJsonValue &v : ids)
                have.insert(v.toString());
            for (const QJsonValue &v : moved) {
                if (!have.contains(v.toString()))
                    ids.append(v);
            }
            c.insert(QStringLiteral("paperIds"), ids);
            c.insert(QStringLiteral("confirmed"), true);
            cats.replace(j, c);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            break;
        }
    }
    tax.insert(QStringLiteral("dimensions"), dims);
    saveTaxonomy(tax);
}

void LibraryAnalysisService::splitCategory(const QString &categoryId,
                                           const QString &newName,
                                           const QStringList &paperIds)
{
    if (newName.trimmed().isEmpty() || paperIds.isEmpty())
        return;
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        const QString dim = d.value(QStringLiteral("dimension")).toString();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            QJsonArray keep;
            QJsonArray moved;
            for (const QJsonValue &v : c.value(QStringLiteral("paperIds")).toArray()) {
                if (paperIds.contains(v.toString()))
                    moved.append(v);
                else
                    keep.append(v);
            }
            if (moved.isEmpty())
                return;
            c.insert(QStringLiteral("paperIds"), keep);
            c.insert(QStringLiteral("confirmed"), true);
            cats.replace(j, c);
            cats.insert(j + 1,
                        QJsonObject{
                            {QStringLiteral("id"), newCategoryId()},
                            {QStringLiteral("key"), normKey(dim, newName.trimmed())},
                            {QStringLiteral("name"), newName.trimmed()},
                            {QStringLiteral("description"),
                             c.value(QStringLiteral("description"))},
                            {QStringLiteral("paperIds"), moved},
                            {QStringLiteral("locked"), true},
                            {QStringLiteral("confirmed"), true},
                            {QStringLiteral("source"), QStringLiteral("user")}});
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

QVariantList LibraryAnalysisService::categoryPapers(const QString &categoryId) const
{
    QVariantList out;
    for (const QJsonValue &dv :
         taxonomy().value(QStringLiteral("dimensions")).toArray()) {
        for (const QJsonValue &cv :
             dv.toObject().value(QStringLiteral("categories")).toArray()) {
            const QJsonObject c = cv.toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            for (const QJsonValue &p : c.value(QStringLiteral("paperIds")).toArray()) {
                const QString id = p.toString();
                out.append(QVariantMap{{QStringLiteral("paperId"), id},
                                       {QStringLiteral("title"), paperTitle(id)}});
            }
            return out;
        }
    }
    return out;
}

void LibraryAnalysisService::addCategory(const QString &dimension,
                                         const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    const QJsonObject cat{
        {QStringLiteral("id"), newCategoryId()},
        {QStringLiteral("key"), normKey(dimension, name.trimmed())},
        {QStringLiteral("name"), name.trimmed()},
        {QStringLiteral("description"), QString()},
        {QStringLiteral("paperIds"), QJsonArray{}},
        {QStringLiteral("locked"), true},
        {QStringLiteral("confirmed"), true},
        {QStringLiteral("source"), QStringLiteral("user")}};
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        if (d.value(QStringLiteral("dimension")).toString() != dimension)
            continue;
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        cats.append(cat);
        d.insert(QStringLiteral("categories"), cats);
        dims.replace(i, d);
        tax.insert(QStringLiteral("dimensions"), dims);
        saveTaxonomy(tax);
        return;
    }
    dims.append(QJsonObject{{QStringLiteral("dimension"), dimension},
                            {QStringLiteral("categories"), QJsonArray{cat}}});
    tax.insert(QStringLiteral("dimensions"), dims);
    saveTaxonomy(tax);
}

void LibraryAnalysisService::removeCategory(const QString &categoryId)
{
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            if (cats.at(j).toObject().value(QStringLiteral("id")).toString()
                != categoryId)
                continue;
            cats.removeAt(j);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

void LibraryAnalysisService::assignPaper(const QString &paperId,
                                         const QString &categoryId, bool on)
{
    QJsonObject tax = taxonomy();
    QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
    for (int i = 0; i < dims.size(); ++i) {
        QJsonObject d = dims.at(i).toObject();
        QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
        for (int j = 0; j < cats.size(); ++j) {
            QJsonObject c = cats.at(j).toObject();
            if (c.value(QStringLiteral("id")).toString() != categoryId)
                continue;
            QJsonArray ids = c.value(QStringLiteral("paperIds")).toArray();
            int at = -1;
            for (int k = 0; k < ids.size(); ++k) {
                if (ids.at(k).toString() == paperId) {
                    at = k;
                    break;
                }
            }
            if (on && at < 0)
                ids.append(paperId);
            else if (!on && at >= 0)
                ids.removeAt(at);
            else
                return;
            c.insert(QStringLiteral("paperIds"), ids);
            c.insert(QStringLiteral("confirmed"), true);
            cats.replace(j, c);
            d.insert(QStringLiteral("categories"), cats);
            dims.replace(i, d);
            tax.insert(QStringLiteral("dimensions"), dims);
            saveTaxonomy(tax);
            return;
        }
    }
}

QStringList LibraryAnalysisService::unclassifiedPapers() const
{
    QSet<QString> placed;
    for (const QJsonValue &dv :
         taxonomy().value(QStringLiteral("dimensions")).toArray()) {
        for (const QJsonValue &cv :
             dv.toObject().value(QStringLiteral("categories")).toArray()) {
            for (const QJsonValue &p :
                 cv.toObject().value(QStringLiteral("paperIds")).toArray())
                placed.insert(p.toString());
        }
    }
    QStringList out;
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick)) {
        if (!placed.contains(r.paperId))
            out.append(r.paperId);
    }
    return out;
}

void LibraryAnalysisService::classifyNewPapers()
{
    // As in generate(): with a manager this becomes a task and waits its
    // turn; without one it can only go when nothing else is running.
    if (m_call && !m_tasks)
        return;
    const QJsonObject tax = taxonomy();
    if (tax.isEmpty()) {
        setError(tr("Generate the category system first."));
        return;
    }
    const QStringList fresh = unclassifiedPapers();
    if (fresh.isEmpty()) {
        setError(tr("Every interpreted paper is already placed."));
        return;
    }

    // Only the categories the reader has confirmed count as the system to
    // place new papers into (§8.4); an unreviewed suggestion is not yet a
    // decision.
    QJsonArray categories;
    bool anyConfirmed = false;
    for (const QJsonValue &dv : tax.value(QStringLiteral("dimensions")).toArray()) {
        const QJsonObject d = dv.toObject();
        for (const QJsonValue &cv : d.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject c = cv.toObject();
            if (c.value(QStringLiteral("confirmed")).toBool())
                anyConfirmed = true;
        }
    }
    for (const QJsonValue &dv : tax.value(QStringLiteral("dimensions")).toArray()) {
        const QJsonObject d = dv.toObject();
        for (const QJsonValue &cv : d.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject c = cv.toObject();
            if (anyConfirmed && !c.value(QStringLiteral("confirmed")).toBool())
                continue;
            categories.append(QJsonObject{
                {QStringLiteral("id"), c.value(QStringLiteral("id"))},
                {QStringLiteral("dimension"), d.value(QStringLiteral("dimension"))},
                {QStringLiteral("name"), c.value(QStringLiteral("name"))},
                {QStringLiteral("description"),
                 c.value(QStringLiteral("description"))}});
        }
    }
    if (categories.isEmpty()) {
        setError(tr("No categories to place them into yet."));
        return;
    }

    QJsonArray newBriefs;
    for (const AnalysisRecord &r : m_store->paperAnalyses(Analysis::KindQuick)) {
        if (!fresh.contains(r.paperId))
            continue;
        newBriefs.append(
            AnalysisPrompts::digestBrief(r.paperId, r.title, r.payload));
    }

    run(QStringLiteral("classify"), Analysis::KindTaxonomy, newBriefs,
        QJsonObject{{QStringLiteral("existingCategories"), categories}},
        [this](const QJsonObject &raw) {
            QJsonObject tax = taxonomy();
            QJsonArray dims = tax.value(QStringLiteral("dimensions")).toArray();
            QJsonArray ambiguous = tax.value(QStringLiteral("ambiguous")).toArray();

            for (const QJsonValue &av :
                 raw.value(QStringLiteral("assignments")).toArray()) {
                const QJsonObject a = av.toObject();
                const QString paperId =
                    a.value(QStringLiteral("paperId")).toString();
                if (paperId.isEmpty())
                    continue;
                if (a.value(QStringLiteral("ambiguous")).toBool()) {
                    ambiguous.append(QJsonObject{
                        {QStringLiteral("paperId"), paperId},
                        {QStringLiteral("note"), a.value(QStringLiteral("note"))}});
                }
                QSet<QString> want;
                for (const QJsonValue &cv :
                     a.value(QStringLiteral("categoryIds")).toArray())
                    want.insert(cv.toString());
                for (int i = 0; i < dims.size(); ++i) {
                    QJsonObject d = dims.at(i).toObject();
                    QJsonArray cats = d.value(QStringLiteral("categories")).toArray();
                    bool touched = false;
                    for (int j = 0; j < cats.size(); ++j) {
                        QJsonObject c = cats.at(j).toObject();
                        if (!want.contains(c.value(QStringLiteral("id")).toString()))
                            continue;
                        QJsonArray ids = c.value(QStringLiteral("paperIds")).toArray();
                        bool have = false;
                        for (const QJsonValue &v : ids)
                            have = have || v.toString() == paperId;
                        if (!have) {
                            ids.append(paperId);
                            c.insert(QStringLiteral("paperIds"), ids);
                            cats.replace(j, c);
                            touched = true;
                        }
                    }
                    if (touched) {
                        d.insert(QStringLiteral("categories"), cats);
                        dims.replace(i, d);
                    }
                }
            }
            tax.insert(QStringLiteral("dimensions"), dims);
            tax.insert(QStringLiteral("ambiguous"), ambiguous);
            return tax;
        });
}
