#pragma once

#include "LlmClientCache.h"
#include "Block.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class AnalysisListModel;
class AnalysisStore;
class LlmClient;
class PaperSource;
class ProjectProfileController;
class QuickAnalysisJob;
class Settings;
class TaskManager;

// Interpreting a whole library (§7).
//
// Two things are being paced at once. Getting a paper's paragraphs is
// serialized -- one download, one segmentation at a time -- because PDFium
// has a global lock and the reader's own window has to keep rendering. The
// model calls are not: as soon as one paper's paragraphs are in hand its
// interpretation goes out and the next paper starts being fetched, so the
// slow half overlaps with the expensive half rather than following it.
//
// Nothing here opens a paper in the reader. Papers that already carry a
// current interpretation are skipped, including one a collaborator generated
// -- a five-person project pays for a paper once.
//
// Two kinds of run, never at once (one task per project, one exclusive key):
// the quick interpretation, one call per paper; and the close reading, nine
// calls per paper. The close reading needs the quick one as context, so a
// paper reached without one has it generated first rather than being failed
// -- and the papers are taken one at a time there, with the nine parts paced
// against the concurrency cap, so a starred set of thirty does not open two
// hundred and seventy calls at once.
class BatchAnalysisService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int failed READ failed NOTIFY progressChanged)
    Q_PROPERTY(int skipped READ skipped NOTIFY progressChanged)
    Q_PROPERTY(int running READ running NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY progressChanged)
    // Which of the two runs is on, so the pane can say "close-reading" rather
    // than "interpreting" and count the parts of the paper it is inside.
    Q_PROPERTY(bool deepMode READ deepMode NOTIFY progressChanged)
    Q_PROPERTY(int deepPartsDone READ deepPartsDone NOTIFY progressChanged)
    Q_PROPERTY(int deepPartsTotal READ deepPartsTotal NOTIFY progressChanged)

public:
    BatchAnalysisService(Settings *settings, AnalysisStore *store,
                         ProjectProfileController *profile,
                         PaperSource *source, AnalysisListModel *model,
                         QObject *parent = nullptr);

    // A whole run is one task, not one per paper: the papers are already
    // paced by the queue below, and a hundred rows in the viewer would tell
    // the reader less than one row that says which paper it is on. Without a
    // manager -- the harnesses build this service bare -- nothing changes.
    void setTasks(TaskManager *tasks);

    // m_sourceBusy counts: between taking a paper off the queue and the
    // model call going out, the PDF is being fetched and segmented, and on
    // the last paper of a run -- every paper of a close-read run, which
    // takes them one at a time -- the queue is empty and nothing is running
    // yet. Without it the pane's progress row and its Stop button vanish
    // for the whole of that, and the buttons that start a run come back to
    // life on top of one.
    bool busy() const
    {
        return m_running > 0 || !m_queue.isEmpty() || m_sourceBusy;
    }
    int total() const { return m_total; }
    int done() const { return m_done; }
    int failed() const { return m_failed; }
    int skipped() const { return m_skipped; }
    int running() const { return m_running; }
    QString status() const { return m_status; }
    bool canRun() const;
    bool deepMode() const { return m_deep; }
    int deepPartsDone() const { return m_deepRun.active ? m_deepRun.done : 0; }
    int deepPartsTotal() const;

    // Everything in the project without a current interpretation.
    Q_INVOKABLE void startPending();
    // A specific set, e.g. what the filters are showing.
    Q_INVOKABLE void startItems(const QStringList &itemIds, bool force = false);
    // The same set, but the nine-part close reading. This is what a star in
    // the library pane is for: marking a paper is a decision, and this is
    // the button that spends it.
    Q_INVOKABLE void startDeepItems(const QStringList &itemIds,
                                    bool force = false);
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void cancel();
    // Why one paper failed, for the row that shows it.
    Q_INVOKABLE QString errorFor(const QString &itemId) const;
    Q_INVOKABLE QStringList failedItems() const;

