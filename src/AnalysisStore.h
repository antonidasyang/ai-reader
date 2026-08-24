#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "LibraryDb.h"

class AuthController;
class ProjectController;
class SyncEngine;

// One stored analysis, decoded.
struct AnalysisRecord {
    QString id;
    QString paperId;
    QString kind;
    QString scopeHash;
    QString author;
    QString authorEmail;
    QString model;
    QString updatedAt;
    QString inputHash;
    QString status;
    QString error;
    QString title;
    QJsonObject payload;
    int paperCount = 0;
    bool mine = false;
    bool valid = false;
};

// Persistence for the interpretation layer. Everything it writes is an
// ordinary synced object, so the existing offline-first engine carries it:
// written locally first, pushed when there is a network, pulled by the
// other members of the project. The server stores `data` without looking
// inside it, so none of these types cost a backend change.
//
// Who owns what:
//   * paper_analysis  — one row per (paper, kind, member). A member's two
//     machines share a row; two members keep their own, and a collaborator's
//     interpretation is readable but never overwritten (§16).
//   * library_analysis — one row per (project, kind[, scope]), shared by the
//     whole project, stamped with who generated it and keeping the last few
//     versions so a regenerate is never a silent loss (§16 历史版本).
//   * project_profile — one row per project (§6).
//   * analysis_note   — one row per (paper, member): the reader's own notes
//     and edits, which a regenerate must never touch (§16).
class AnalysisStore : public QObject
{
    Q_OBJECT
public:
    AnalysisStore(LibraryDb *db, ProjectController *projects, SyncEngine *sync,
                  AuthController *auth, QObject *parent = nullptr);

    QString projectId() const;
    QString userId() const;
    QString userEmail() const;
    // Signed in, a project selected, and we may write to it.
    bool canWrite() const;

    // ── per-paper analyses ───────────────────────────────────────────
    bool putPaperAnalysis(const QString &paperId, const QString &kind,
                          const QJsonObject &payload, const QString &model,
                          const QString &inputHash, const QString &status,
                          const QString &error, const QString &title);
    // Ours if we have one, otherwise the most recent collaborator's.
    AnalysisRecord paperAnalysis(const QString &paperId,
                                 const QString &kind) const;
    // Every member's, newest first.
    QList<AnalysisRecord> paperAnalysesFor(const QString &paperId,
                                           const QString &kind) const;
    // One per paper across the project — the unit every library-level
    // analysis reads (design decision A: digests, never full text).
    QList<AnalysisRecord> paperAnalyses(const QString &kind) const;
    void removePaperAnalysis(const QString &paperId, const QString &kind);
    // The last couple of versions of our own interpretation of a paper, so
    // regenerating one is recoverable (§16 历史版本).
    QJsonArray paperHistoryIndex(const QString &paperId,
                                 const QString &kind) const;
    bool restorePaperVersion(const QString &paperId, const QString &kind,
                             int index);

    // ── project-wide analyses ────────────────────────────────────────
    bool putLibraryAnalysis(const QString &kind, const QString &scopeHash,
                            const QJsonObject &payload, const QString &model,
                            const QString &inputHash, int paperCount);
    AnalysisRecord libraryAnalysis(const QString &kind,
                                   const QString &scopeHash = QString()) const;
    // [{generatedAt, generatedBy, generatedByEmail, model}], newest first.
    QJsonArray libraryHistoryIndex(const QString &kind,
                                   const QString &scopeHash = QString()) const;
    QJsonObject libraryHistoryPayload(const QString &kind,
                                      const QString &scopeHash, int index) const;
    bool restoreLibraryVersion(const QString &kind, const QString &scopeHash,
                               int index);

    // ── research profile (§6) ────────────────────────────────────────
    QJsonObject profile() const;
    bool putProfile(const QJsonObject &fields);

    // ── a member's own notes on a paper (§5, §16) ────────────────────
    QJsonObject note(const QString &paperId) const;
    bool putNote(const QString &paperId, const QJsonObject &payload);

    // ── the papers a member has lined up to compare (§10.1) ──────────
    // Synced like everything else, so the selection follows the account to
    // another machine instead of living in this one's settings file.
    QJsonArray compareBasket() const;
    bool putCompareBasket(const QJsonArray &papers);

signals:
    // Anything the store serves may have moved: a sync landed, the project
    // changed, or we just wrote something.
    void changed();

private:
    AnalysisRecord decodePaper(const SyncObjectRow &row) const;
    AnalysisRecord decodeLibrary(const SyncObjectRow &row) const;

    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    AuthController *m_auth;
};
