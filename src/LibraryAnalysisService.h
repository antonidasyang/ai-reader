#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class AnalysisStore;
class LlmClient;
class ProjectController;
class ProjectProfileController;
class Settings;
class StructuredCall;

// Everything that looks at the whole project at once (§8–§15): the category
// system, the research map, consensus and conflict, how the field moved, what
// this collection does not cover, candidate openings, and what to do next.
//
// All of it reads the per-paper digests and none of it reads a PDF. That is
// the design decision the whole feature rests on: fifty papers' full text is
// hundreds of thousands of tokens and cannot be asked a question; fifty
// digests fit in one call.
//
// Results are shared by the project — one per kind, stamped with who
// generated it, with the previous few versions kept so regenerating is never
// a silent loss for the other members.
class LibraryAnalysisService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString runningKind READ runningKind NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(int digestCount READ digestCount NOTIFY stateChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)

public:
    LibraryAnalysisService(Settings *settings, AnalysisStore *store,
                           ProjectController *projects,
                           ProjectProfileController *profile,
                           QObject *parent = nullptr);

    QString runningKind() const { return m_runningKind; }
    QString lastError() const { return m_lastError; }
    int digestCount() const;
    bool canRun() const;

    Q_INVOKABLE void generate(const QString &kind);
    Q_INVOKABLE void cancel();

    Q_INVOKABLE QVariantMap result(const QString &kind) const;
    Q_INVOKABLE bool has(const QString &kind) const;
    Q_INVOKABLE QString authorOf(const QString &kind) const;
    Q_INVOKABLE QString updatedAtOf(const QString &kind) const;
    Q_INVOKABLE int paperCountOf(const QString &kind) const;
    // True when papers have been interpreted (or re-interpreted) since this
    // was generated — §17, at library scale.
    Q_INVOKABLE bool isStale(const QString &kind) const;
    Q_INVOKABLE QVariantList history(const QString &kind) const;
    Q_INVOKABLE bool restoreVersion(const QString &kind, int index);
    Q_INVOKABLE QString titleOf(const QString &kind) const;

    // Papers named by a library analysis, resolved for display / clicking.
    Q_INVOKABLE QString paperTitle(const QString &paperId) const;

    // ── §8.3: the reader owns the category system ────────────────────
    Q_INVOKABLE void renameCategory(const QString &categoryId,
                                    const QString &name);
    Q_INVOKABLE void setCategoryLocked(const QString &categoryId, bool locked);
    Q_INVOKABLE void setCategoryConfirmed(const QString &categoryId,
                                          bool confirmed);
    Q_INVOKABLE void mergeCategories(const QString &intoId,
                                     const QString &fromId);
    Q_INVOKABLE void addCategory(const QString &dimension, const QString &name);
    Q_INVOKABLE void removeCategory(const QString &categoryId);
    Q_INVOKABLE void assignPaper(const QString &paperId,
                                 const QString &categoryId, bool on);
    // §8.4: place papers the category system has never seen, without
    // redrawing it.
    Q_INVOKABLE void classifyNewPapers();
    Q_INVOKABLE QStringList unclassifiedPapers() const;

signals:
    void stateChanged();
    void resultChanged(const QString &kind);

private:
    QJsonArray briefs() const;
    QString inputHashNow() const;
    // One call: build the prompt for `kind`, run it, optionally reshape the
    // answer, and file it under `storeKind`. Classification reshapes into the
    // category system rather than storing an object of its own.
    void run(const QString &kind, const QString &storeKind,
             const QJsonArray &briefs, const QJsonObject &extra,
             std::function<QJsonObject(const QJsonObject &)> postProcess);
    QJsonObject mergeTaxonomy(const QJsonObject &fresh) const;
    QJsonObject taxonomy() const;
    void saveTaxonomy(const QJsonObject &tax);
    void setError(const QString &e);

    QPointer<Settings> m_settings;
    AnalysisStore *m_store;
    ProjectController *m_projects;
    ProjectProfileController *m_profile;
    QPointer<LlmClient> m_client;
    QPointer<StructuredCall> m_call;

    QString m_runningKind;
    QString m_lastError;
};
