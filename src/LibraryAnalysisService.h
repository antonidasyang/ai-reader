#pragma once

#include "LlmClientCache.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

class AnalysisStore;
class LlmClient;
class ProjectController;
class ProjectProfileController;
class Settings;
class StructuredCall;
class TaskManager;

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

    // Each kind becomes its own task, so generateAll() hands the manager
    // seven of them and lets its queue do the pacing the list below used to
    // do alone. Without a manager -- the harnesses build this service bare --
    // that list is still what serialises them.
    void setTasks(TaskManager *tasks);

    QString runningKind() const { return m_runningKind; }
    QString lastError() const { return m_lastError; }
    int digestCount() const;
    bool canRun() const;

    Q_INVOKABLE void generate(const QString &kind);
    // Every kind, one after another. They share a model and each is a whole
    // library's worth of reading, so they queue rather than run at once.
    Q_INVOKABLE void generateAll();
    Q_PROPERTY(int queuedKinds READ queuedKinds NOTIFY stateChanged)
    int queuedKinds() const { return m_queue.size() + m_deferred.size(); }
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
    // Split some papers out of a category into a new one beside it. The new
    // category is the reader's, so a later regeneration leaves it alone.
    Q_INVOKABLE void splitCategory(const QString &categoryId,
                                   const QString &newName,
                                   const QStringList &paperIds);
    // The papers currently in a category, as {paperId, title}.
    Q_INVOKABLE QVariantList categoryPapers(const QString &categoryId) const;
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
    using PostProcess = std::function<QJsonObject(const QJsonObject &)>;

    // A run that is ready to go and is waiting for the model call in front
    // of it. Two project-wide analyses cannot share one StructuredCall, and
    // the manager may well admit several at once.
    struct QueuedRun {
        QString kind;
        QString storeKind;
        QJsonArray briefs;
        QJsonObject extra;
        PostProcess postProcess;
    };

    QJsonArray briefs() const;
    QString inputHashNow() const;
    // Everything canRun() asks except "nothing is running": submitting a
    // task while a call is in flight is fine, starting a second one is not.
    bool canSubmit() const;
    // One call: build the prompt for `kind`, run it, optionally reshape the
    // answer, and file it under `storeKind`. Classification reshapes into the
    // category system rather than storing an object of its own.
    void run(const QString &kind, const QString &storeKind,
             const QJsonArray &briefs, const QJsonObject &extra,
             PostProcess postProcess);
    // The call itself, split out of run() so the direct path and a task's
    // start callback go the same way.
    void startCall(const QString &kind, const QString &storeKind,
                   const QJsonArray &briefs, const QJsonObject &extra,
                   PostProcess postProcess);
    // cancel(), narrowed to one kind -- what stopping a single task means.
    void cancelKind(const QString &kind);
    QString taskTitleFor(const QString &kind) const;
    void finishTaskFor(const QString &kind, bool ok,
                       const QString &error = QString());
    // The service stopped the work itself -- the viewer's stop button, or a
    // project switch under a task that was still waiting. Nothing went
    // wrong, so the row ends Canceled rather than Failed with no reason.
    void cancelTaskFor(const QString &kind);
    void cancelAllTasks();
    QJsonObject mergeTaxonomy(const QJsonObject &fresh) const;
    void runNextQueued();
    QJsonObject taxonomy() const;
    void saveTaxonomy(const QJsonObject &tax);
    void setError(const QString &e);

    QPointer<Settings> m_settings;
    AnalysisStore *m_store;
    ProjectController *m_projects;
    ProjectProfileController *m_profile;
    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    QPointer<StructuredCall> m_call;

    QPointer<TaskManager> m_tasks;
    QHash<QString, QString> m_taskIds;   // kind -> task id
    QVector<QueuedRun> m_deferred;

    QString m_runningKind;
    QStringList m_queue;
    QString m_lastError;
};
