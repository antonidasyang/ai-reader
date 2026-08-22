#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include "LibraryDb.h"

class AuthController;
class BlockCache;
class PaperController;
class ProjectController;
class Settings;
class SyncEngine;
class TranslationCache;
class TranslationService;

// Puts a paper's paragraph segmentation and its translations into the project,
// so the seconds of clustering and the tokens of translation are paid once.
//
// Both used to be local-only JSON caches under AppDataLocation; this bridges
// them to the existing sync engine as objects of type "paper_data", keyed
// deterministically by (project, paper, kind, author) — the same shape
// AiArtifactService uses. The payload is the cache file itself, zlib-deflated
// and base64'd, because a segmented paper runs to hundreds of KB of text.
//
// Who wins, when two members have data for the same paper:
//
//   * Same account, another machine — the id matches, so it is simply our own
//     object coming back. It is adopted whenever this machine has nothing.
//   * Another account — adopted only where we have nothing of our own.
//     Paragraph lists are all-or-nothing (a merge of two segmentations is not
//     a segmentation), so ours wins outright as soon as we have segmented or
//     edited. Translations are per entry: a paragraph we translated ourselves
//     always wins, and a collaborator's entry only ever fills a gap.
//
// Adopted content is marked as such in the cache file and is never re-published
// under this account, so a project of N members doesn't end up storing N copies
// of the same work.
class PaperSyncService : public QObject
{
    Q_OBJECT
    // Whether this machine will publish what it segments and translates:
    // signed in, a project selected that we can write to, and the setting on.
    Q_PROPERTY(bool sharing READ sharing NOTIFY stateChanged)
    // Who the paragraphs on screen came from, empty when they are ours:
    // the member's id for the code, their email for the reading pane.
    Q_PROPERTY(QString blocksOrigin READ blocksOrigin NOTIFY stateChanged)
    Q_PROPERTY(QString blocksOriginLabel READ blocksOriginLabel NOTIFY stateChanged)
    // One line about what was just adopted, for the reading pane to show.
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    PaperSyncService(LibraryDb *db, ProjectController *projects,
                     SyncEngine *sync, AuthController *auth,
                     PaperController *paper, TranslationService *translation,
                     Settings *settings, QObject *parent = nullptr);

    bool sharing() const;
    QString blocksOrigin() const { return m_blocksOrigin; }
    QString blocksOriginLabel() const { return m_blocksOriginLabel; }
    QString notice() const { return m_notice; }

    Q_INVOKABLE void dismissNotice();
    // Publish anything pending right now (paper switch, app quit).
    void flushPending();

signals:
    void stateChanged();
    void noticeChanged();

private:
    // ── adoption ──────────────────────────────────────────────────────
    void onBlockCacheReady(const QString &paperId);
    void onTranslationCacheReady(const QString &paperId);
    void onProjectSynced();
    bool adoptBlocks(const QString &paperId);
    int adoptTranslations(const QString &paperId);

    // ── publication ───────────────────────────────────────────────────
    void onCacheWritten(bool blocks);
    // Publish one kind if there is anything of ours to publish. Returns
    // whether an artifact actually went into the outbox, which is what arms
    // the throttle — a write that turns out to have nothing to share (an
    // adoption, or an unchanged payload) must not delay the next real one.
    bool publishKind(bool blocks);
    // Writes the artifact into the outbox; false when there was nothing to
    // write (identical payload already there, or over the size limit).
    bool putArtifact(const QString &kind, const QString &paperId,
                     const QJsonObject &inner, int n);
    void refreshBlocksOrigin();
    // Queue a publish for work that is already on disk. Opening a paper that
    // was segmented or translated before this machine started sharing is the
    // only moment we get to notice it, and the throttle keeps the open path
    // itself free of compression work.
    void offerExisting(bool blocks);

    QString artifactId(const QString &paperId, const QString &kind,
                       const QString &author) const;
    // The decoded cache document behind one index entry, or an empty object
    // when the row is missing, tombstoned, or its payload doesn't inflate.
    QJsonObject loadPayload(const PaperDataRef &ref) const;
    void setNotice(const QString &text);

    LibraryDb *m_db;
    ProjectController *m_projects;
    SyncEngine *m_sync;
    AuthController *m_auth;
    PaperController *m_paper;
    TranslationService *m_translation;
    Settings *m_settings;
    BlockCache *m_blocks = nullptr;
    TranslationCache *m_trans = nullptr;

    // Publication is throttled: a long translation run writes its cache every
    // second or so, and each publish costs a recompress here plus a pull for
    // every other member. Leading edge fires at once, the trailing one mops
    // up. The two kinds throttle separately — segmenting happens once, while
    // translations churn, and neither should hold the other up.
    QTimer m_blocksTimer;
    QTimer m_transTimer;
    bool m_blocksPending = false;
    bool m_transPending = false;

    // objectId → the updatedAt we last merged, so the 30-second poll doesn't
    // re-inflate and re-scan every collaborator's translations each time.
    QHash<QString, QString> m_mergedRev;
    QString m_mergedPaper;

    QString m_blocksOrigin;        // the donor's member id, if any
    QString m_blocksOriginLabel;   // ...and how to name them on screen
    QString m_notice;
};
