#include "LibraryAnalysisService.h"

#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LlmClient.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "StructuredLlm.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QHash>
#include <QSet>
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

int LibraryAnalysisService::digestCount() const
{
    return m_store->paperAnalyses(Analysis::KindQuick).size();
}

bool LibraryAnalysisService::canRun() const
{
    return m_settings && m_settings->isConfigured()
           && m_store->canWrite() && digestCount() >= 2 && m_call.isNull();
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
    if (m_call)
        return;
    if (!canRun()) {
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

void LibraryAnalysisService::run(
    const QString &kind, const QString &storeKind, const QJsonArray &briefs,
    const QJsonObject &extra,
    std::function<QJsonObject(const QJsonObject &)> postProcess)
{
    if (briefs.isEmpty()) {
        setError(tr("There is nothing to work from yet."));
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
    emit stateChanged();

    connect(m_call, &StructuredCall::succeeded, this,
            [this, storeKind, count, postProcess](const QJsonObject &raw) {
                m_call.clear();
                m_runningKind.clear();
                QJsonObject payload = postProcess ? postProcess(raw) : raw;
                if (payload.isEmpty()) {
                    emit stateChanged();
                    return;
                }
                payload.insert(QStringLiteral("generatedAt"),
                               QDateTime::currentDateTimeUtc().toString(
                                   Qt::ISODate));
                m_store->putLibraryAnalysis(
                    storeKind, QString(), payload,
                    m_settings->model(), inputHashNow(), count);
                emit resultChanged(storeKind);
                emit stateChanged();
            });
    connect(m_call, &StructuredCall::failed, this, [this](const QString &e) {
        m_call.clear();
        m_runningKind.clear();
        setError(e);
        emit stateChanged();
    });
}

void LibraryAnalysisService::cancel()
{
    if (!m_call)
        return;
    m_call->abort();
    m_call.clear();
    m_runningKind.clear();
    emit stateChanged();
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
    if (m_call)
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
