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
