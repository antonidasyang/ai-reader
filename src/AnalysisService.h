#pragma once

#include "LlmClientCache.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class AnalysisStore;
class LlmClient;
class PaperController;
class ProjectProfileController;
class DeepModuleJob;
class QuickAnalysisJob;
class Settings;
class TaskManager;

// The interpretation of the paper currently on screen.
//
// Owns the quick read (§2): generate it, keep it, know when it is stale,
// and hand it to QML as a plain map. The generated result is a structured
// digest -- claims with provenance and checked citations -- and the pane
// renders it; the model never writes the Markdown the reader sees, which
// is what makes "show me the evidence", "regenerate just this part" and a
// clean export possible at all.
//
// Results are stored per member in the project, so a collaborator's
// interpretation shows up here (attributed) instead of every member paying
// for the same paper.
class AnalysisService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Status status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)
    Q_PROPERTY(QString modelInUse READ modelInUse NOTIFY stateChanged)

    Q_PROPERTY(QString paperId READ paperId NOTIFY paperChanged)
    Q_PROPERTY(QString paperTitle READ paperTitle WRITE setPaperTitle NOTIFY paperChanged)

    Q_PROPERTY(QVariantMap quick READ quick NOTIFY quickChanged)
    Q_PROPERTY(bool hasQuick READ hasQuick NOTIFY quickChanged)
    // The paragraphs, prompt, profile or model moved since this was
    // generated -- §17's "分析结果可能过期".
    Q_PROPERTY(bool quickStale READ quickStale NOTIFY quickChanged)
    // False when there was no project to file it in, so it lives only in
    // this window.
    Q_PROPERTY(bool quickSaved READ quickSaved NOTIFY quickChanged)
    Q_PROPERTY(bool quickIsMine READ quickIsMine NOTIFY quickChanged)
    Q_PROPERTY(QString quickAuthorEmail READ quickAuthorEmail NOTIFY quickChanged)
    Q_PROPERTY(QString quickModel READ quickModel NOTIFY quickChanged)
    Q_PROPERTY(QString quickUpdatedAt READ quickUpdatedAt NOTIFY quickChanged)

    // ── the close reading (§3), module by module ─────────────────────
    Q_PROPERTY(QVariantMap deep READ deep NOTIFY deepChanged)
    Q_PROPERTY(bool hasDeep READ hasDeep NOTIFY deepChanged)
    Q_PROPERTY(bool deepStale READ deepStale NOTIFY deepChanged)
    Q_PROPERTY(bool deepSaved READ deepSaved NOTIFY deepChanged)
    Q_PROPERTY(bool deepIsMine READ deepIsMine NOTIFY deepChanged)
    Q_PROPERTY(QString deepAuthorEmail READ deepAuthorEmail NOTIFY deepChanged)
    Q_PROPERTY(QStringList moduleIds READ moduleIds CONSTANT)
    Q_PROPERTY(int deepDone READ deepDone NOTIFY deepChanged)
    Q_PROPERTY(int deepTotal READ deepTotal NOTIFY deepChanged)
    Q_PROPERTY(bool deepRunning READ deepRunning NOTIFY deepChanged)

    // ── the reader's own notes (§5, §16) ─────────────────────────────
    Q_PROPERTY(QVariantList notes READ notes NOTIFY notesChanged)

public:
    enum Status { Idle, Running, Done, Failed };
    Q_ENUM(Status)

    AnalysisService(Settings *settings, PaperController *paper,
                    AnalysisStore *store, ProjectProfileController *profile,
                    QObject *parent = nullptr);

    // Both readings become tasks once there is a manager to hold them, so
    // one queue knows what is in flight and the app can be asked before it
    // closes. Without one -- the harnesses build this service bare -- every
    // path below runs directly, exactly as it always has.
    void setTasks(TaskManager *tasks);

    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    bool canRun() const;
    QString modelInUse() const;

    QString paperId() const;
    QString paperTitle() const { return m_paperTitle; }
    void setPaperTitle(const QString &title);

    QVariantMap quick() const { return m_quick.toVariantMap(); }
    bool hasQuick() const { return !m_quick.isEmpty(); }
    bool quickStale() const;
    bool quickSaved() const { return m_quickSaved; }
    bool quickIsMine() const { return m_quickIsMine; }
    QString quickAuthorEmail() const { return m_quickAuthorEmail; }
    QString quickModel() const { return m_quickModel; }
    QString quickUpdatedAt() const { return m_quickUpdatedAt; }

    QVariantMap deep() const { return m_deep.toVariantMap(); }
    bool hasDeep() const;
    bool deepStale() const;
    bool deepSaved() const { return m_deepSaved; }
    bool deepIsMine() const { return m_deepIsMine; }
    QString deepAuthorEmail() const { return m_deepAuthorEmail; }
    QStringList moduleIds() const;
    int deepDone() const;
    int deepTotal() const;
    bool deepRunning() const { return !m_deepQueue.isEmpty() || m_deepInflight > 0; }
    QVariantList notes() const;

    // What one module produced, empty when it has not run.
    // §16: the last couple of versions of this paper's interpretation.
    Q_INVOKABLE QVariantList quickHistory() const;
    Q_INVOKABLE bool restoreQuick(int index);
    Q_INVOKABLE QVariantList deepHistory() const;
    Q_INVOKABLE bool restoreDeep(int index);

    Q_INVOKABLE QVariantMap module(const QString &id) const;
    Q_INVOKABLE QString moduleTitle(const QString &id) const;
    Q_INVOKABLE QString moduleError(const QString &id) const;
    Q_INVOKABLE bool moduleBusy(const QString &id) const;

