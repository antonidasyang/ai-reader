// The one queue, driven for real.
//
// The promises under test are the ones a reader would notice breaking: work
// that is submitted actually starts, one paper is never worked on twice at
// once, two different papers do not block each other, no more than the
// budget runs at a time and the rest wait their turn, the bar and the
// estimate mean something, cancel really stops the work and a late answer
// cannot undo it, a failure keeps its reason, and nothing that was in flight
// is lost when the app closes -- it is offered back on the next launch,
// except the work that could not be restarted anyway, which is never
// promised back.
//
// Isolation. Everything this writes goes under a throwaway app-data root
// (Qt test mode plus a harness-only organization) and, for QSettings, an Ini
// file under the system temp directory. The second part is not paranoia:
// QSettings' native backend on macOS is cfprefsd, which setTestModeEnabled
// does NOT redirect, and a harness that writes there is writing the
// developer's real preferences -- which has already cost someone their
// settings once in this project. Both roots are wiped at the START of a run,
// never at the end, so a failure can be picked over afterwards.

#include "Settings.h"
#include "TaskListModel.h"
#include "TaskManager.h"
#include "TaskTypes.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <memory>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

// The manager calls back through the event loop, so nothing here may assert
// on a callback without giving the loop a turn first.
static void pump(int ms = 30)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

template <typename F>
static bool waitFor(F cond, int ms = 3000)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
        if (cond())
            return true;
        pump(10);
    }
    return cond();
}

// ── reading the model the way the viewer does ───────────────────────

static QVariant roleAt(TaskListModel *model, int row, int role)
{
    if (!model || row < 0 || row >= model->rowCount())
        return {};
    return model->data(model->index(row), role);
}

static int rowFor(TaskListModel *model, const QString &id)
{
    if (!model || id.isEmpty())
        return -1;
    for (int i = 0; i < model->rowCount(); ++i)
        if (roleAt(model, i, TaskListModel::IdRole).toString() == id)
            return i;
    return -1;
}

// -1 means "no such row". The role carries a Tasks::State; a key string is
// accepted too, since stateFromKey() is part of the same vocabulary.
static int stateOf(TaskManager &tm, const QString &id)
{
    const int row = rowFor(tm.model(), id);
    if (row < 0)
        return -1;
    const QVariant v = roleAt(tm.model(), row, TaskListModel::StateRole);
    if (v.metaType().id() == QMetaType::QString)
        return static_cast<int>(Tasks::stateFromKey(v.toString()));
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok ? n : -1;
}

static int stateInt(Tasks::State s) { return static_cast<int>(s); }

static QString stateName(int n)
{
    if (n < 0)
        return QStringLiteral("<no row>");
    return Tasks::stateKey(static_cast<Tasks::State>(n));
}

static QString whereIs(TaskManager &tm, const QString &id)
{
    return QStringLiteral("state=%1").arg(stateName(stateOf(tm, id)));
}

// ── submitting ──────────────────────────────────────────────────────

// One submitted task and what the manager did to it. Held by shared_ptr so
// the callbacks the manager keeps stay valid for as long as it keeps them.
struct Job {
    QString id;
    QString label;
    int starts = 0;
    int stops = 0;
    bool startedBeforePump = false;
};
using JobPtr = std::shared_ptr<Job>;

static Tasks::Request request(Tasks::Kind kind, const QString &title,
                              const QString &paperId, const QString &paperTitle,
                              int steps = 0, const QJsonObject &resume = QJsonObject())
{
    Tasks::Request r;
    r.kind = kind;
    r.title = title;
    r.paperId = paperId;
    r.paperTitle = paperTitle;
    r.steps = steps;
    r.resume = resume;
    return r;
}

static JobPtr submit(TaskManager &tm, const Tasks::Request &req)
{
    JobPtr job = std::make_shared<Job>();
    job->label = req.paperTitle.isEmpty() ? req.title
                                          : req.title + " / " + req.paperTitle;
    job->id = tm.submit(req,
                        [job] { job->starts++; },
                        [job] { job->stops++; });
    job->startedBeforePump = job->starts > 0;
    return job;
}

