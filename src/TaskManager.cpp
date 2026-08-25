#include "TaskManager.h"

#include "Settings.h"
#include "TaskListModel.h"
#include "TaskStore.h"

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

using Tasks::Record;
using Tasks::State;

namespace {

// Twice a second is enough for a clock that counts in seconds, and cheap
// enough that a queue of thirty tasks does not repaint the world.
constexpr int kTickMs = 500;

// What the cap is when there is no Settings object to ask -- the same number
// Settings itself defaults to.
constexpr int kDefaultConcurrency = 2;

} // namespace

class TaskManager::Private
{
public:
    // The record vector lives in the model; see the note at the top of
    // TaskStore.h for why.
    TaskListModel::Private *m() const { return model->d; }
    QVector<Record> &rows() const { return model->d->tasks; }
    int indexOfId(const QString &id) const { return model->d->indexOfId(id); }

    int concurrency() const;
    void pump();
    void ensureTicker();
    void tick();

    // Which task holds this key, or empty. A claim is as binding as a live
    // task with that key: the work behind it is already being done.
    QString claimHolder(const QString &key) const;
    void dropClaims(const QString &taskId) { claims.remove(taskId); }

    // Hands one pending entry to its kind's resumer. False means it is still
    // owed -- no resumer for the kind, or a resumer that looked at the
    // payload and said not now -- and the entry has to be kept.
    bool tryResume(const Tasks::PendingTask &p) const;
    // One pass over everything still owed: what starts is dropped from the
    // list, what refuses is kept. True when at least one entry started, so a
    // caller knows whether the list on disk has anything to catch up with.
    bool resumeWhatCan();
    // What is left after a resume attempt goes straight back to disk, so the
    // file always says what is still owed rather than what once was. Nothing
    // left means nothing to keep.
    void writePending() const;
    // Ask the app to put a paper on screen so the entries waiting on it can
    // be tried again. Never the paper that was asked for last: a resumer
    // that refuses a paper that is already open would otherwise be asked
    // about it forever.
    void askForNextPaper();

    TaskManager *q = nullptr;
    QPointer<Settings> settings;
    TaskListModel *model = nullptr;
    QTimer ticker;
    QHash<QString, std::function<bool(const QJsonObject &)>> resumers;
    std::function<QString()> projectIdFn;
    std::function<QString(const QString &)> paperTitleFn;
    std::function<void(const QString &)> paperOpenerFn;
    QVector<Tasks::PendingTask> pending;
    // Task id -> the exclusion keys it is holding on behalf of the work it
    // is doing right now.
    QHash<QString, QSet<QString>> claims;
    QString lastOpenAsk;
    bool pumping = false;
    bool pumpAgain = false;
};

QString TaskManager::Private::claimHolder(const QString &key) const
{
    for (auto it = claims.constBegin(); it != claims.constEnd(); ++it) {
        if (it.value().contains(key))
            return it.key();
    }
    return {};
}

bool TaskManager::Private::tryResume(const Tasks::PendingTask &p) const
{
    const auto it = resumers.constFind(Tasks::kindKey(p.request.kind));
    // A kind nobody registered a resumer for cannot be started from here,
    // but it was still real work: it stays on the list rather than being
    // thrown away on the reader's behalf.
    if (it == resumers.constEnd())
        return false;
    // Copied out before the call, for the reason retry() copies it too: the
    // resumer submits, and a submit reaches back into the model.
    const std::function<bool(const QJsonObject &)> resumer = it.value();
    return resumer(p.request.resume);
}

bool TaskManager::Private::resumeWhatCan()
{
    // Taken out of the member first: every resumer submits, and a submit can
    // reach back here through the model.
    const QVector<Tasks::PendingTask> list = pending;
    pending.clear();
    for (const Tasks::PendingTask &p : list) {
        if (!tryResume(p))
            pending.append(p);
    }
    return pending.size() != list.size();
}

