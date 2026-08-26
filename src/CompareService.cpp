#include "CompareService.h"

#include "AnalysisPrompts.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LlmClient.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "StructuredLlm.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QVariantMap>

CompareService::CompareService(Settings *settings, AnalysisStore *store,
                               ProjectController *projects,
                               ProjectProfileController *profile,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_projects(projects)
    , m_profile(profile)
    , m_clients(settings, this)
{
    connect(m_projects, &ProjectController::currentChanged, this, [this]() {
        m_basket.clear();
        m_result = QJsonObject();
        load();
        emit basketChanged();
        emit resultChanged();
        emit stateChanged();
    });
    connect(m_store, &AnalysisStore::changed, this, [this]() {
        // A basket filled on another machine lands here with the sync.
        const int before = m_basket.size();
        load();
        if (m_basket.size() != before)
            emit basketChanged();
        emit stateChanged();
    });
    load();
}

// ── the basket ───────────────────────────────────────────────────────

QVariantList CompareService::basket() const
{
    QVariantList out;
    for (const Entry &e : m_basket) {
        out.append(QVariantMap{{QStringLiteral("paperId"), e.paperId},
                               {QStringLiteral("title"), e.title},
                               {QStringLiteral("notes"), e.notes}});
    }
    return out;
}

void CompareService::add(const QString &paperId, const QString &title,
                         const QString &note)
{
    if (paperId.isEmpty())
        return;
    for (Entry &e : m_basket) {
        if (e.paperId != paperId)
            continue;
        if (!note.trimmed().isEmpty() && !e.notes.contains(note.trimmed()))
            e.notes.append(note.trimmed());
        save();
        emit basketChanged();
        return;
    }
    Entry e;
    e.paperId = paperId;
    e.title = title;
    if (!note.trimmed().isEmpty())
        e.notes.append(note.trimmed());
    m_basket.append(e);
    save();
    emit basketChanged();
}

void CompareService::removePaper(const QString &paperId)
{
    for (int i = 0; i < m_basket.size(); ++i) {
        if (m_basket.at(i).paperId == paperId) {
            m_basket.removeAt(i);
            save();
            emit basketChanged();
            return;
        }
    }
}

bool CompareService::contains(const QString &paperId) const
{
    for (const Entry &e : m_basket) {
        if (e.paperId == paperId)
            return true;
    }
    return false;
}

void CompareService::clearBasket()
{
    if (m_basket.isEmpty())
        return;
    m_basket.clear();
    save();
    emit basketChanged();
}

QString CompareService::scopeKey() const
{
    QStringList ids;
    for (const Entry &e : m_basket)
        ids.append(e.paperId);
    return Analysis::scopeHash(ids);
}

void CompareService::load()
{
    m_basket.clear();
    const QString project = m_projects->currentId();
    if (project.isEmpty())
        return;

    QJsonArray arr = m_store->compareBasket();
    if (arr.isEmpty()) {
        // Migration: the basket used to live in this machine's settings file,
        // where it never reached the reader's other machines. Take it over
        // once, then let the synced object own it.
        const QByteArray raw =
            m_qs.value(QStringLiteral("compare/") + project).toByteArray();
        arr = QJsonDocument::fromJson(raw).array();
        if (!arr.isEmpty())
            m_store->putCompareBasket(arr);
        m_qs.remove(QStringLiteral("compare/") + project);
    }

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.paperId = o.value(QStringLiteral("paperId")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        for (const QJsonValue &n : o.value(QStringLiteral("notes")).toArray())
            e.notes.append(n.toString());
        if (!e.paperId.isEmpty())
            m_basket.append(e);
    }
    loadStored();
}

void CompareService::save()
{
    QJsonArray arr;
    for (const Entry &e : m_basket) {
        QJsonArray notes;
        for (const QString &n : e.notes)
            notes.append(n);
        arr.append(QJsonObject{{QStringLiteral("paperId"), e.paperId},
                               {QStringLiteral("title"), e.title},
                               {QStringLiteral("notes"), notes}});
    }
    m_store->putCompareBasket(arr);
}

// ── the comparison ───────────────────────────────────────────────────

bool CompareService::canRun() const
{
    if (!m_settings || !m_settings->isConfigured())
        return false;
    if (m_basket.size() < 2)
        return false;
    // Every paper in the basket needs an interpretation: the comparison is
    // built out of those, not out of the PDFs.
    for (const Entry &e : m_basket) {
        if (!m_store->paperAnalysis(e.paperId, Analysis::KindQuick).valid)
            return false;
    }
    return true;
}

void CompareService::loadStored()
{
    if (m_basket.size() < 2) {
        if (!m_result.isEmpty()) {
            m_result = QJsonObject();
            emit resultChanged();
        }
        return;
    }
    const AnalysisRecord rec =
        m_store->libraryAnalysis(Analysis::KindCompare, scopeKey());
    const QJsonObject payload = rec.valid ? rec.payload : QJsonObject();
    if (payload == m_result)
        return;
    m_result = payload;
    m_resultAuthor = rec.authorEmail;
    m_resultUpdatedAt = rec.updatedAt;
    emit resultChanged();
}

void CompareService::compare()
{
    if (m_call)
        return;
    if (!canRun()) {
        setError(m_basket.size() < 2
                     ? tr("Put at least two papers in the comparison.")
                     : tr("Every paper in the comparison needs an "
                          "interpretation first."));
        return;
    }
    setError(QString());

    QJsonArray digests;
    QStringList notes;
    for (const Entry &e : m_basket) {
        const AnalysisRecord rec =
            m_store->paperAnalysis(e.paperId, Analysis::KindQuick);
        const QString title =
            e.title.isEmpty() ? rec.title : e.title;
        digests.append(
            AnalysisPrompts::digestBrief(e.paperId, title, rec.payload));
        for (const QString &n : e.notes)
            notes.append(QStringLiteral("%1 — %2").arg(title, n));
    }

    // Rebuilt when the model configuration moved (a comparison is a single call).
    m_client = m_clients.client();

    StructuredCall::Request req;
    req.system = AnalysisPrompts::compareSystem(
        AnalysisPrompts::languageName(m_settings->targetLang()),
        m_profile->promptBlock());
    req.user = AnalysisPrompts::compareUser(digests, notes);
    req.schema = AnalysisPrompts::compareSchema();
    req.toolName = QStringLiteral("emit_comparison");
    req.toolDescription = QStringLiteral("Return the comparison table.");
    req.maxTokens = m_settings->analysisMaxTokens();
    req.temperature = 0.1;

    const QString scope = scopeKey();
    m_call = StructuredCall::start(m_client, req, this);
    emit stateChanged();
    connect(m_call, &StructuredCall::succeeded, this,
            [this, scope](const QJsonObject &result) {
                m_call.clear();
                QJsonObject stored = result;
                QJsonArray papers;
                for (const Entry &e : m_basket) {
                    papers.append(
                        QJsonObject{{QStringLiteral("paperId"), e.paperId},
                                    {QStringLiteral("title"), e.title}});
                }
                stored.insert(QStringLiteral("papers"), papers);
                m_result = stored;
                m_resultAuthor = m_store->userEmail();
                m_store->putLibraryAnalysis(Analysis::KindCompare, scope,
                                            stored,
                                            m_settings->model(),
                                            scope, m_basket.size());
                m_resultUpdatedAt =
                    m_store->libraryAnalysis(Analysis::KindCompare, scope)
                        .updatedAt;
                emit resultChanged();
                emit stateChanged();
            });
    connect(m_call, &StructuredCall::failed, this, [this](const QString &e) {
        m_call.clear();
        setError(e);
        emit stateChanged();
    });
}

void CompareService::cancel()
{
    if (m_call) {
        m_call->abort();
        m_call.clear();
        emit stateChanged();
    }
}

void CompareService::setError(const QString &e)
{
    if (e == m_lastError)
        return;
    m_lastError = e;
    emit stateChanged();
}