// Leaves a manager with nothing queued, nothing running and no rows, so the
// next block starts from the same place this one did.
static void drain(TaskManager &tm)
{
    tm.cancelAll();
    waitFor([&tm] { return tm.runningCount() == 0 && tm.queuedCount() == 0; }, 2000);
    tm.clearFinished();
    pump(20);
}

// ── looking at what was written ─────────────────────────────────────

// Every scalar inside a QVariantList of maps, flattened, so a check can ask
// "is this title in there anywhere" without guessing the key names.
static void collectStrings(const QVariant &v, QStringList &out)
{
    switch (v.metaType().id()) {
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        const QVariantList list = v.toList();
        for (const QVariant &e : list)
            collectStrings(e, out);
        break;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = v.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            out << it.key();
            collectStrings(it.value(), out);
        }
        break;
    }
    case QMetaType::QVariantHash: {
        const QVariantHash hash = v.toHash();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
            out << it.key();
            collectStrings(it.value(), out);
        }
        break;
    }
    case QMetaType::QJsonObject:
        out << QString::fromUtf8(
            QJsonDocument(v.toJsonObject()).toJson(QJsonDocument::Compact));
        break;
    case QMetaType::QJsonArray:
        out << QString::fromUtf8(
            QJsonDocument(v.toJsonArray()).toJson(QJsonDocument::Compact));
        break;
    case QMetaType::QJsonValue: {
        const QJsonValue jv = v.toJsonValue();
        if (jv.isObject())
            out << QString::fromUtf8(
                QJsonDocument(jv.toObject()).toJson(QJsonDocument::Compact));
        else if (jv.isArray())
            out << QString::fromUtf8(
                QJsonDocument(jv.toArray()).toJson(QJsonDocument::Compact));
        else
            out << jv.toVariant().toString();
        break;
    }
    default:
        out << v.toString();
    }
}

static QString flatten(const QVariant &v)
{
    QStringList parts;
    collectStrings(v, parts);
    return parts.join(QStringLiteral(" | "));
}