void TaskManager::Private::writePending() const
{
    // An empty list is a delete, the same way it is on the way out: a file
    // that still names work nothing owes any more would be offered back on
    // the next launch as though the session had been interrupted again.
    if (pending.isEmpty())
        TaskStore::clear();
    else
        TaskStore::save(pending);
}

void TaskManager::Private::askForNextPaper()
{
    if (!paperOpenerFn || pending.isEmpty())
        return;
    QString paperId;
    for (const Tasks::PendingTask &p : pending) {
        // Project-level work needs no paper on screen, and the paper that
        // was asked for last has had its chance already -- its entry refused
        // even with the paper open, so asking for it again is the loop.
        if (p.request.paperId.isEmpty() || p.request.paperId == lastOpenAsk)
            continue;
        paperId = p.request.paperId;
        break;
    }
    if (paperId.isEmpty())
        return;
    lastOpenAsk = paperId;
    paperOpenerFn(paperId);
}

int TaskManager::Private::concurrency() const
{
    const int cap = settings ? settings->analysisConcurrency()
                             : kDefaultConcurrency;
    return qMax(1, cap);
}

void TaskManager::Private::pump()
{
    if (pumping) {
        // Somebody submitted or finished from inside a signal this pump is
        // in the middle of emitting. Their pump is not lost -- it happens
        // below, once this one has stopped looking at rows by index.
        pumpAgain = true;
        return;
    }
    pumping = true;

    QStringList started;
    do {
        pumpAgain = false;

        const int cap = concurrency();
        QHash<QString, int> running;
        for (const Record &r : rows()) {
            if (r.state == State::Running)
                running[r.request.group] += 1;
        }

        // Admit first, announce second. A service that finishes inside its
        // own start(), or a delegate that reacts to dataChanged, must not be
        // able to reshape the vector while this loop still holds a reference
        // into it.
        QVector<int> admitted;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < rows().size(); ++i) {
            Record &r = rows()[i];
            if (r.state != State::Queued)
                continue;
            int &inGroup = running[r.request.group];
            if (inGroup >= cap)
                continue;
            inGroup += 1;
            r.state = State::Running;
            r.startedAt = now;
            // The rate is measured from here, against nothing done yet.
            r.rate = 0.0;
            r.rateAt = now.toMSecsSinceEpoch();
            r.rateDone = 0;
            admitted.append(i);
            if (r.start)
                started.append(r.id);
        }

        if (admitted.isEmpty())
            break;

        const QVector<int> roles{TaskListModel::StateRole,
                                 TaskListModel::StateLabelRole,
                                 TaskListModel::ProgressRole,
                                 TaskListModel::ElapsedMsRole,
                                 TaskListModel::EtaMsRole,
                                 TaskListModel::CanCancelRole};
        for (const int i : admitted)
            m()->touch(i, roles);
        ensureTicker();
        emit q->countsChanged();
        // Round again only if one of those two brought work in that this
        // pass could not see. Nothing admitted means nothing announced,
        // which means nobody was given the chance to ask.
    } while (pumpAgain);
    // Cleared only here: a delegate reacting to dataChanged, or anything
    // watching countsChanged, must not be able to start a second admission
    // pass over rows this one is still touching by index -- which is what
    // the loop above answers instead.
    pumping = false;
    pumpAgain = false;

    // Queued, so a service whose start() finishes on the spot cannot call
    // finish() -- and through it pump() -- while this pump is still going.
    // By id, and checked again on arrival: a cancel that lands in between
    // clears the callback and ends the row, and the work must not run after
    // that. The record is the only thing that knows.
    for (const QString &id : std::as_const(started)) {
        QMetaObject::invokeMethod(
            q,
            [this, id]() {
                const int i = indexOfId(id);
                if (i < 0)
                    return;
                const Record &r = rows().at(i);
                if (r.state != State::Running || !r.start)
                    return;
                // Copied out before the call: start() may submit or finish,
                // and either can move the vector under a reference into it.
                const std::function<void()> fn = r.start;
                fn();
            },
            Qt::QueuedConnection);
    }
}

