#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class AnalysisStore;
class LlmClient;
class PaperController;
class ProjectProfileController;
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

public slots:
    // `force` regenerates even when a current interpretation exists.
    void generateQuick(bool force = false);
    void cancel();
    // Drops our own stored interpretation for this paper.
    void discardQuick();

signals:
    void stateChanged();
    void paperChanged();
    void quickChanged();

private:
    void onPaperChanged();
    void reloadFromStore();
    void setStatus(Status s, const QString &err = QString());
    void clearQuick();
    QString currentInputHash() const;
    int contextChars() const;

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    AnalysisStore *m_store;
    ProjectProfileController *m_profile;

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

    Status m_status = Idle;
    QString m_lastError;
};
