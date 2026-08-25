#pragma once

#include "TaskTypes.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

class TaskListModel;
class Settings;

// The one queue.
//
// A service does not start its own work any more: it submits a request and
// hands over two callbacks, one to start and one to stop. The manager
// decides when -- how many may run at once, and which ones refuse to run
// beside each other -- then calls back. Progress comes back the same way, so
// there is exactly one place that knows what is in flight, how far along it
// is and how long it has left, and exactly one place that has to be asked
// before the app may close.
class TaskManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(TaskListModel *model READ model CONSTANT)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY countsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY countsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY countsChanged)
    // 0..1 across everything running and queued, -1 when nothing is.
    Q_PROPERTY(double overallProgress READ overallProgress NOTIFY countsChanged)
    // Tasks the last session was still running when it closed.
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)

public:
    explicit TaskManager(Settings *settings, QObject *parent = nullptr);
    ~TaskManager() override;

    TaskListModel *model() const;
    int runningCount() const;
    int queuedCount() const;
    int activeCount() const;
    int finishedCount() const;
    double overallProgress() const;
    int pendingCount() const;

    // ── the service side ────────────────────────────────────────────
    // Returns the task id, or an empty string when an identical task (same
    // exclusion key) is already queued or running, or when another task has
    // claimed that key while it works through a list -- in which case
    // `start` is never called and the caller should do nothing.
    QString submit(const Tasks::Request &request,
                   std::function<void()> start,
                   std::function<void()> stop);

    // Steps done out of total. Passing total <= 0 keeps whatever the
    // request declared. Safe to call with an id that has already finished.
    void setProgress(const QString &id, int done, int total = -1);
    // A line under the title: which paragraph, which module, which paper.
    void setNote(const QString &id, const QString &note);
    void finish(const QString &id, bool ok, const QString &error = QString());
    // The service stopped the work itself -- the pane's own Cancel button,
    // or the paper being closed. The row ends Canceled rather than Failed,
    // because nothing went wrong, and the stop callback is not called: the
    // work is already stopped.
    void markCanceled(const QString &id);

    // A task that works through many papers holds each paper's key while it
    // is on it, so an individual run of the same work cannot start beside
    // it. Claims are dropped when the task ends.
    void claim(const QString &taskId, const QString &exclusionKey);
    void releaseClaim(const QString &taskId, const QString &exclusionKey);

    // True while anything with this kind and paper is queued or running.
    Q_INVOKABLE bool isActive(const QString &kindKey, const QString &paperId) const;
    Q_INVOKABLE QString activeId(const QString &kindKey, const QString &paperId) const;

    // ── the user side ───────────────────────────────────────────────
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void clearFinished();
    // False when the kind's resumer looked at the payload and said no -- the
    // paper it needs is not the one on screen -- so a caller can tell "it is
    // running again" from "nothing happened". The row is left as it was
    // either way.
    Q_INVOKABLE bool retry(const QString &id);

    // ── closing and reopening ───────────────────────────────────────
    // Called from the quit path: writes every queued/running task to disk as
    // Interrupted so the next launch can offer them back. Returns how many
    // were written.
    int saveInterrupted();
    // What the last session left behind, for the prompt on startup.
    Q_INVOKABLE QVariantList pending() const;
    // Hands every pending entry to its kind's resumer. What actually starts
    // is dropped from the list; what refused -- and what has no resumer at
    // all -- stays owed, is written back to disk, and can be tried again.
    Q_INVOKABLE void resumePending();
    // Retry every pending entry whose resumer may now succeed. Called when a
    // paper has finished loading.
    Q_INVOKABLE void retryPending();
    Q_INVOKABLE void discardPending();

    // The services that submit work know a paper id but not what to call it
    // -- a paper opened from a project lives in the blob cache under a
    // sha256 -- and not which project it belongs to. main.cpp knows both, so
    // it lends the manager two lookups rather than every service growing two
    // more constructor arguments. Either may be left unset.
    void setContext(std::function<QString()> currentProjectId,
                    std::function<QString(const QString &paperId)> paperTitleFor);

    // Resuming a paper's work needs that paper open. The manager cannot open
    // it -- main.cpp can -- so it asks, and tries again when the app says the
    // paper is ready.
    void setPaperOpener(std::function<void(const QString &paperId)> opener);

    // A kind that can be resumed registers how, once, at construction. The
    // function is handed the request's `resume` payload and returns false if
    // it cannot start it after all (the paper is gone, the project changed).
    void registerResumer(Tasks::Kind kind,
                         std::function<bool(const QJsonObject &)> resumer);

signals:
    void countsChanged();
    void pendingChanged();
    void taskFinished(const QString &id, bool ok, const QString &error);

private:
    class Private;
    Private *d;
};