void TaskManager::Private::ensureTicker()
{
    bool anyRunning = false;
    for (const Record &r : rows()) {
        if (r.state == State::Running) {
            anyRunning = true;
            break;
        }
    }
    // Nothing is moving when nothing runs, and a timer that fires into an
    // idle queue is a timer that keeps a laptop awake.
    if (anyRunning && !ticker.isActive())
        ticker.start();
    else if (!anyRunning && ticker.isActive())
        ticker.stop();
}

void TaskManager::Private::tick()
{
    const QVector<int> roles{TaskListModel::ElapsedMsRole,
                             TaskListModel::EtaMsRole,
                             TaskListModel::ProgressRole};
    bool anyRunning = false;
    for (int i = 0; i < rows().size(); ++i) {
        if (rows().at(i).state != State::Running)
            continue;
        anyRunning = true;
        m()->touch(i, roles);
    }
    if (!anyRunning)
        ticker.stop();
}

TaskManager::TaskManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->q = this;
    d->settings = settings;
    d->model = new TaskListModel(this);

    d->ticker.setInterval(kTickMs);
    connect(&d->ticker, &QTimer::timeout, this, [this]() { d->tick(); });

    // What the last session was in the middle of. It deliberately stays out
    // of the model: nothing is running, and a row that looks like a task but
    // has no work behind it would be a lie until the reader says "resume".
    d->pending = TaskStore::load();
}

TaskManager::~TaskManager()
{
    delete d;
}

TaskListModel *TaskManager::model() const
{
    return d->model;
}

int TaskManager::runningCount() const
{
    int n = 0;
    for (const Record &r : d->rows()) {
        if (r.state == State::Running)
            ++n;
    }
    return n;
}

int TaskManager::queuedCount() const
{
    int n = 0;
    for (const Record &r : d->rows()) {
        if (r.state == State::Queued)
            ++n;
    }
    return n;
}

int TaskManager::activeCount() const
{
    int n = 0;
    for (const Record &r : d->rows()) {
        if (r.active())
            ++n;
    }
    return n;
}

int TaskManager::finishedCount() const
{
    int n = 0;
    for (const Record &r : d->rows()) {
        if (r.state == State::Succeeded || r.state == State::Failed
            || r.state == State::Canceled)
            ++n;
    }
    return n;
}

double TaskManager::overallProgress() const
{
    // Weighted by steps, so translating a hundred paragraphs does not read
    // as half done because a two-step job beside it finished. A task that
    // cannot count its steps still has to count for something, so it counts
    // as one step that is not done until it is.
    double total = 0.0;
    double done = 0.0;
    for (const Record &r : d->rows()) {
        if (!r.active())
            continue;
        if (r.total > 0) {
            total += r.total;
            done += qBound(0, r.done, r.total);
        } else {
            total += 1.0;
        }
    }
    if (total <= 0.0)
        return -1.0;
    return qBound(0.0, done / total, 1.0);
}

int TaskManager::pendingCount() const
{
    return int(d->pending.size());
}

QString TaskManager::submit(const Tasks::Request &request,
                            std::function<void()> start,
                            std::function<void()> stop)
{
    Record rec;
    rec.request = request;
    rec.total = qMax(0, request.steps);

    // Fill in what the caller could not know (see setContext). A resolver
    // that answers with the paper id itself has not found a title, so the
    // file name the service passed stays.
    if (rec.request.projectId.isEmpty() && d->projectIdFn)
        rec.request.projectId = d->projectIdFn();
    if (!rec.request.paperId.isEmpty() && d->paperTitleFn) {
        const QString better = d->paperTitleFn(rec.request.paperId);
        if (!better.isEmpty() && better != rec.request.paperId)
            rec.request.paperTitle = better;
    }

    // The same work, already waiting or already under way. Doing it twice
    // costs tokens and lands two sets of answers on one paper, so the second
    // caller is told no rather than queued.
    const QString key = rec.exclusionKey();
    for (const Record &r : d->rows()) {
        if (r.active() && r.exclusionKey() == key)
            return {};
    }
    // The same, one level up: a task that works through many papers is one
    // row with one key of its own, so the paper it is on right now is spoken
    // for only because it claimed that paper's key. Without this, an
    // individual run of the same work on that paper would sail past the loop
    // above and both would pay for the model call.
    if (!d->claimHolder(key).isEmpty())
        return {};

    rec.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rec.state = State::Queued;
    rec.queuedAt = QDateTime::currentDateTimeUtc();
    rec.start = std::move(start);
    rec.stop = std::move(stop);

    const QString id = rec.id;
    d->m()->append(rec);
    emit countsChanged();
    d->pump();
    return id;
}