signals:
    void progressChanged();
    void statusChanged();
    void finished(int done, int failed, int skipped);

private:
    // What startItems() used to do, so the direct path and the task's start
    // callback enqueue the same way.
    void startRun(const QStringList &itemIds, bool force);
    // The shared body of startItems()/startDeepItems(): the two differ only
    // in what the task is called and what onSourceReady() does with the
    // paragraphs once they are in hand.
    void beginBatch(const QStringList &itemIds, bool force, bool deep);
    // The close reading of one paper, once its paragraphs and its quick
    // interpretation are both in hand.
    void beginDeepRun(const QString &itemId, const QString &paperId,
                      const QString &title, const QVector<Block> &blocks,
                      const QJsonObject &digest);
    void pumpDeepModules();
    void startDeepModule(const QString &moduleId);
    void finishDeepRun();
    // The quick interpretation of one paper. In a quick run it is the whole
    // job; in a close-read run it is the context the nine parts need, and
    // the close reading starts when it lands.
    void startQuick(const QString &itemId, const QString &paperId,
                    const QString &title, const QVector<Block> &blocks,
                    bool thenDeep);
    // Taken out first, always: the manager must never be told twice about
    // one task, and a cancel and the last paper's answer can race for it.
    QString takeTaskId();
    void finishTask(bool ok, const QString &error = QString());
    // The reader stopped it, or the project changed under it. Nothing went
    // wrong, so the row ends Canceled rather than Failed with no reason.
    void cancelTask();
    // A paper the batch is on is spoken for: the reader's own Interpret
    // button must not start a second call against the same paper.
    // Both keys while a close reading is running: it writes the quick
    // interpretation too when the paper has none.
    QStringList claimKeys(const QString &paperId) const;
    void claimPaper(const QString &paperId);
    void releasePaper(const QString &paperId);
    void releaseAllPapers();
    void pump();
    void onSourceReady(const QString &itemId, const QString &paperId,
                       const QString &title, const QVector<Block> &blocks);
    void onSourceFailed(const QString &itemId, const QString &reason);
    void recordFailure(const QString &itemId, const QString &reason);
    void finishIfIdle();
    void setStatus(const QString &s);

    QPointer<Settings> m_settings;
    AnalysisStore *m_store;
    ProjectProfileController *m_profile;
    PaperSource *m_source;
    AnalysisListModel *m_model;
    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    QPointer<TaskManager> m_tasks;
    QString m_taskId;
    // False while the task is submitted but still waiting its turn. The
    // papers a second Interpret click asks for in that window join m_pending
    // rather than running beside the batch, outside the concurrency cap and
    // ahead of the papers the task was opened for.
    bool m_taskStarted = false;
    QStringList m_pending;
    bool m_pendingForce = false;
    // Papers this task holds the individual-interpretation key for.
    QSet<QString> m_claimed;

    QQueue<QString> m_queue;
    QHash<QString, QString> m_errors;     // itemId -> why
    QStringList m_failedItems;
    bool m_sourceBusy = false;
    bool m_force = false;
    // The mode of the run now open. A run is one or the other for its whole
    // life: the two share a task and an exclusive key, so they cannot
    // overlap, and the queue never holds a mix.
    bool m_deep = false;
    // The one paper being close-read, and where its nine parts have got to.
    struct DeepRun {
        bool active = false;
        QString itemId;
        QString paperId;
        QString title;
        QVector<Block> blocks;
        QJsonObject digest;      // the quick interpretation, as context
        QJsonObject modules;     // what has come back so far
        QStringList queue;       // parts not yet asked for
        int inflight = 0;
        int done = 0;       // parts in hand, including ones already on disk
        int fetched = 0;    // parts this run actually paid for
        QString contentHash;
        QString firstError;
    };
    DeepRun m_deepRun;
    bool m_cancelled = false;
    int m_running = 0;
    int m_total = 0;
    int m_done = 0;
    int m_failed = 0;
    int m_skipped = 0;
    QString m_status;
};