// The store picks its own file name; a check should not have to know it.
static QString fileContaining(const QString &root, const QString &needle)
{
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (f.size() > 4 * 1024 * 1024)
            continue;
        if (!f.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8(f.readAll()).contains(needle))
            return path;
    }
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ai-reader-harness"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("harness.local"));
    QCoreApplication::setApplicationName(QStringLiteral("TasksHarness"));

    // Directories go under the Qt test root. Settings need their own
    // isolation on top of that, because setTestModeEnabled does not redirect
    // cfprefsd: Ini format under a scratch path keeps every platform in a
    // directory this harness owns.
    QStandardPaths::setTestModeEnabled(true);
    const QString settingsRoot =
        QDir::tempPath() + QStringLiteral("/ai-reader-tasks-harness");
    QDir(settingsRoot).removeRecursively();
    QDir().mkpath(settingsRoot);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);

    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataRoot).removeRecursively();
    QDir().mkpath(dataRoot);
    { QSettings stale; stale.clear(); stale.sync(); }

    Settings settings;

    // ── A submitted task starts ──────────────────────────────────────
    {
        int finishedSignals = 0;
        bool finishedOk = false;
        QString finishedId;
        TaskManager tm(&settings);
        QObject::connect(&tm, &TaskManager::taskFinished,
                         [&](const QString &id, bool ok, const QString &) {
                             ++finishedSignals;
                             finishedOk = ok;
                             finishedId = id;
                         });

        JobPtr job = submit(tm, request(Tasks::Kind::Segment,
                                        QStringLiteral("Segment"),
                                        QStringLiteral("paper-start-1"),
                                        QStringLiteral("A paper worth splitting")));
        check("a submitted task is handed an id to be known by",
              !job->id.isEmpty());

        const bool started = waitFor([&job] { return job->starts == 1; });
        check("a submitted task is actually started by the manager", started,
              job->startedBeforePump
                  ? QStringLiteral("started inside submit()")
                  : QStringLiteral("started from the event loop, as expected"));
        check("a started task shows as Running in the model",
              stateOf(tm, job->id) == stateInt(Tasks::State::Running)
                  && tm.runningCount() == 1,
              QStringLiteral("%1, running=%2")
                  .arg(whereIs(tm, job->id))
                  .arg(tm.runningCount()));

        tm.finish(job->id, true);
        waitFor([&tm] { return tm.runningCount() == 0; });
        check("a task that finishes cleanly ends up Succeeded",
              stateOf(tm, job->id) == stateInt(Tasks::State::Succeeded)
                  && tm.runningCount() == 0 && tm.finishedCount() == 1,
              QStringLiteral("%1, finished=%2")
                  .arg(whereIs(tm, job->id))
                  .arg(tm.finishedCount()));
        check("finishing a task tells everyone who was listening",
              finishedSignals == 1 && finishedOk && finishedId == job->id,
              QStringLiteral("%1 signal(s)").arg(finishedSignals));
        drain(tm);
    }

    // ── One paper is never worked on twice at once ───────────────────
    {
        TaskManager tm(&settings);
        const QString paper = QStringLiteral("paper-exclusive-2");
        JobPtr first = submit(tm, request(Tasks::Kind::Translate,
                                          QStringLiteral("Translate"), paper,
                                          QStringLiteral("The contested paper")));
        waitFor([&first] { return first->starts == 1; });

        JobPtr second = submit(tm, request(Tasks::Kind::Translate,
                                           QStringLiteral("Translate"), paper,
                                           QStringLiteral("The contested paper")));
        pump(80);

        check("a second run of the same work on the same paper is refused",
              second->id.isEmpty(),
              second->id.isEmpty() ? QString()
                                   : QStringLiteral("got id %1").arg(second->id));
        check("the refused task is never started", second->starts == 0,
              QStringLiteral("%1 start(s)").arg(second->starts));
        check("the task already on that paper is left untouched",
              first->starts == 1 && first->stops == 0
                  && stateOf(tm, first->id) == stateInt(Tasks::State::Running),
              QStringLiteral("starts=%1 stops=%2 %3")
                  .arg(first->starts)
                  .arg(first->stops)
                  .arg(whereIs(tm, first->id)));
        check("the manager can say which task owns that paper",
              tm.isActive(Tasks::kindKey(Tasks::Kind::Translate), paper)
                  && tm.activeId(Tasks::kindKey(Tasks::Kind::Translate), paper)
                         == first->id,
              tm.activeId(Tasks::kindKey(Tasks::Kind::Translate), paper));
        drain(tm);
    }

    // ── Papers do not block each other ───────────────────────────────
    {
        TaskManager tm(&settings);
        JobPtr a = submit(tm, request(Tasks::Kind::Translate,
                                      QStringLiteral("Translate"),
                                      QStringLiteral("paper-parallel-a"),
                                      QStringLiteral("First of two papers")));
        JobPtr b = submit(tm, request(Tasks::Kind::Translate,
                                      QStringLiteral("Translate"),
                                      QStringLiteral("paper-parallel-b"),
                                      QStringLiteral("Second of two papers")));
        check("the same work on another paper is admitted, not refused",
              !b->id.isEmpty() && b->id != a->id);

        waitFor([&a] { return a->starts == 1; });
        pump(80);
        QString how;
        bool bothRan = a->starts == 1 && b->starts == 1;
        if (bothRan) {
            how = QStringLiteral("both running at once");
        } else {
            // A budget of one is still not one paper blocking another: the
            // second must run the moment a slot frees.
            tm.finish(a->id, true);
            waitFor([&b] { return b->starts == 1; });
            how = QStringLiteral("budget of 1 here; the second started as soon "
                                 "as the first was done");
        }
        check("two papers are worked on side by side rather than one blocking "
              "the other",
              a->starts == 1 && b->starts == 1, how);
        drain(tm);
    }

    // ── The budget holds ─────────────────────────────────────────────
    const int kSubmitted = 10;
    int cap = 0;
    {
        int highWater = 0;
        TaskManager tm(&settings);
        QObject::connect(&tm, &TaskManager::countsChanged, [&] {
            highWater = qMax(highWater, tm.runningCount());
        });

        QVector<JobPtr> jobs;
        for (int i = 0; i < kSubmitted; ++i)
            jobs << submit(tm, request(Tasks::Kind::QuickInterpret,
                                       QStringLiteral("Interpret"),
                                       QStringLiteral("paper-cap-%1").arg(i),
                                       QStringLiteral("Paper %1 of the queue").arg(i)));
        pump(120);
        cap = tm.runningCount();
        highWater = qMax(highWater, cap);

        check("no more tasks run at once than the budget allows",
              cap >= 1 && cap < kSubmitted,
              QStringLiteral("%1 of %2 submitted are running, %3 queued")
                  .arg(cap)
                  .arg(kSubmitted)
                  .arg(tm.queuedCount()));

        bool orderHeld = tm.queuedCount() == kSubmitted - cap;
        QString orderDetail;
        for (int i = 0; i < kSubmitted; ++i) {
            const int want = i < cap ? stateInt(Tasks::State::Running)
                                     : stateInt(Tasks::State::Queued);
            const int got = stateOf(tm, jobs[i]->id);
            if (got != want || (i >= cap && jobs[i]->starts != 0)) {
                orderHeld = false;
                if (orderDetail.isEmpty())
                    orderDetail = QStringLiteral("#%1 is %2, wanted %3")
                                      .arg(i)
                                      .arg(stateName(got))
                                      .arg(stateName(want));
            }
        }
        check("the tasks over the budget wait in the order they were submitted",
              orderHeld,
              orderDetail.isEmpty()
                  ? QStringLiteral("%1 running, %2 queued in order")
                        .arg(cap)
                        .arg(tm.queuedCount())
                  : orderDetail);

        // Drain it one at a time: each one that finishes should let in the
        // next in line, and never a second one alongside it.
        bool admittedInOrder = true;
        bool capNeverExceeded = true;
        QString drainDetail;
        for (int i = 0; i < kSubmitted; ++i) {
            tm.finish(jobs[i]->id, true);
            const int next = i + cap;
            if (next < kSubmitted)
                waitFor([&jobs, next] { return jobs[next]->starts == 1; }, 2000);
            else
                pump(40);
            if (tm.runningCount() > cap) {
                capNeverExceeded = false;
                if (drainDetail.isEmpty())
                    drainDetail = QStringLiteral("%1 running after #%2 finished")
                                      .arg(tm.runningCount())
                                      .arg(i);
            }
            if (next < kSubmitted && jobs[next]->starts != 1) {
                admittedInOrder = false;
                if (drainDetail.isEmpty())
                    drainDetail = QStringLiteral("#%1 did not start when #%2 finished")
                                      .arg(next)
                                      .arg(i);
            }
            // Nothing further down the queue may jump ahead of it.
            for (int j = next + 1; j < kSubmitted; ++j) {
                if (jobs[j]->starts != 0) {
                    admittedInOrder = false;
                    if (drainDetail.isEmpty())
                        drainDetail = QStringLiteral("#%1 jumped the queue").arg(j);
                }
            }
        }
        check("a task that finishes lets in exactly the next one in line",
              admittedInOrder,
              drainDetail.isEmpty() ? QStringLiteral("all %1 ran in order")
                                          .arg(kSubmitted)
                                    : drainDetail);
        check("the budget is never exceeded while the queue drains",
              capNeverExceeded && highWater <= cap,
              QStringLiteral("most seen running at once: %1 (budget %2)")
                  .arg(highWater)
                  .arg(cap));
        drain(tm);
    }

    // ── Progress, and how long it has left ───────────────────────────
    {
        TaskManager tm(&settings);
        JobPtr counted = submit(tm, request(Tasks::Kind::Translate,
                                            QStringLiteral("Translate"),
                                            QStringLiteral("paper-progress-1"),
                                            QStringLiteral("A paper of 100 paragraphs"),
                                            100));
        waitFor([&counted] { return counted->starts == 1; });

        // A first report right on top of the start would measure a rate off
        // a millisecond or two of clock; give it a window of its own first.
        pump(60);
        QElapsedTimer sinceFirstReport;
        tm.setProgress(counted->id, 10);
        sinceFirstReport.start();
        pump(90);                       // a real gap, so a real rate comes out
        tm.setProgress(counted->id, 20);
        const qint64 gapMs = qMax<qint64>(1, sinceFirstReport.elapsed());
        pump(40);

        const int row = rowFor(tm.model(), counted->id);
        const double progress =
            roleAt(tm.model(), row, TaskListModel::ProgressRole).toDouble();
        const int done = roleAt(tm.model(), row, TaskListModel::DoneRole).toInt();
        const int total = roleAt(tm.model(), row, TaskListModel::TotalRole).toInt();
        check("progress a task reports comes back as a fraction of the whole",
              qAbs(progress - 0.2) < 0.001 && done == 20 && total == 100,
              QStringLiteral("progress=%1 done=%2 total=%3")
                  .arg(progress)
                  .arg(done)
                  .arg(total));

        const qint64 eta =
            roleAt(tm.model(), row, TaskListModel::EtaMsRole).toLongLong();
        check("two progress reports a moment apart give an estimate of time "
              "still to come",
              eta > 0, QStringLiteral("eta=%1 ms after %2 ms of work")
                           .arg(eta)
                           .arg(gapMs));
        // 80 steps left at the 10-steps-per-gap it just managed. Only the
        // order of magnitude is asserted -- the rate is measured off a real
        // clock and the manager may average it differently.
        const qint64 expected = gapMs * 8;
        check("...and the estimate is about the work left over the rate observed",
              eta > expected / 16 && eta < expected * 16,
              QStringLiteral("eta=%1 ms, back-of-envelope %2 ms")
                  .arg(eta)
                  .arg(expected));
        tm.finish(counted->id, true);

        JobPtr uncounted = submit(tm, request(Tasks::Kind::Vision,
                                              QStringLiteral("Read the page"),
                                              QStringLiteral("paper-progress-2"),
                                              QStringLiteral("A paper of unknown length")));
        waitFor([&uncounted] { return uncounted->starts == 1; });
        pump(40);
        const int row2 = rowFor(tm.model(), uncounted->id);
        check("a task that cannot say how much there is to do reports no progress",
              roleAt(tm.model(), row2, TaskListModel::ProgressRole).toDouble() < 0,
              QStringLiteral("progress=%1")
                  .arg(roleAt(tm.model(), row2, TaskListModel::ProgressRole)
                           .toDouble()));
        check("...and offers no estimate rather than a made-up one",
              roleAt(tm.model(), row2, TaskListModel::EtaMsRole).toLongLong() < 0,
              QStringLiteral("eta=%1")
                  .arg(roleAt(tm.model(), row2, TaskListModel::EtaMsRole)
                           .toLongLong()));
        drain(tm);
    }

    // ── Cancelling ───────────────────────────────────────────────────
    {
        TaskManager tm(&settings);
        const int fill = qBound(1, cap, 8);
        QVector<JobPtr> jobs;
        for (int i = 0; i <= fill; ++i)     // one more than fits
            jobs << submit(tm, request(Tasks::Kind::DeepInterpret,
                                       QStringLiteral("Read closely"),
                                       QStringLiteral("paper-cancel-%1").arg(i),
                                       QStringLiteral("Paper %1 to be cancelled").arg(i)));
        pump(120);

        JobPtr queued = jobs.last();
        tm.cancel(queued->id);
        pump(80);
        check("cancelling a task that never got its turn never starts it",
              queued->starts == 0
                  && stateOf(tm, queued->id) == stateInt(Tasks::State::Canceled),
              QStringLiteral("starts=%1 %2")
                  .arg(queued->starts)
                  .arg(whereIs(tm, queued->id)));

        JobPtr running = jobs.first();
        tm.cancel(running->id);
        waitFor([&running] { return running->stops == 1; });
        check("cancelling a running task asks it to stop",
              running->stops == 1
                  && stateOf(tm, running->id) == stateInt(Tasks::State::Canceled),
              QStringLiteral("stops=%1 %2")
                  .arg(running->stops)
                  .arg(whereIs(tm, running->id)));

        // The work was already in the air; its answer arrives after the fact.
        tm.finish(running->id, true);
        pump(60);
        check("an answer that arrives after a cancel does not bring the task back",
              stateOf(tm, running->id) == stateInt(Tasks::State::Canceled)
                  && running->stops == 1,
              whereIs(tm, running->id));
        drain(tm);
    }

    // ── Failing ──────────────────────────────────────────────────────
    {
        TaskManager tm(&settings);
        JobPtr job = submit(tm, request(Tasks::Kind::Toc,
                                        QStringLiteral("Read the contents"),
                                        QStringLiteral("paper-failure-1"),
                                        QStringLiteral("A paper the model choked on")));
        waitFor([&job] { return job->starts == 1; });
        const QString reason =
            QStringLiteral("the gateway returned 502 before any contents came back");
        tm.finish(job->id, false, reason);
        waitFor([&tm] { return tm.runningCount() == 0; });

        const int row = rowFor(tm.model(), job->id);
        check("a task that failed says so, and keeps the reason on its row",
              stateOf(tm, job->id) == stateInt(Tasks::State::Failed)
                  && roleAt(tm.model(), row, TaskListModel::ErrorRole).toString()
                         == reason,
              QStringLiteral("%1, error=\"%2\"")
                  .arg(whereIs(tm, job->id))
                  .arg(roleAt(tm.model(), row, TaskListModel::ErrorRole).toString()));
        check("a task that failed is no longer active, and cannot be cancelled",
              tm.runningCount() == 0 && tm.queuedCount() == 0
                  && tm.finishedCount() == 1
                  && !roleAt(tm.model(), row, TaskListModel::CanCancelRole).toBool(),
              QStringLiteral("running=%1 queued=%2 finished=%3 canCancel=%4")
                  .arg(tm.runningCount())
                  .arg(tm.queuedCount())
                  .arg(tm.finishedCount())
                  .arg(roleAt(tm.model(), row, TaskListModel::CanCancelRole).toBool()));
        drain(tm);
    }

    // ── What the viewer reads ────────────────────────────────────────
    {
        TaskManager tm(&settings);
        TaskListModel *model = tm.model();
        check("the viewer is given a model to read", model != nullptr);

        JobPtr older = submit(tm, request(Tasks::Kind::Translate,
                                          QStringLiteral("Translate"),
                                          QStringLiteral("paper-model-1"),
                                          QStringLiteral("The paper submitted first")));
        pump(40);
        JobPtr newer = submit(tm, request(Tasks::Kind::Translate,
                                          QStringLiteral("Translate"),
                                          QStringLiteral("paper-model-2"),
                                          QStringLiteral("The paper submitted second")));
        pump(40);
        check("the task that just started is the first row",
              roleAt(model, 0, TaskListModel::IdRole).toString() == newer->id
                  && roleAt(model, 1, TaskListModel::IdRole).toString() == older->id,
              QStringLiteral("row 0 is \"%1\"")
                  .arg(roleAt(model, 0, TaskListModel::PaperTitleRole).toString()));

        JobPtr third = submit(tm, request(Tasks::Kind::Translate,
                                          QStringLiteral("Translate"),
                                          QStringLiteral("paper-model-3"),
                                          QStringLiteral("The paper submitted third")));
        pump(40);
        check("the row count follows every submit",
              model->rowCount() == 3,
              QStringLiteral("%1 rows for 3 submits").arg(model->rowCount()));

        tm.finish(third->id, true);
        waitFor([&tm] { return tm.finishedCount() == 1; });
        tm.clearFinished();
        pump(40);
        check("clearing finished work takes those rows away and leaves the rest",
              model->rowCount() == 2 && rowFor(model, third->id) < 0
                  && rowFor(model, older->id) >= 0 && rowFor(model, newer->id) >= 0,
              QStringLiteral("%1 rows left").arg(model->rowCount()));
        drain(tm);
    }

    // ── Closing with work in flight, and opening again ───────────────
    const QString kAlphaPaperId = QStringLiteral("paper-interrupted-alpha");
    const QString kAlphaTitle = QStringLiteral("Billows in a shear flow");
    const QString kBetaPaperId = QStringLiteral("paper-interrupted-beta");
    const QString kBetaTitle = QStringLiteral("Sediment on a cold shelf");
    QString pendingFile;
    {
        TaskManager closing(&settings);
        closing.discardPending();       // whatever earlier blocks left behind
        pump(20);

        QJsonObject alphaResume;
        alphaResume.insert(QStringLiteral("paperId"), kAlphaPaperId);
        alphaResume.insert(QStringLiteral("fromParagraph"), 12);
        QJsonObject betaResume;
        betaResume.insert(QStringLiteral("paperId"), kBetaPaperId);
        betaResume.insert(QStringLiteral("module"), QStringLiteral("evidence"));

        JobPtr alpha = submit(closing, request(Tasks::Kind::Translate,
                                               QStringLiteral("Translate"),
                                               kAlphaPaperId, kAlphaTitle, 40,
                                               alphaResume));
        JobPtr beta = submit(closing, request(Tasks::Kind::DeepInterpret,
                                              QStringLiteral("Read closely"),
                                              kBetaPaperId, kBetaTitle, 9,
                                              betaResume));
        pump(100);

        const int written = closing.saveInterrupted();
        check("closing with two tasks in flight writes both of them down",
              written == 2,
              QStringLiteral("saveInterrupted() returned %1; alpha %2, beta %3")
                  .arg(written)
                  .arg(whereIs(closing, alpha->id))
                  .arg(whereIs(closing, beta->id)));

        pendingFile = fileContaining(dataRoot, kAlphaPaperId);
        check("...to a file on disk, not just to memory", !pendingFile.isEmpty(),
              pendingFile.isEmpty() ? QStringLiteral("nothing under %1 mentions %2")
                                          .arg(dataRoot, kAlphaPaperId)
                                    : pendingFile);

        // The next launch, while the old manager is still standing: it must
        // read what was written, not what is in the other object's head.
        {
            TaskManager opening(&settings);
            check("the next launch finds both interrupted tasks waiting",
                  opening.pendingCount() == 2,
                  QStringLiteral("pendingCount()=%1").arg(opening.pendingCount()));

            const QString offered = flatten(QVariant(opening.pending()));
            check("...and can still say what they were",
                  offered.contains(kAlphaTitle) && offered.contains(kBetaTitle),
                  offered.isEmpty() ? QStringLiteral("pending() is empty") : offered);

            int alphaResumed = 0, betaResumed = 0;
            QJsonObject alphaSeen, betaSeen;
            opening.registerResumer(Tasks::Kind::Translate,
                                    [&](const QJsonObject &payload) {
                                        ++alphaResumed;
                                        alphaSeen = payload;
                                        return true;
                                    });
            opening.registerResumer(Tasks::Kind::DeepInterpret,
                                    [&](const QJsonObject &payload) {
                                        ++betaResumed;
                                        betaSeen = payload;
                                        return true;
                                    });
            opening.resumePending();
            waitFor([&] { return alphaResumed == 1 && betaResumed == 1; });
            check("resuming hands each task's payload back to its own kind's "
                  "resumer",
                  alphaResumed == 1 && betaResumed == 1
                      && alphaSeen.value(QStringLiteral("fromParagraph")).toInt() == 12
                      && betaSeen.value(QStringLiteral("module")).toString()
                             == QStringLiteral("evidence"),
                  QStringLiteral("translate resumer x%1 %2, deep resumer x%3 %4")
                      .arg(alphaResumed)
                      .arg(QString::fromUtf8(
                          QJsonDocument(alphaSeen).toJson(QJsonDocument::Compact)))
                      .arg(betaResumed)
                      .arg(QString::fromUtf8(
                          QJsonDocument(betaSeen).toJson(QJsonDocument::Compact))));
            drain(opening);
        }
        drain(closing);
    }

    // ── Throwing away what was interrupted ───────────────────────────
    {
        TaskManager closing(&settings);
        closing.discardPending();
        pump(20);

        QJsonObject resume;
        resume.insert(QStringLiteral("paperId"), kAlphaPaperId);
        submit(closing, request(Tasks::Kind::Translate, QStringLiteral("Translate"),
                                kAlphaPaperId, kAlphaTitle, 40, resume));
        QJsonObject resume2;
        resume2.insert(QStringLiteral("paperId"), kBetaPaperId);
        submit(closing, request(Tasks::Kind::DeepInterpret,
                                QStringLiteral("Read closely"), kBetaPaperId,
                                kBetaTitle, 9, resume2));
        pump(100);
        const int written = closing.saveInterrupted();

        {
            TaskManager opening(&settings);
            const int before = opening.pendingCount();
            opening.discardPending();
            pump(40);
            check("throwing away what was interrupted leaves nothing pending",
                  before == 2 && opening.pendingCount() == 0,
                  QStringLiteral("%1 written, %2 offered, %3 left")
                      .arg(written)
                      .arg(before)
                      .arg(opening.pendingCount()));
            check("...and takes the file with it",
                  fileContaining(dataRoot, kAlphaPaperId).isEmpty(),
                  fileContaining(dataRoot, kAlphaPaperId));
            drain(opening);
        }
        drain(closing);
    }

    // ── Work that could not be restarted is not promised back ────────
    {
        const QString restartable = QStringLiteral("A paper that can be picked up again");
        const QString oneShot = QStringLiteral("A paper that cannot be picked up again");
        TaskManager closing(&settings);
        closing.discardPending();
        pump(20);

        QJsonObject resume;
        resume.insert(QStringLiteral("paperId"), QStringLiteral("paper-resumable"));
        submit(closing, request(Tasks::Kind::Translate, QStringLiteral("Translate"),
                                QStringLiteral("paper-resumable"), restartable, 40,
                                resume));
        // No resume payload: nothing on disk could start this again, so
        // offering it back on the next launch would be a promise the app
        // cannot keep.
        submit(closing, request(Tasks::Kind::LibraryAnalysis,
                                QStringLiteral("Analyse the library"), QString(),
                                oneShot, 0, QJsonObject()));
        pump(100);
        const int written = closing.saveInterrupted();

        {
            TaskManager opening(&settings);
            const QString offered = flatten(QVariant(opening.pending()));
            check("a task that could not be restarted is not offered back",
                  opening.pendingCount() == 1 && !offered.contains(oneShot),
                  QStringLiteral("saveInterrupted() returned %1, %2 offered: %3")
                      .arg(written)
                      .arg(opening.pendingCount())
                      .arg(offered));
            check("...while the one that could be still is",
                  offered.contains(restartable), offered);
            opening.discardPending();
            drain(opening);
        }
        drain(closing);
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