void TaskManager::setProgress(const QString &id, int done, int total)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return;
    Record &r = d->rows()[i];
    // A service that answers after a cancel, or after it already finished,
    // must not bring the row back to life.
    if (!r.active())
        return;

    if (total > 0)
        r.total = total;
    int value = qMax(0, done);
    if (r.total > 0)
        value = qMin(value, r.total);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (value < r.rateDone) {
        // The service started its count over; nothing measured before that
        // says anything about what is left.
        r.rate = 0.0;
        r.rateAt = now;
        r.rateDone = value;
    } else if (value > r.rateDone && r.rateAt > 0 && now > r.rateAt) {
        const double sample =
            double(value - r.rateDone) / double(now - r.rateAt);
        r.rate = r.rate > 0.0
                     ? Tasks::kRateAlpha * sample
                           + (1.0 - Tasks::kRateAlpha) * r.rate
                     : sample;
        // Only moved when the count moved: a stall has to stretch the next
        // sample's window, or the estimate would quietly ignore it.
        r.rateAt = now;
        r.rateDone = value;
    }
    r.done = value;

    d->m()->touch(i, {TaskListModel::DoneRole, TaskListModel::TotalRole,
                      TaskListModel::ProgressRole, TaskListModel::EtaMsRole,
                      TaskListModel::ElapsedMsRole});
    emit countsChanged();   // overallProgress moved with it
}

void TaskManager::setNote(const QString &id, const QString &note)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return;
    Record &r = d->rows()[i];
    if (!r.active() || r.note == note)
        return;
    r.note = note;
    d->m()->touch(i, {TaskListModel::NoteRole});
}

void TaskManager::finish(const QString &id, bool ok, const QString &error)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return;
    Record &r = d->rows()[i];
    // Already over: canceled underneath the service, or answered twice.
    if (!r.active())
        return;

    const QString err = ok ? QString() : error;
    r.state = ok ? State::Succeeded : State::Failed;
    r.finishedAt = QDateTime::currentDateTimeUtc();
    r.error = err;
    // A bar that stops at 9 of 10 on a task that worked reads as a hang.
    if (ok && r.total > 0)
        r.done = r.total;
    // Whatever the callbacks captured -- a service, a reply, a page of text
    // -- has no reason to stay alive for the life of the row.
    r.start = nullptr;
    r.stop = nullptr;
    // Nothing is being worked on any more, so nothing is spoken for.
    d->dropClaims(id);

    d->m()->touch(i);
    d->ensureTicker();
    emit countsChanged();
    emit taskFinished(id, ok, err);
    d->pump();
}

void TaskManager::markCanceled(const QString &id)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return;
    Record &r = d->rows()[i];
    if (!r.active())
        return;

    r.state = State::Canceled;
    r.finishedAt = QDateTime::currentDateTimeUtc();
    r.error.clear();
    r.start = nullptr;
    r.stop = nullptr;
    d->dropClaims(id);

    d->m()->touch(i);
    d->ensureTicker();
    emit countsChanged();
    emit taskFinished(id, false, QString());
    d->pump();
}

