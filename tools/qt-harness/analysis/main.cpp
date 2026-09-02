// Harness for the interpretation layer: drives the real PaperController /
// AnalysisService / AnalysisStore / SyncEngine stack against a fake backend
// (FakeSync) and a fake gateway (FakeAnalysisLlm), and asserts the things the
// requirement actually promises:
//
//   §4.4  a citation that does not match the paper is not shown as evidence
//   §4.3  a claim with nothing behind it stops being "the authors say so"
//   §6    the research profile reaches the prompt
//   §17   an interpretation goes stale when its inputs move
//   §16   results are stored in the project, and a collaborator's is readable

#include "FakeAnalysisLlm.h"
#include "FakeSync.h"

#include "AnalysisListModel.h"
#include "AnalysisService.h"
#include "BatchAnalysisService.h"
#include "AnalysisExporter.h"
#include "CompareService.h"
#include "LibraryAnalysisService.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "BlockCache.h"
#include "BlockListModel.h"
#include "LibraryDb.h"
#include "PaperController.h"
#include "FileSyncService.h"
#include "LibraryModel.h"
#include "PaperSource.h"
#include "PayloadCodec.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "SyncEngine.h"
#include "TaskManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

template <typename F>
static bool waitFor(F cond, int ms)
{
    QDeadlineTimer t(ms);
    while (!t.hasExpired()) {
        if (cond())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return cond();
}

template <typename F>
static bool waitForSync(F cond, SyncEngine &sync, int ms)
{
    QDeadlineTimer t(ms);
    int tick = 0;
    while (!t.hasExpired()) {
        if (cond())
            return true;
        if (++tick % 100 == 0)
            sync.syncNow();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return cond();
}

// Reads a paper's cached paragraphs off disk, the way the reader's own
// window would when opening that paper later.
struct BlockCacheProbe {
    explicit BlockCacheProbe(const QString &paperId)
    {
        BlockCache cache;
        cache.setPaperId(paperId);
        hasBlocks = cache.hasBlocks();
        count = cache.count();
    }
    bool hasBlocks = false;
    int count = 0;
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("AnalysisHarness");

    const QString pdfA = QString::fromLocal8Bit(qgetenv("PDF_A"));
    const QString paperA = PaperController::paperIdForFile(pdfA);
    Q_ASSERT(!paperA.isEmpty());

    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    { QSettings stale; stale.clear(); stale.sync(); }

    const QString PROJ = QStringLiteral("11111111-2222-3333-4444-555555555555");
    const QString ME = QStringLiteral("me-user-id");
    const QString OTHER = QStringLiteral("other-user-id");
    qputenv("TEST_USER_ID", ME.toUtf8());
    qputenv("TEST_USER_EMAIL", "me@example.test");

    FakeSync backend;
    backend.setProject(PROJ, QStringLiteral("owner"));

    // A collaborator has already interpreted a different paper in this
    // project. It must come through readable and attributed, without this
    // account paying for it again.
    const QString otherPaper = QStringLiteral("some-other-paper-id");
    {
        const QJsonObject payload{
            {"oneLiner", "Their paper, their reading."},
            {"facets", QJsonObject{{"methodRoute", "diffusion"}}}};
        backend.seed(
            Analysis::paperAnalysisId(PROJ, otherPaper, Analysis::KindQuick,
                                      OTHER),
            Analysis::TypePaperAnalysis,
            QJsonObject{
                {"paperId", otherPaper},
                {"kind", Analysis::KindQuick},
                {"author", OTHER},
                {"authorEmail", "other@example.test"},
                {"model", "their-model"},
                {"inputHash", "whatever"},
                {"status", Analysis::StatusOk},
                {"title", "Their paper"},
                {"codec", PayloadCodec::codecName()},
                {"payload", PayloadCodec::encode(payload)},
                {"updatedAt",
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    }

    qputenv("TEST_SERVER_URL", backend.baseUrl().toUtf8());

    FakeAnalysisLlm gateway;

    Settings settings;
    settings.setProvider(QStringLiteral("openai-compatible"));
    settings.setBaseUrl(gateway.baseUrl());
    settings.setApiKey(QStringLiteral("harness-key"));
    settings.setModel(QStringLiteral("harness-model"));
    settings.setTargetLang(QStringLiteral("en"));

    PaperController paper;
    paper.setAutoSegment(true);

    ApiClient api;
    AuthController auth(&api);
    LibraryDb db;
    ProjectController projects(&api, &auth, &db);
    SyncEngine sync(&api, &auth, &projects, &db);
    AnalysisStore store(&db, &projects, &sync, &auth);
    ProjectProfileController profile(&store);
    AnalysisService analysis(&settings, &paper, &store, &profile);

    auth.startCasLogin();
    waitFor([&] { return auth.authenticated(); }, 5000);
    projects.refresh();
    waitFor([&] { return !projects.currentId().isEmpty(); }, 5000);
    if (projects.currentId().isEmpty())
        projects.selectProject(PROJ);
    waitForSync([&] { return !projects.currentId().isEmpty(); }, sync, 5000);
    check("signed in with a writable project",
          auth.authenticated() && projects.canWrite(),
          projects.currentId());

    // A collaborator's interpretation, pulled down.
    waitForSync(
        [&] {
            return store.paperAnalysis(otherPaper, Analysis::KindQuick).valid;
        },
        sync, 8000);
    const AnalysisRecord theirs =
        store.paperAnalysis(otherPaper, Analysis::KindQuick);
    check("a collaborator's interpretation arrives readable", theirs.valid
              && theirs.payload.value("oneLiner").toString()
                     == "Their paper, their reading.");
    check("...attributed to them, not to us",
          !theirs.mine && theirs.authorEmail == "other@example.test");

    // The research profile: everything is interpreted against it.
    profile.save(QVariantMap{
        {"goal", QStringLiteral("forecast freeway traffic from loop sensors")},
        {"questions", QStringLiteral("which models hold up out of "
                                     "distribution")}});
    check("the research profile is stored", profile.hasProfile(),
          profile.summary());
    const QString profileHashBefore = profile.hash();

    paper.openPdf(QUrl::fromLocalFile(pdfA));
    const bool segmented = waitFor([&] { return paper.blockCount() > 5; }, 30000);
    check("the fixture paper segmented", segmented,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));

    check("nothing has been interpreted yet", !analysis.hasQuick());
    check("but it could be", analysis.canRun());

    analysis.generateQuick(true);
    const bool done = waitFor(
        [&] { return analysis.status() != AnalysisService::Running; }, 30000);
    check("the interpretation finished", done
              && analysis.status() == AnalysisService::Done,
          analysis.lastError());

    // What the model was actually asked.
    check("the paragraphs went in with citable [b<id> p<page>] markers",
          gateway.lastPrompt().contains(QStringLiteral("[b")),
          QStringLiteral("%1 chars of prompt").arg(gateway.lastPrompt().size()));
    check("the research profile went with them",
          gateway.lastPrompt().contains(QStringLiteral("freeway traffic")));
    check("the schema was offered as exactly one tool",
          gateway.lastRequest().value("tools").toArray().size() == 1);

    // What came back, after the citations were checked.
    const QVariantMap quick = analysis.quick();
    const QVariantMap meta = quick.value("meta").toMap();
    check("three citations were checked", meta.value("evidenceTotal").toInt() == 3,
          QStringLiteral("got %1").arg(meta.value("evidenceTotal").toInt()));
    check("two of them check out", meta.value("evidenceVerified").toInt() == 2,
          QStringLiteral("got %1").arg(meta.value("evidenceVerified").toInt()));
    check("the one with a bogus block id was found by its words",
          meta.value("evidenceRepaired").toInt() == 1);
    check("the invented quote demoted its claim",
          meta.value("claimsDemoted").toInt() == 1);

    const QVariantMap method = quick.value("method").toMap();
    check("an unsupported author_claim is relabelled",
          method.value("source").toString() == "ai_analysis"
              && method.value("unsupported").toBool());
    const QVariantList methodEv = method.value("evidence").toList();
    check("...and its citation is marked unverified, not dropped",
          methodEv.size() == 1
              && !methodEv.first().toMap().value("verified").toBool());

    const QVariantMap problem = quick.value("problem").toMap();
    const QVariantMap goodEv =
        problem.value("evidence").toList().value(0).toMap();
    check("a good citation keeps its provenance",
          problem.value("source").toString() == "author_claim");
    check("...and carries a 1-based page for the jump",
          goodEv.value("verified").toBool() && goodEv.value("page").toInt() >= 1);
    check("...pointing at a paragraph that exists",
          paper.blocks()->rowForBlockId(goodEv.value("blockId").toInt()) >= 0);

    const QVariantMap importance = quick.value("importance").toMap();
    const QVariantMap movedEv =
        importance.value("evidence").toList().value(0).toMap();
    check("the repaired citation records what it originally claimed",
          movedEv.value("repairedFrom").toInt() == 99999
              && movedEv.value("verified").toBool());

    // Storage and sharing.
    check("the interpretation was filed in the project", analysis.quickSaved());
    const bool pushed = waitForSync(
        [&] { return backend.count(Analysis::TypePaperAnalysis) == 2; }, sync,
        10000);
    check("...and pushed to the project for everyone else", pushed,
          QStringLiteral("%1 on the server")
              .arg(backend.count(Analysis::TypePaperAnalysis)));

    check("it is not stale as generated", !analysis.quickStale());

    // §17: change what it was written against, and say so.
    profile.save(QVariantMap{
        {"goal", QStringLiteral("something else entirely")}});
    check("editing the research profile changes the staleness key",
          profile.hash() != profileHashBefore);
    check("...and marks the interpretation as possibly out of date",
          analysis.quickStale());

    // The digest is the unit the project-wide analyses read.
    const QList<AnalysisRecord> all =
        store.paperAnalyses(Analysis::KindQuick);
    check("both papers' digests are listed for library-level analysis",
          all.size() == 2,
          QStringLiteral("got %1").arg(all.size()));

    analysis.discardQuick();
    check("discarding drops it from the pane", !analysis.hasQuick());

    // ── §7: the same thing over a library nobody has opened ──────────
    LibraryModel library(&db, &projects, &sync);
    FileSyncService files(&api, &db, &projects, &sync);
    PaperSource source(&db, &library, &projects, &files);
    AnalysisListModel list(&db, &library, &projects, &store);
    BatchAnalysisService batch(&settings, &store, &profile, &source, &list);

    const QStringList fixtures = {
        QString::fromLocal8Bit(qgetenv("PDF_B")),
        QString::fromLocal8Bit(qgetenv("PDF_C"))};
    for (int i = 0; i < fixtures.size(); ++i) {
        library.addPaper(QStringLiteral("Batch paper %1").arg(i + 1),
                         PaperController::paperIdForFile(fixtures.at(i)),
                         fixtures.at(i));
    }
    // One entry whose PDF is not there at all, so the failure path is
    // exercised rather than assumed.
    library.addPaper(QStringLiteral("Missing paper"),
                     QStringLiteral("no-such-paper-id"),
                     QStringLiteral("/nowhere/missing.pdf"));
    list.reload();
    check("the library lists every paper in the project",
          list.totalPapers() == 3,
          QStringLiteral("got %1").arg(list.totalPapers()));
    check("...and knows none of them has been read",
          list.pendingCount() == 3,
          QStringLiteral("got %1").arg(list.pendingCount()));

    const int before = gateway.requests();
    batch.startPending();
    const bool batchDone =
        waitFor([&] { return !batch.busy() && batch.total() > 0; }, 120000);
    check("the batch ran to the end", batchDone, batch.status());
    check("both real papers were interpreted without being opened",
          batch.done() == 2,
          QStringLiteral("done=%1 failed=%2 skipped=%3")
              .arg(batch.done()).arg(batch.failed()).arg(batch.skipped()));
    check("the missing PDF failed instead of hanging the batch",
          batch.failed() == 1);
    check("...with a reason the row can show",
          !batch.errorFor(batch.failedItems().value(0)).isEmpty(),
          batch.errorFor(batch.failedItems().value(0)));
    check("the reader's own paper was never touched by the batch",
          paper.paperId() == paperA);
    check("one model call per paper", gateway.requests() - before == 2,
          QStringLiteral("got %1").arg(gateway.requests() - before));

    list.reload();
    check("the list shows what came back", list.interpretedCount() == 2,
          QStringLiteral("got %1").arg(list.interpretedCount()));

    // Filters: the fake gateway always answers "high" and
    // "read_method_experiments", so these are the two ends of the check.
    list.setFilterRelevance(QStringLiteral("high"));
    check("filtering by relevance keeps the interpreted ones",
          list.rowCount() == 2, QStringLiteral("got %1").arg(list.rowCount()));
    list.setFilterRelevance(QStringLiteral("low"));
    check("...and drops everything else", list.rowCount() == 0);
    list.setFilterRelevance(QString());
    list.setFilterAdvice(QStringLiteral("read_method_experiments"));
    check("filtering by reading advice works the same way",
          list.rowCount() == 2);
    list.setFilterAdvice(QString());

    // §7's two bulk actions.
    list.setFilterState(QStringLiteral("done"));
    const QStringList shown = list.visibleItemIds();
    list.applyToRead(shown, true);
    check("a filtered set can be marked for a close read in one go",
          list.toReadCount() == 2);
    list.applyExcluded(shown, true);
    list.setFilterState(QString());
    check("...or set aside in one go", list.excludedCount() == 2);
    check("set-aside papers drop out of the default view",
          list.rowCount() == 1, QStringLiteral("got %1").arg(list.rowCount()));
    list.applyExcluded(shown, false);

    // Re-running must not pay for the same papers twice.
    const int before2 = gateway.requests();
    batch.startItems(shown, false);
    waitFor([&] { return !batch.busy(); }, 30000);
    check("a second run skips papers that already have an interpretation",
          gateway.requests() == before2 && batch.skipped() == 2,
          QStringLiteral("skipped=%1").arg(batch.skipped()));

    // ── §7 × §3: close-reading a whole starred set ──────────────────
    // The star used to be a note to self and nothing else: the batch could
    // only ever run the quick interpretation, so a reader could mark thirty
    // papers and find nothing in the app that would read them.
    check("the two papers starred earlier are what a close read would cost",
          list.deepPendingCount() == 2,
          QStringLiteral("got %1").arg(list.deepPendingCount()));

    const int before3 = gateway.requests();
    batch.startDeepItems(list.toReadItemIds(), false);
    const bool deepBatchDone =
        waitFor([&] { return !batch.busy() && batch.total() > 0; }, 180000);
    check("the starred papers were close-read without being opened",
          deepBatchDone && batch.done() == 2,
          QStringLiteral("done=%1 failed=%2 skipped=%3 — %4")
              .arg(batch.done()).arg(batch.failed()).arg(batch.skipped())
              .arg(batch.status()));
    check("...nine calls for each of them, one per part of the reading",
          gateway.requests() - before3 == 18,
          QStringLiteral("got %1").arg(gateway.requests() - before3));
    list.reload();
    check("...and the list says both carry a close reading now",
          list.deepDoneCount() == 2,
          QStringLiteral("got %1").arg(list.deepDoneCount()));
    check("...so the star has nothing left to spend",
          list.deepPendingCount() == 0,
          QStringLiteral("got %1").arg(list.deepPendingCount()));

    const int before4 = gateway.requests();
    batch.startDeepItems(list.toReadItemIds(), false);
    waitFor([&] { return !batch.busy(); }, 30000);
    check("a second close-read run buys none of it again",
          gateway.requests() == before4 && batch.skipped() == 2,
          QStringLiteral("skipped=%1").arg(batch.skipped()));

    // The nine parts are written against the quick interpretation, so a
    // paper reached without one has to be interpreted first rather than
    // failed for not having been.
    const QString dPath = QString::fromLocal8Bit(qgetenv("PDF_D"));
    const QString dItem =
        library.addPaper(QStringLiteral("Never interpreted"),
                         PaperController::paperIdForFile(dPath), dPath);
    list.reload();
    const int before5 = gateway.requests();
    batch.startDeepItems(QStringList{dItem}, false);
    const bool dDone =
        waitFor([&] { return !batch.busy() && batch.total() > 0; }, 180000);
    check("a paper with no interpretation gets one first, then the nine parts",
          dDone && gateway.requests() - before5 == 10,
          QStringLiteral("%1 calls — %2")
              .arg(gateway.requests() - before5)
              .arg(batch.status()));
    list.reload();
    check("...and both are on record when it is over",
          list.interpretedCount() == 3 && list.deepDoneCount() == 3,
          QStringLiteral("quick=%1 deep=%2")
              .arg(list.interpretedCount())
              .arg(list.deepDoneCount()));

    // A paper the batch segmented is cached, so opening it later is free.
    BlockCacheProbe probe(PaperController::paperIdForFile(fixtures.at(0)));
    check("the segmentation the batch paid for was kept for the reader",
          probe.hasBlocks, QStringLiteral("%1 paragraphs").arg(probe.count));

    // ── §3: the close reading, module by module ─────────────────────
    // Back to the reader's own paper, which still has its paragraphs.
    analysis.generateQuick(true);
    waitFor([&] { return analysis.status() != AnalysisService::Running; }, 30000);

    analysis.generateDeep(false);
    const bool deepDone = waitFor([&] { return !analysis.deepRunning(); }, 120000);
    check("the close reading ran to the end", deepDone,
          QStringLiteral("%1/%2 modules")
              .arg(analysis.deepDone()).arg(analysis.deepTotal()));
    check("every module was written",
          analysis.deepDone() == analysis.deepTotal(),
          QStringLiteral("got %1").arg(analysis.deepDone()));
    check("one model call per module", analysis.moduleIds().size() == 9);

    const QVariantMap methodModule = analysis.module(QStringLiteral("method"));
    check("a module carries its sections",
          !methodModule.value("sections").toList().isEmpty());
    const QVariantMap mmeta = methodModule.value("meta").toMap();
    // The fake answers every module with the same object, so its citations
    // land in `sections`, in `coreQuestion` and in `acknowledged` — four in
    // all, of which the one invented quote fails.
    check("...and its citations were checked too",
          mmeta.value("evidenceTotal").toInt() == 4
              && mmeta.value("evidenceVerified").toInt() == 3,
          QStringLiteral("%1 of %2")
              .arg(mmeta.value("evidenceVerified").toInt())
              .arg(mmeta.value("evidenceTotal").toInt()));
    check("...with the unsupported claim demoted here as well",
          mmeta.value("claimsDemoted").toInt() == 1);
    check("the close reading was filed in the project", analysis.deepSaved());

    // §5: redo one part without touching the rest.
    const int callsBefore = gateway.requests();
    analysis.regenerateModule(QStringLiteral("critique"));
    waitFor([&] { return !analysis.deepRunning(); }, 60000);
    check("regenerating one module costs exactly one call",
          gateway.requests() - callsBefore == 1,
          QStringLiteral("got %1").arg(gateway.requests() - callsBefore));
    check("...and leaves the other modules in place",
          analysis.deepDone() == analysis.deepTotal());

    // §5 / §16: notes are the reader's and survive a regeneration.
    analysis.saveNote(QStringLiteral("Ask about the sampling rate"),
                      QStringLiteral("method"));
    check("a note is kept", analysis.notes().size() == 1);
    analysis.generateQuick(true);
    waitFor([&] { return analysis.status() != AnalysisService::Running; }, 30000);
    check("...and regenerating an interpretation does not touch it",
          analysis.notes().size() == 1);

    // ── §10: comparing papers the reader picked ─────────────────────
    CompareService compare(&settings, &store, &projects, &profile);
    // Through the one queue, like every other model call: the row is what
    // the reader watches while the model writes.
    TaskManager tasks(&settings);
    compare.setTasks(&tasks);
    compare.clearBasket();
    check("an empty comparison cannot run", !compare.canRun());
    const QList<AnalysisRecord> digests =
        store.paperAnalyses(Analysis::KindQuick);
    check("there are digests to compare", digests.size() >= 2);
    for (const AnalysisRecord &d : digests) {
        if (d.paperId == otherPaper)
            continue;              // the seeded one has no facets to compare
        compare.add(d.paperId, d.title, QStringLiteral("worth a look"));
    }
    check("papers can be put in the comparison basket", compare.count() >= 2,
          QStringLiteral("got %1").arg(compare.count()));
    check("...and the basket knows what is in it",
          compare.contains(digests.first().paperId)
              || compare.contains(digests.last().paperId));
    check("a comparison can run once every paper has been interpreted",
          compare.canRun());

    compare.compare();
    check("a comparison is a task the moment it is asked for",
          compare.busy() && tasks.activeCount() == 1,
          QStringLiteral("busy=%1 active=%2")
              .arg(compare.busy()).arg(tasks.activeCount()));
    const bool compared = waitFor([&] { return compare.hasResult(); }, 60000);
    check("the comparison came back", compared, compare.lastError());
    check("...and its task ended with it",
          !compare.busy() && tasks.activeCount() == 0
              && tasks.finishedCount() == 1,
          QStringLiteral("busy=%1 active=%2 finished=%3")
              .arg(compare.busy()).arg(tasks.activeCount())
              .arg(tasks.finishedCount()));
    check("the answer reported its size as it streamed in",
          compare.receivedBytes() > 0,
          QStringLiteral("%1 bytes").arg(compare.receivedBytes()));
    const QVariantMap cres = compare.result();
    check("it has a row per dimension", cres.value("rows").toList().size() == 2);
    check("it names what cannot be compared",
          cres.value("comparability").toList().size() == 1);
    check("...and says so instead of ranking them",
          cres.value("ranking").toString().contains(QStringLiteral("cannot")));
    check("the comparison was filed in the project under its own paper set",
          store.libraryAnalysis(Analysis::KindCompare,
                                Analysis::scopeHash(
                                    [&] {
                                        QStringList ids;
                                        for (const QVariant &v : compare.basket())
                                            ids.append(v.toMap()
                                                           .value("paperId")
                                                           .toString());
                                        return ids;
                                    }()))
              .valid);

    // Stopped from the tasks pane: the call ends, the row ends Canceled, and
    // the answer already on file is left alone.
    const QString kept = compare.resultUpdatedAt();
    compare.compare();
    check("a second run of the same set is one task, not two",
          compare.busy() && tasks.activeCount() == 1);
    tasks.cancelAll();
    check("cancelling from the tasks pane stops the comparison",
          !compare.busy() && tasks.activeCount() == 0
              && tasks.finishedCount() == 2,
          QStringLiteral("busy=%1 active=%2 finished=%3")
              .arg(compare.busy()).arg(tasks.activeCount())
              .arg(tasks.finishedCount()));
    check("...and is not counted as a failure",
          compare.lastError().isEmpty(), compare.lastError());
    waitFor([&] { return false; }, 300);   // the aborted reply, if it lands
    check("...nor does it disturb the comparison on file",
          compare.hasResult() && compare.resultUpdatedAt() == kept);
    compare.compare();
    check("the comparison runs again afterwards",
          waitFor([&] { return !compare.busy(); }, 60000)
              && compare.lastError().isEmpty() && tasks.finishedCount() == 3,
          QStringLiteral("err=%1 finished=%2")
              .arg(compare.lastError()).arg(tasks.finishedCount()));

    // A gateway whose model has no tool parser answers 400 to anything
    // carrying `tools`. The interpretation must still land, via prose.
    gateway.setRefuseTools(true);
    const int beforeRefuse = gateway.requests();
    analysis.generateQuick(true);
    waitFor([&] { return analysis.status() != AnalysisService::Running; }, 30000);
    check("a gateway that refuses tools does not kill the interpretation",
          analysis.status() == AnalysisService::Done && analysis.hasQuick(),
          analysis.lastError());
    check("...it asks again in prose, and digs the JSON out of the answer",
          gateway.requests() - beforeRefuse == 2,
          QStringLiteral("%1 calls").arg(gateway.requests() - beforeRefuse));
    gateway.setRefuseTools(false);

    // A provider that rejects the request says why in the body. That text is
    // what the reader has to see -- Qt's own "server replied with status code
    // 400" names no cause and no cure.
    gateway.setRefuseAll(QStringLiteral(
        "This model's maximum context length is 65536 tokens."));
    analysis.generateQuick(true);
    waitFor([&] { return analysis.status() != AnalysisService::Running; }, 30000);
    check("a rejected request reports what the server said",
          analysis.lastError().contains(QStringLiteral("65536")),
          analysis.lastError().left(120));
    check("...with the HTTP status alongside it",
          analysis.lastError().contains(QStringLiteral("400")));
    check("...and what to do about it",
          analysis.lastError().contains(QStringLiteral("Context window")),
          analysis.lastError().right(80));
    gateway.setRefuseAll(QString());

    // ── §8: the category system, and who owns it ────────────────────
    LibraryAnalysisService research(&settings, &store, &projects, &profile);
    check("a project-wide analysis can run once there are digests",
          research.canRun(), QStringLiteral("%1 digests").arg(research.digestCount()));

    research.generate(QStringLiteral("taxonomy"));
    waitFor([&] { return research.runningKind().isEmpty(); }, 60000);
    check("the category system came back", research.has(QStringLiteral("taxonomy")),
          research.lastError());

    auto categories = [&]() {
        QVariantList out;
        const QVariantMap tax = research.result(QStringLiteral("taxonomy"));
        for (const QVariant &dv : tax.value("dimensions").toList())
            for (const QVariant &cv : dv.toMap().value("categories").toList())
                out.append(cv);
        return out;
    };
    check("it has categories with ids", categories().size() == 2,
          QStringLiteral("got %1").arg(categories().size()));

    const QString catA = categories().value(0).toMap().value("id").toString();
    check("...that are stable identifiers, not names", !catA.isEmpty());

    research.renameCategory(catA, QStringLiteral("my own name"));
    check("the reader can rename a category",
          categories().value(0).toMap().value("name").toString()
              == QStringLiteral("my own name"));
    check("...which counts as confirming it",
          categories().value(0).toMap().value("confirmed").toBool());

    research.setCategoryLocked(catA, true);
    research.addCategory(QStringLiteral("method_route"),
                         QStringLiteral("a category of mine"));
    check("the reader can add their own category", categories().size() == 3);

    // Regenerating must not undo any of that (§8.3).
    research.generate(QStringLiteral("taxonomy"));
    waitFor([&] { return research.runningKind().isEmpty(); }, 60000);
    const QVariantList after = categories();
    bool keptRename = false, keptOwn = false;
    for (const QVariant &c : after) {
        const QVariantMap m = c.toMap();
        if (m.value("name").toString() == QStringLiteral("my own name"))
            keptRename = true;
        if (m.value("name").toString() == QStringLiteral("a category of mine"))
            keptOwn = true;
    }
    check("a rename survives regenerating the category system", keptRename);
    check("...and so does a category the reader made", keptOwn);
    check("...without the system doubling in size", after.size() == 3,
          QStringLiteral("got %1").arg(after.size()));

    // §8.2: a paper can sit in several categories.
    const QString somePaper = digests.first().paperId;
    const QString catB = after.value(1).toMap().value("id").toString();
    research.assignPaper(somePaper, catB, true);
    int inTwo = 0;
    for (const QVariant &c : categories()) {
        const QVariantList ids = c.toMap().value("paperIds").toList();
        for (const QVariant &v : ids)
            if (v.toString() == somePaper)
                ++inTwo;
    }
    check("a paper can belong to more than one category", inTwo >= 2,
          QStringLiteral("in %1").arg(inTwo));

    // §8.4: papers the system has never seen get placed into it.
    const QStringList unplaced = research.unclassifiedPapers();
    check("papers outside the system are noticed", !unplaced.isEmpty(),
          QStringLiteral("%1 unplaced").arg(unplaced.size()));
    research.classifyNewPapers();
    waitFor([&] { return research.runningKind().isEmpty(); }, 60000);
    check("...and get placed without redrawing the system",
          research.unclassifiedPapers().size() < unplaced.size()
              && categories().size() == 3,
          QStringLiteral("%1 left, %2 categories")
              .arg(research.unclassifiedPapers().size())
              .arg(categories().size()));

    check("the analysis knows when papers moved under it",
          !research.isStale(QStringLiteral("taxonomy")));

    // §16: getting it back out.
    AnalysisExporter exporter(&store, &projects, &profile, &research, &compare);
    const QString md = exporter.paperMarkdown(paperA);
    check("a paper exports as Markdown", md.startsWith(QStringLiteral("# ")));
    check("...carrying where each statement came from",
          md.contains(QStringLiteral("AI reading"))
              || md.contains(QStringLiteral("authors")));
    check("...and marking the citation that did not check out",
          md.contains(QStringLiteral("unverified")));
    check("...and the reader's own notes",
          md.contains(QStringLiteral("sampling rate")));
    const QString cmd = exporter.comparisonMarkdown();
    check("a comparison exports as a Markdown table",
          cmd.contains(QStringLiteral("| ")) &&
          cmd.contains(QStringLiteral("Not directly comparable")));
    const QString rmd = exporter.projectMarkdown();
    check("the project exports as a report", rmd.contains(QStringLiteral("# ")));
    check("...saying out loud that it describes this library, not the field",
          rmd.contains(QStringLiteral("not work that does not exist")));
    const QString outPath = root + QStringLiteral("/export.md");
    check("the export writes a file",
          exporter.save(md, QUrl::fromLocalFile(outPath))
              && QFile(outPath).size() > 100);

    // ── everything it produces reaches the project ──────────────────
    // The point of the whole layer is that the work is done once and the
    // rest of the group has it. That means every object type has to be on
    // the server, not only in this machine's mirror.
    compare.add(digests.first().paperId, digests.first().title,
                QStringLiteral("for the record"));
    const bool synced = waitForSync(
        [&] {
            return backend.count(Analysis::TypeProjectProfile) == 1
                   && backend.count(Analysis::TypeLibraryAnalysis) >= 1
                   && backend.count(Analysis::TypeAnalysisNote) == 1
                   && backend.count(Analysis::TypeCompareBasket) == 1;
        },
        sync, 15000);
    check("the research profile reaches the project",
          backend.count(Analysis::TypeProjectProfile) == 1);
    check("the interpretations do",
          backend.count(Analysis::TypePaperAnalysis) >= 3,
          QStringLiteral("%1 on the server")
              .arg(backend.count(Analysis::TypePaperAnalysis)));
    check("the project-wide analyses do",
          backend.count(Analysis::TypeLibraryAnalysis) >= 1);
    check("a personal note does", backend.count(Analysis::TypeAnalysisNote) == 1);
    check("and so does the comparison basket, which used to sit in this "
          "machine's settings file",
          backend.count(Analysis::TypeCompareBasket) == 1, synced ? "" : "timed out");
    bool flagged = false;
    for (const QJsonObject &item : backend.objectsOfType(QStringLiteral("item")))
        flagged = flagged || item.value(QStringLiteral("toRead")).toBool();
    check("marking a paper to read closely rides on the paper itself", flagged);

    // ── a settings change takes effect now, not after a restart ─────
    // Every service used to cache its client and patch its fields, so
    // switching provider kept talking to the previous endpoint until the
    // app was restarted.
    FakeAnalysisLlm gateway2;
    const int wasOnOne = gateway.requests();
    const int wasOnTwo = gateway2.requests();
    settings.setBaseUrl(gateway2.baseUrl());
    analysis.generateQuick(true);
    waitFor([&] { return analysis.status() != AnalysisService::Running; }, 30000);
    check("pointing at another endpoint takes effect without a restart",
          gateway2.requests() > wasOnTwo,
          QStringLiteral("second gateway saw %1")
              .arg(gateway2.requests() - wasOnTwo));
    check("...and the old one is not called again",
          gateway.requests() == wasOnOne);
    settings.setBaseUrl(gateway.baseUrl());

    // ── the endpoint follows the provider, not the field ────────────
    // A Base URL left behind by an earlier provider used to keep being
    // used, so every request went to the wrong server.
    check("a named provider ignores a leftover Base URL",
          Settings::resolveBaseUrl(QStringLiteral("deepseek"),
                                   QStringLiteral("http://127.0.0.1:9/ghost"))
              == QStringLiteral("https://api.deepseek.com"));
    check("...for each of the three that have one",
          Settings::resolveBaseUrl(QStringLiteral("anthropic"), QString())
                  == QStringLiteral("https://api.anthropic.com")
              && Settings::resolveBaseUrl(QStringLiteral("openai"),
                                          QStringLiteral("http://stale"))
                     == QStringLiteral("https://api.openai.com"));
    check("...while openai-compatible uses the address it was given",
          Settings::resolveBaseUrl(QStringLiteral("openai-compatible"),
                                   QStringLiteral("http://gateway.local:8080"))
              == QStringLiteral("http://gateway.local:8080"));
    check("...and only it may be told one",
          !Settings::providerTakesCustomUrl(QStringLiteral("openai"))
              && Settings::providerTakesCustomUrl(
                  QStringLiteral("openai-compatible")));

    settings.setBaseUrl(QString());
    check("openai-compatible with no address counts as unconfigured",
          !settings.isConfigured());
    settings.setBaseUrl(gateway.baseUrl());
    check("...and configured again once it has one", settings.isConfigured());

    // ── what a paper switch costs on a real-sized library ───────────
    // Switching paper asks the store for that paper's interpretation. With
    // one row per paper per member, a lookup that scans them all pays for
    // the whole library -- and a deep read is tens of KB of base64 each,
    // so the parse alone is the cost.
    {
        const QString proj = projects.currentId();
        const QString filler(6000, QChar('x'));   // ~ a real digest
        const QString deepFiller(40000, QChar('y'));
        for (int i = 0; i < 200; ++i) {
            const QString paper = QStringLiteral("bulk-paper-%1").arg(i);
            for (const QString &kind : {Analysis::KindQuick, Analysis::KindDeep}) {
                SyncObjectRow row;
                row.id = Analysis::paperAnalysisId(proj, paper, kind,
                                                   QStringLiteral("bulk-author"));
                row.projectId = proj;
                row.type = Analysis::TypePaperAnalysis;
                row.data = QJsonObject{
                    {QStringLiteral("paperId"), paper},
                    {QStringLiteral("kind"), kind},
                    {QStringLiteral("author"), QStringLiteral("bulk-author")},
                    {QStringLiteral("codec"), PayloadCodec::codecName()},
                    {QStringLiteral("payload"),
                     kind == Analysis::KindQuick ? filler : deepFiller},
                    {QStringLiteral("updatedAt"), QStringLiteral("2026-01-01")}};
                row.version = 1;
                db.upsertFromServer(row);
            }
        }
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 20; ++i) {
            store.paperAnalysis(QStringLiteral("bulk-paper-7"), Analysis::KindQuick);
            store.paperAnalysis(QStringLiteral("bulk-paper-7"), Analysis::KindDeep);
        }
        const double perSwitch = t.nsecsElapsed() / 1e6 / 20.0;
        check("looking a paper's interpretation up stays cheap on a 200-paper "
              "library",
              perSwitch < 8.0,
              QStringLiteral("%1 ms per paper switch").arg(perSwitch, 0, 'f', 1));
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed")
                             .arg(g_pass)
                             .arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