public slots:
    // `force` regenerates even when a current interpretation exists.
    void generateQuick(bool force = false);
    void cancel();
    // Drops our own stored interpretation for this paper.
    void discardQuick();

    // The close reading. `force` re-runs modules that already exist.
    void generateDeep(bool force = false);
    // §5: redo one part without touching the rest.
    void regenerateModule(const QString &id);
    void cancelDeep();
    void discardDeep();

    // §5 / §16: the reader's own notes, kept across regenerations.
    void saveNote(const QString &text, const QString &moduleId = QString());
    void removeNote(int index);

signals:
    void stateChanged();
    void paperChanged();
    void quickChanged();
    void deepChanged();
    void notesChanged();

private:
    void onPaperChanged();
    void reloadFromStore();
    void setStatus(Status s, const QString &err = QString());
    // Point m_client at the current settings, unless a job is using it.
    void refreshClient();
    void clearQuick();
    void clearDeep();
    void pumpDeep();
    void startModule(const QString &id);
    // The work itself, split out of generateQuick()/generateDeep() so the
    // direct path and a task's start callback run the same thing.
    void startQuickRun();
    void startDeepRun(const QStringList &modules, bool force);
    // §5's one module, split out the same way: what regenerateModule() does
    // once its own task has been admitted.
    void startModuleRun(const QString &id);
    // The stop callback of a single module's task.
    void cancelModule(const QString &moduleId);
    // Which modules a run would cover: what is missing (or all of them when
    // forced), minus anything already queued or in flight. `only` narrows it
    // to the list a resumed run brought back.
    QStringList deepModulesToRun(bool force, const QStringList &only) const;
    // Submit or run, depending on whether there is a manager. False when
    // there was nothing to do or the manager refused an identical run.
    bool beginDeepRun(const QStringList &modules, bool force);
    void reportDeepProgress();
    // How many of the whole run's own modules are still waiting or in
    // flight. A module regenerated on its own goes through the same queue
    // and must not be counted here.
    int deepRunLeft() const;
    // `only` narrows it to one run's modules, for the same reason.
    QString firstDeepError(const QSet<QString> &only = QSet<QString>()) const;
    // Taken out first, always: the manager must never be told twice about
    // one task, and a cancel and a job's own answer can race for the same
    // one. Empty when there is no manager or no task.
    QString takeQuickTaskId();
    QString takeDeepTaskId();
    QString takeModuleTaskId(const QString &moduleId);
    void finishQuickTask(bool ok, const QString &error = QString());
    void finishDeepTask(bool ok, const QString &error = QString());
    void finishModuleTask(const QString &moduleId, bool ok,
                          const QString &error = QString());
    // The service stopped the work itself -- the pane's Cancel button, or
    // the reader closing the paper. Nothing went wrong, so the row ends
    // Canceled rather than Failed with no reason under it.
    void cancelQuickTask();
    void cancelDeepTask();
    void cancelModuleTask(const QString &moduleId);
    void persistDeep();
    void reloadNotes();
    QString currentInputHash() const;
    int contextChars() const;

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    AnalysisStore *m_store;
    ProjectProfileController *m_profile;

    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    QPointer<QuickAnalysisJob> m_job;
    QPointer<TaskManager> m_tasks;
    QString m_quickTaskId;
    QString m_deepTaskId;
    // How many modules this run set out to do -- deepDone/deepTotal count
    // what is stored, which is not the same thing once a run skips modules
    // a previous one already wrote.
    int m_deepTaskTotal = 0;
    // False while the task is still queued: a single module regenerated in
    // that window must not be mistaken for the whole run finishing.
    bool m_deepTaskStarted = false;
    // Which modules that task set out to do. Everything else in the queue
    // belongs to a §5 regeneration with a task of its own, and neither the
    // whole run's bar nor its verdict may be built out of those.
    QSet<QString> m_deepRunModules;
    // moduleId -> task id, for modules being redone one at a time.
    QHash<QString, QString> m_moduleTaskIds;

    QString m_paperTitle;
    QString m_lastPaperId;
    // Hash of the paragraph text as it stands now, recomputed when the
    // paper changes rather than on every property read.
    QString m_contentHash;

    QJsonObject m_quick;
    QString m_quickInputHash;
    QString m_quickAuthorEmail;
    QString m_quickModel;
    QString m_quickUpdatedAt;
    bool m_quickIsMine = true;
    bool m_quickSaved = false;

    // { "modules": { "<id>": {...} }, "meta": {...} }
    QJsonObject m_deep;
    QString m_deepInputHash;
    QString m_deepAuthorEmail;
    QString m_deepUpdatedAt;
    bool m_deepIsMine = true;
    bool m_deepSaved = false;
    QStringList m_deepQueue;
    QHash<QString, QString> m_deepErrors;
    QSet<QString> m_deepBusy;
    int m_deepInflight = 0;
    bool m_deepForce = false;

    QJsonArray m_notes;

    Status m_status = Idle;
    QString m_lastError;
};