void TaskManager::claim(const QString &taskId, const QString &exclusionKey)
{
    if (taskId.isEmpty() || exclusionKey.isEmpty())
        return;
    // Somebody else is on it. There is nothing to report: a caller finds out
    // that work is spoken for by submitting and being told no, not by asking
    // here, and a claim that quietly does not happen is the same answer.
    const QString holder = d->claimHolder(exclusionKey);
    if (!holder.isEmpty() && holder != taskId)
        return;
    // A set, so a task that claims the same paper twice -- it came back to
    // it, or it never let go -- costs nothing and releases in one go.
    d->claims[taskId].insert(exclusionKey);
}

void TaskManager::releaseClaim(const QString &taskId, const QString &exclusionKey)
{
    const auto it = d->claims.find(taskId);
    if (it == d->claims.end())
        return;
    it->remove(exclusionKey);
    // An empty set would keep answering claimHolder() with nothing while
    // still counting as an entry; it is simply gone instead.
    if (it->isEmpty())
        d->claims.erase(it);
}

bool TaskManager::isActive(const QString &kindKey, const QString &paperId) const
{
    return !activeId(kindKey, paperId).isEmpty();
}

QString TaskManager::activeId(const QString &kindKey,
                              const QString &paperId) const
{
    for (const Record &r : d->rows()) {
        if (!r.active())
            continue;
        if (Tasks::kindKey(r.request.kind) != kindKey)
            continue;
        if (r.request.paperId != paperId)
            continue;
        return r.id;
    }
    return {};
}

void TaskManager::cancel(const QString &id)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return;
    Record &r = d->rows()[i];
    if (!r.active())
        return;

    const bool wasRunning = r.state == State::Running;
    const std::function<void()> stop = r.stop;
    r.state = State::Canceled;
    r.finishedAt = QDateTime::currentDateTimeUtc();
    r.start = nullptr;
    r.stop = nullptr;
    // Dropped before stop() runs: a service that reacts by submitting the
    // work it was told to abandon must not be turned away by the dead task's
    // own claim. cancelAll() ends every task through here, so it needs
    // nothing of its own.
    d->dropClaims(id);

    // Marked first, told second, and the row is not waited on: a service
    // that answers finish() from inside its own stop() finds a task that is
    // already over, which is exactly what makes a late answer harmless.
    if (wasRunning && stop)
        stop();

    d->m()->touch(i);
    d->ensureTicker();
    emit countsChanged();
    // A cancel ends the task as surely as a failure does, and anything
    // waiting on taskFinished to start the next piece of work would hang
    // otherwise. The empty error is what tells the two apart.
    emit taskFinished(id, false, QString());
    d->pump();
}

void TaskManager::cancelAll()
{
    // Queued first: they are the cheapest to drop, and stopping the running
    // ones would otherwise free a slot for one of them to start into.
    QStringList ids;
    for (const Record &r : d->rows()) {
        if (r.state == State::Queued)
            ids.append(r.id);
    }
    for (const Record &r : d->rows()) {
        if (r.state == State::Running)
            ids.append(r.id);
    }
    // By id rather than by index: cancelling one runs a service's stop
    // callback, which is free to submit, finish or clear anything it likes.
    for (const QString &id : std::as_const(ids))
        cancel(id);
}

void TaskManager::clearFinished()
{
    // Backwards, so the indices ahead of the one being removed stay put.
    for (int i = int(d->rows().size()) - 1; i >= 0; --i) {
        const State s = d->rows().at(i).state;
        if (s == State::Succeeded || s == State::Failed || s == State::Canceled)
            d->m()->removeAt(i);
    }
    emit countsChanged();
}

bool TaskManager::retry(const QString &id)
{
    const int i = d->indexOfId(id);
    if (i < 0)
        return false;
    const Record &r = d->rows().at(i);
    if (r.state != State::Failed && r.state != State::Canceled)
        return false;
    if (r.request.resume.isEmpty())
        return false;
    const auto it = d->resumers.constFind(Tasks::kindKey(r.request.kind));
    if (it == d->resumers.constEnd())
        return false;

    // Copied out before the call: the resumer submits, and submitting can
    // move the vector under a reference into it.
    const std::function<bool(const QJsonObject &)> resumer = it.value();
    const QJsonObject payload = r.request.resume;
    // Handed straight back. A resumer says no when it cannot start the work
    // -- the paper is not the one on screen -- and swallowing that would
    // leave a Retry button that looks like it did something.
    return resumer(payload);
}

