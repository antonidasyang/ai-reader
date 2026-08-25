#pragma once

#include "TaskListModel.h"
#include "TaskTypes.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

// The queue's insides, and the one thing it keeps on disk.
//
// TaskManager, TaskListModel and this file are three faces of one component:
// the manager decides what is in the queue, the model says what it looks
// like, and the store is what survives a restart. They therefore share a
// record type and a vector, and that shared vector lives in the model --
// which is what makes it impossible to change a task without the right
// begin/endInsertRows or dataChanged around the change. The manager reaches
// it through the friendship TaskListModel.h already declares.

namespace Tasks {

// How much of a fresh rate sample counts against the running average. Low
// enough that one slow paragraph does not double the estimate, high enough
// that a run which really has slowed down admits it within a few steps.
constexpr double kRateAlpha = 0.3;

// Past this, an estimate is not an estimate. A task that says "11 hours
// left" is telling you its rate measurement is broken, not its finish time,
// so the viewer is told nothing instead.
constexpr qint64 kMaxEtaMs = 24LL * 60LL * 60LL * 1000LL;

// A task as the queue holds it. The two callbacks are the whole trick: the
// manager can decide *when* work runs without knowing anything about what
// the work is.
struct Record {
    QString id;
    Request request;
    State state = State::Queued;

    int done = 0;
    // What the request declared, until a service that discovers the real
    // count revises it.
    int total = 0;
    QString note;
    QString error;

    QDateTime queuedAt;
    QDateTime startedAt;
    QDateTime finishedAt;

    std::function<void()> start;
    std::function<void()> stop;

    // Smoothed steps per millisecond, with the sample it was last measured
    // against. A plain done/elapsed estimate swings wildly when one step
    // happens to be slow, so each new sample is blended into the old rate
    // rather than replacing it.
    double rate = 0.0;
    qint64 rateAt = 0;      // ms since epoch
    int rateDone = 0;

    bool active() const
    {
        return state == State::Queued || state == State::Running;
    }

    // What two tasks may not share while either of them is active. An empty
    // exclusiveKey means the kind and the paper, which is what keeps two
    // runs off one paper without stopping the same work on another.
    QString exclusionKey() const
    {
        if (!request.exclusiveKey.isEmpty())
            return request.exclusiveKey;
        return kindKey(request.kind) + QChar('|') + request.paperId;
    }

    // 0..1, or -1 when the task cannot say how much there is to do.
    double progress() const
    {
        if (state == State::Succeeded)
            return 1.0;
        if (total <= 0)
            return -1.0;
        return qBound(0.0, double(done) / double(total), 1.0);
    }

    qint64 elapsedMs() const
    {
        if (!startedAt.isValid())
            return 0;
        const QDateTime end = finishedAt.isValid()
                                  ? finishedAt
                                  : QDateTime::currentDateTimeUtc();
        return qMax<qint64>(0, startedAt.msecsTo(end));
    }

    // -1 whenever we would be guessing: nothing to count, nothing counted
    // yet, no measured rate, or an answer too absurd to show.
    qint64 etaMs() const
    {
        if (state != State::Running || total <= 0 || done <= 0 || rate <= 0.0)
            return -1;
        const double remaining = double(total - done);
        if (remaining <= 0.0)
            return 0;
        const double ms = remaining / rate;
        if (ms <= 0.0 || ms > double(kMaxEtaMs))
            return -1;
        return qint64(ms);
    }
};

// A task the last session was still working on, as it goes to disk and comes
// back. Only the request survives -- the callbacks were the running app's,
// and the resumer builds new ones.
struct PendingTask {
    QString id;
    Request request;
    int done = 0;
};

} // namespace Tasks

// See the note at the top of the file: this is the model's private state,
// defined here because the manager mutates it and the model renders it.
class TaskListModel::Private : public QObject
{
public:
    explicit Private(TaskListModel *model)
        : QObject(model)
        , q(model)
    {
    }

    // Newest first, so a row is the vector read backwards. The mapping is
    // its own inverse: it turns a row into an index just as well.
    int rowOf(int index) const { return int(tasks.size()) - 1 - index; }
    int indexOfId(const QString &id) const;

    void append(const Tasks::Record &record);
    void removeAt(int index);
    // Empty roles means every role, which is what dataChanged already takes
    // it to mean.
    void touch(int index, const QVector<int> &roles = {});

    TaskListModel *q;
    QVector<Tasks::Record> tasks;   // submit order, oldest first
    QSet<QString> resumableKinds;   // kind keys a resumer was registered for
};

// What the app was in the middle of when it closed.
//
// Machine-local state and nothing else: it names work this computer was
// doing, it is meaningless on another one, and it never goes near the sync
// engine or a project. One file, rewritten whole, under the app's data
// directory.
namespace TaskStore {

QString filePath();
// Tasks with an empty resume payload are dropped: nothing could restart
// them, and offering them back on the next launch would be a lie.
bool save(const QVector<Tasks::PendingTask> &tasks);
// A missing or unreadable file is not an error -- it is the ordinary case of
// a session that closed with nothing running. Nothing is deleted on the way.
QVector<Tasks::PendingTask> load();
void clear();

} // namespace TaskStore