int TaskManager::saveInterrupted()
{
    QVector<Tasks::PendingTask> out;
    for (int i = 0; i < d->rows().size(); ++i) {
        Record &r = d->rows()[i];
        // Without a resume payload there is nothing to offer back, so the
        // task simply ends with the session.
        if (!r.active() || r.request.resume.isEmpty())
            continue;
        r.state = State::Interrupted;
        r.finishedAt = QDateTime::currentDateTimeUtc();

        Tasks::PendingTask p;
        p.id = r.id;
        p.request = r.request;
        p.done = r.done;
        out.append(p);
        d->dropClaims(r.id);
        d->m()->touch(i);
    }
    d->ticker.stop();

    // An empty save is a delete: whatever an older session left is answered
    // by this one, and offering it twice would be worse than losing it.
    if (out.isEmpty())
        TaskStore::clear();
    else
        TaskStore::save(out);
    emit countsChanged();
    return int(out.size());
}

QVariantList TaskManager::pending() const
{
    QVariantList out;
    out.reserve(d->pending.size());
    for (const Tasks::PendingTask &p : d->pending) {
        out.append(QVariantMap{
            {QStringLiteral("id"), p.id},
            {QStringLiteral("kind"), Tasks::kindKey(p.request.kind)},
            {QStringLiteral("kindLabel"), Tasks::kindLabel(p.request.kind)},
            {QStringLiteral("title"), p.request.title},
            {QStringLiteral("paperTitle"), p.request.paperTitle},
            {QStringLiteral("done"), p.done},
            {QStringLiteral("steps"), p.request.steps}});
    }
    return out;
}

void TaskManager::resumePending()
{
    // Tried once, here and now. Most of it will refuse: every per-paper
    // resumer wants its paper open with its paragraphs loaded, and at
    // startup nothing is open yet. What refuses is kept, and the file is
    // rewritten to say exactly that -- clearing it whatever happened was the
    // old bug, and it lost the work silently.
    d->resumeWhatCan();
    d->writePending();
    emit pendingChanged();

    // Nothing in here can open a paper -- main.cpp can -- so the manager
    // asks for the first one that is still owed and waits to be told, through
    // retryPending(), that it has loaded. Without an opener the entries
    // simply stay on the list until the reader opens the paper themselves.
    d->askForNextPaper();
}

void TaskManager::retryPending()
{
    if (d->pending.isEmpty())
        return;
    // Silent when nothing started: this runs on every paper that finishes
    // loading, and rewriting a file and repainting a dialog to say what they
    // already said is work for nothing.
    if (d->resumeWhatCan()) {
        d->writePending();
        emit pendingChanged();
    }
    d->askForNextPaper();
}

void TaskManager::discardPending()
{
    d->pending.clear();
    TaskStore::clear();
    emit pendingChanged();
}

void TaskManager::setPaperOpener(
    std::function<void(const QString &paperId)> opener)
{
    d->paperOpenerFn = std::move(opener);
}

void TaskManager::setContext(std::function<QString()> currentProjectId,
                             std::function<QString(const QString &)> paperTitleFor)
{
    d->projectIdFn = std::move(currentProjectId);
    d->paperTitleFn = std::move(paperTitleFor);
}

void TaskManager::registerResumer(
    Tasks::Kind kind, std::function<bool(const QJsonObject &)> resumer)
{
    const QString key = Tasks::kindKey(kind);
    d->resumers.insert(key, std::move(resumer));
    // The Retry button asks the model, and the model only knows a task can
    // be restarted because a resumer said so.
    d->m()->resumableKinds.insert(key);
}
