// Harness for cancelling a translation run.
//
// The reported bug: pressing Cancel did nothing visible — paragraphs kept
// streaming in and the button stayed on Cancel. cancel() cleared the queue but
// deliberately let in-flight requests "finish naturally", and with two of them
// running that is what the user sees.
//
// Everything here is the shipping code except main, AuthController and the two
// keychain functions in Settings (see build.sh). The model endpoint is a local
// socket that streams forever, so a cancel can be caught mid-flight.

#include "FakeLlm.h"

#include "Block.h"
#include "BlockListModel.h"
#include "PaperController.h"
#include "Settings.h"
#include "Tabs.h"
#include "TranslationService.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  — " + detail);
}

static void pump(int ms)
{
    QDeadlineTimer t(ms);
    while (!t.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
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

// How many rows are in each state right now.
struct Tally { int idle = 0, queued = 0, translating = 0, done = 0, failed = 0,
                   skipped = 0, withText = 0; };
static Tally tally(BlockListModel *m)
{
    Tally t;
    for (int row = 0; row < m->blockCount(); ++row) {
        const Block *b = m->blockAt(row);
        if (!b) continue;
        switch (b->translationStatus) {
        case Block::NotTranslated: ++t.idle; break;
        case Block::Queued:        ++t.queued; break;
        case Block::Translating:   ++t.translating; break;
        case Block::Translated:    ++t.done; break;
        case Block::Failed:        ++t.failed; break;
        case Block::Skipped:       ++t.skipped; break;
        }
        if (!b->translation.isEmpty())
            ++t.withText;
    }
    return t;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("TranslationHarness");

    const QString pdf = QString::fromLocal8Bit(qgetenv("PDF_A"));
    const QString pdfB = QString::fromLocal8Bit(qgetenv("PDF_B"));
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    // QSettings does NOT live under AppDataLocation — on macOS it is a plist
    // in ~/Library/Preferences keyed by the org/app names above. Wiping only
    // the app-data root left the previous run's Settings behind, which is how
    // a target language set at the end of one run turned up at the start of
    // the next and quietly changed what the checks were measuring.
    { QSettings stale; stale.clear(); stale.sync(); }

    FakeLlm llm;

    Settings settings;
    settings.setProvider(QStringLiteral("openai"));
    settings.setModel(QStringLiteral("harness-model"));
    settings.setBaseUrl(llm.baseUrl());
    // Non-empty on purpose: an empty key takes setApiKey's delete branch,
    // which is the one keychain call build.sh does not neutralise.
    settings.setApiKey(QStringLiteral("harness-key"));
    check("the harness has a configured model", settings.isConfigured());

    PaperController paper;
    TranslationService translation(&settings, &paper);
    Tabs tabs(&paper);
    // The same one-liner main.cpp uses: a closed tab ends that paper's run.
    QObject::connect(&tabs, &Tabs::paperClosed, &translation,
                     [&translation](const QUrl &url) {
                         translation.cancelPaper(
                             PaperController::paperIdForFile(url.toLocalFile()));
                     });

    const QString paperA = PaperController::paperIdForFile(pdf);
    const QString paperB = PaperController::paperIdForFile(pdfB);
    const auto entriesOnDisk = [&](const QString &paperId) {
        QFile f(root + QStringLiteral("/cache/translations/") + paperId
                + QStringLiteral(".json"));
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        return int(QJsonDocument::fromJson(f.readAll())
                       .object().value(QStringLiteral("entries")).toArray().size());
    };

    paper.openPdf(QUrl::fromLocalFile(pdf));
    paper.rebuildBlocks();
    const bool segmented =
        waitFor([&] { return !paper.extracting() && paper.blockCount() > 8; },
                60000);
    check("the fixture segmented", segmented,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));

    // ── a tab can be named by something that knows better ─────────────
    check("a tab falls back to the filename",
          tabs.nameAt(0).endsWith(QStringLiteral(".pdf")), tabs.nameAt(0));
    tabs.setTitleResolver([](const QUrl &u) {
        return u.toLocalFile().endsWith(QStringLiteral("t.pdf"))
                   ? QStringLiteral("A Named Paper") : QString();
    });
    tabs.refreshTitles();
    check("and uses the resolver's title when there is one",
          tabs.nameAt(0) == QStringLiteral("A Named Paper"), tabs.nameAt(0));
    tabs.setTitleResolver({});
    check("an empty title falls back rather than blanking the tab",
          tabs.nameAt(0).endsWith(QStringLiteral(".pdf")), tabs.nameAt(0));

    // ── which paper is open, as the library and folder panes ask it ───
    check("the open paper is recognised by path",
          paper.isCurrentFile(pdf) && !paper.isCurrentFile(pdfB));
    {
        // The library stores plain paths; a row differing only by a "/./"
        // still has to light up.
        const int slash = pdf.lastIndexOf(QChar('/'));
        const QString noisy = pdf.left(slash) + QStringLiteral("/./")
                              + pdf.mid(slash + 1);
        check("and by an equivalent spelling of it", paper.isCurrentFile(noisy),
              noisy);
    }
    check("a path that isn't a file is not the open paper",
          !paper.isCurrentFile(QStringLiteral("/nowhere/missing.pdf"))
              && !paper.isCurrentFile(QString()));

    // ── a run is under way ────────────────────────────────────────────
    translation.translateAll();
    const bool streaming = waitFor([&] {
        return llm.openStreams() >= 2 && tally(paper.blocks()).withText >= 2;
    }, 30000);
    check("two paragraphs are streaming", streaming,
          QStringLiteral("%1 open, %2 requests")
              .arg(llm.openStreams()).arg(llm.requests()));
    check("and the service says it is busy", translation.busy());

    const Tally before = tally(paper.blocks());
    check("with a queue behind them", before.queued > 0,
          QStringLiteral("%1 queued").arg(before.queued));

    // ── cancel ────────────────────────────────────────────────────────
    const int chunksAtCancel = llm.chunksSent();
    translation.cancel();

    check("Cancel stops the run there and then", !translation.busy());
    const Tally after = tally(paper.blocks());
    check("nothing is left queued or in flight",
          after.queued == 0 && after.translating == 0,
          QStringLiteral("%1 queued, %2 translating")
              .arg(after.queued).arg(after.translating));
    check("cancelling is not counted as a failure",
          translation.failedCount() == 0 && after.failed == 0,
          QStringLiteral("failedCount=%1 rows=%2")
              .arg(translation.failedCount()).arg(after.failed));
    check("half-streamed paragraphs are cleared, not left mid-sentence",
          after.withText <= after.done + after.skipped,
          QStringLiteral("%1 rows with text, %2 done + %3 skipped")
              .arg(after.withText).arg(after.done).arg(after.skipped));

    // The real test of an abort: the sockets go away and the model stops
    // being billed for tokens nobody will read.
    const bool closed = waitFor([&] { return llm.openStreams() == 0; }, 5000);
    check("the model connections are actually closed", closed,
          QStringLiteral("%1 still open").arg(llm.openStreams()));
    pump(1500);
    check("no more chunks arrive after Cancel",
          llm.chunksSent() <= chunksAtCancel + 2,
          QStringLiteral("%1 → %2").arg(chunksAtCancel).arg(llm.chunksSent()));

    // ── and the pane is usable again ──────────────────────────────────
    const int requestsBefore = llm.requests();
    translation.translateAll();
    const bool restarted = waitFor([&] {
        return translation.busy() && llm.requests() > requestsBefore;
    }, 30000);
    check("Translate works again afterwards", restarted,
          QStringLiteral("%1 → %2 requests").arg(requestsBefore).arg(llm.requests()));
    translation.cancel();
    waitFor([&] { return llm.openStreams() == 0; }, 5000);

    // ── a run belongs to its paper, not to the pane ───────────────────
    // Reported alongside the Cancel bug: switching papers killed the run,
    // and it shouldn't — "各翻译各的".
    llm.setChunkLimit(3);          // let paragraphs actually finish now
    translation.translateAll();
    waitFor([&] { return translation.doneCount() > 0; }, 30000);
    const int doneOnA = translation.doneCount();
    check("paper A is translating", doneOnA > 0 && translation.busy(),
          QStringLiteral("%1 done").arg(doneOnA));

    tabs.openPaper(QUrl::fromLocalFile(pdfB));
    waitFor([&] { return paper.paperId() == paperB; }, 20000);
    check("switching papers does not stop the run",
          translation.backgroundPapers() == 1,
          QStringLiteral("%1 background papers, %2 streams")
              .arg(translation.backgroundPapers()).arg(llm.openStreams()));
    check("the new paper has its own, empty tally",
          !translation.busy() && translation.doneCount() == 0
              && translation.totalCount() == 0,
          QStringLiteral("busy=%1 %2/%3").arg(translation.busy())
              .arg(translation.doneCount()).arg(translation.totalCount()));

    const bool drained =
        waitFor([&] { return translation.backgroundPapers() == 0; }, 120000);
    check("paper A finishes in the background", drained);
    check("and its translations were written to its own cache",
          entriesOnDisk(paperA) > 0,
          QStringLiteral("%1 entries").arg(entriesOnDisk(paperA)));
    check("without leaking into the paper on screen",
          entriesOnDisk(paperB) == 0);

    tabs.openPaper(QUrl::fromLocalFile(pdf));
    waitFor([&] { return paper.paperId() == paperA; }, 20000);
    const Tally back = tally(paper.blocks());
    check("coming back to A shows the work it did while hidden",
          back.done > 0 && !translation.busy(),
          QStringLiteral("%1 translated").arg(back.done));

    // ── the two things Translate can mean on a half-done paper ────────
    check("a fully translated paper reports no gaps",
          translation.translatedParagraphs() == back.done
              && translation.untranslatedParagraphs() == 0,
          QStringLiteral("%1 translated, %2 left")
              .arg(translation.translatedParagraphs())
              .arg(translation.untranslatedParagraphs()));

    llm.setChunkLimit(100000);
    translation.retranslateAll();
    check("starting over re-queues every paragraph",
          translation.totalCount() == back.done && translation.busy(),
          QStringLiteral("%1 queued of %2 translated")
              .arg(translation.totalCount()).arg(back.done));
    check("and nothing is left claiming to be translated",
          tally(paper.blocks()).done == 0,
          QStringLiteral("%1 still done").arg(tally(paper.blocks()).done));
    translation.cancel();
    waitFor([&] { return llm.openStreams() == 0; }, 5000);

    // Filling the gaps is the other one: with everything cleared, the two
    // now agree, and translateAll picks up exactly what has no translation.
    check("the gap count is what translateAll would take",
          translation.untranslatedParagraphs() == paper.blockCount()
              - tally(paper.blocks()).skipped,
          QStringLiteral("%1 gaps, %2 paragraphs")
              .arg(translation.untranslatedParagraphs())
              .arg(paper.blockCount()));

    // ── progress can never overrun ────────────────────────────────────
    // Reported as "正在翻译 419/382". The tally was fed from two places at
    // once: every finished job added one, and the cache-rehydrate raised it
    // to however many paragraphs were translated. Start over with the old
    // translations still in the cache, let a rehydrate refill them, and the
    // same paragraph got counted twice.
    llm.setChunkLimit(3);
    translation.retranslateAll();
    const int paras = paper.blockCount();
    waitFor([&] { return translation.doneCount() > 2; }, 60000);
    tabs.openPaper(QUrl::fromLocalFile(pdfB));      // forces a rehydrate...
    waitFor([&] { return paper.paperId() == paperB; }, 20000);
    tabs.openPaper(QUrl::fromLocalFile(pdf));       // ...on the way back
    waitFor([&] { return paper.paperId() == paperA; }, 20000);

    bool overran = false;
    for (int i = 0; i < 400 && translation.busy(); ++i) {
        if (translation.doneCount() > translation.totalCount())
            overran = true;
        pump(100);
    }
    if (translation.doneCount() > translation.totalCount())
        overran = true;
    check("re-translating past a rehydrate never overruns the total",
          !overran, QStringLiteral("%1/%2").arg(translation.doneCount())
                        .arg(translation.totalCount()));
    check("and the total stays the paper's paragraph count",
          translation.totalCount() <= paras,
          QStringLiteral("%1 of %2 paragraphs")
              .arg(translation.totalCount()).arg(paras));
    translation.cancel();
    waitFor([&] { return llm.openStreams() == 0; }, 5000);

    // ── editing paragraphs still cancels that paper's run ─────────────
    // Block ids move when a paragraph is split or merged, so jobs built
    // against the old ids describe nothing.
    llm.setChunkLimit(100000);
    // translateAll would find nothing to do — A is fully translated by now —
    // so ask for specific paragraphs, which re-translates them.
    translation.translateBlock(0);
    translation.translateBlock(1);
    translation.translateBlock(2);
    waitFor([&] { return llm.openStreams() >= 1; }, 30000);
    check("a fresh run is under way", translation.busy(),
          QStringLiteral("%1 streams").arg(llm.openStreams()));
    paper.blocks()->mergeWithNext(0);
    check("merging two paragraphs cancels the run", !translation.busy());
    check("and lets go of the model connections",
          waitFor([&] { return llm.openStreams() == 0; }, 5000),
          QStringLiteral("%1 open").arg(llm.openStreams()));

    // ── closing a tab stops that paper ────────────────────────────────
    translation.translateBlock(3);
    translation.translateBlock(4);
    translation.translateBlock(5);
    waitFor([&] { return llm.openStreams() >= 1; }, 30000);
    tabs.openPaper(QUrl::fromLocalFile(pdfB));
    waitFor([&] { return paper.paperId() == paperB; }, 20000);
    check("A is still running from the other tab",
          translation.backgroundPapers() == 1);
    int idxA = -1;
    for (int i = 0; i < tabs.count(); ++i) {
        if (tabs.urlAt(i) == QUrl::fromLocalFile(pdf))
            idxA = i;
    }
    tabs.closePaper(idxA);
    check("closing A's tab stops A", translation.backgroundPapers() == 0,
          QStringLiteral("%1 background papers")
              .arg(translation.backgroundPapers()));
    check("no request is left running for it",
          waitFor([&] { return llm.openStreams() == 0; }, 5000),
          QStringLiteral("%1 open").arg(llm.openStreams()));

    // ── two papers translating at the same time ───────────────────────
    // Reported: a second paper couldn't get going while the first was still
    // translating. The queue was one FIFO, so every paragraph of the first
    // paper was ahead of the second paper's first one.
    settings.setTargetLang(QStringLiteral("ja"));   // nothing rehydrates
    settings.setTranslationConcurrency(2);
    llm.setChunkLimit(10);   // long enough that A is still going while B works

    // B has never been segmented in this run, and auto-segmentation is off,
    // so it has no paragraphs to translate until we ask for them.
    tabs.openPaper(QUrl::fromLocalFile(pdfB));
    waitFor([&] { return paper.paperId() == paperB; }, 20000);
    paper.rebuildBlocks();
    check("paper B is segmented too",
          waitFor([&] { return !paper.extracting() && paper.blockCount() > 4; },
                  60000),
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));

    llm.resetPeak();
    tabs.openPaper(QUrl::fromLocalFile(pdf));
    waitFor([&] { return paper.paperId() == paperA; }, 20000);
    translation.translateAll();
    const int backlogA = translation.totalCount();
    check("paper A has a backlog", backlogA > 6,
          QStringLiteral("%1 queued").arg(backlogA));

    tabs.openPaper(QUrl::fromLocalFile(pdfB));
    waitFor([&] { return paper.paperId() == paperB; }, 20000);
    translation.translateAll();
    check("and paper B is asked to translate too", translation.totalCount() > 0,
          QStringLiteral("%1 queued").arg(translation.totalCount()));

    // B must make progress while A still has plenty left — that is the whole
    // point. Under the old FIFO it would sit at 0 until A was finished.
    const bool shared = waitFor([&] {
        return translation.doneCount() > 0 && translation.backgroundPapers() == 1;
    }, 60000);
    check("both papers progress at once", shared,
          QStringLiteral("B %1/%2 done, %3 other paper(s) running")
              .arg(translation.doneCount()).arg(translation.totalCount())
              .arg(translation.backgroundPapers()));

    // ── the number of lanes is a setting ──────────────────────────────
    check("the setting is the cap: two at a time, and two were used",
          llm.peakStreams() == 2,
          QStringLiteral("peak %1").arg(llm.peakStreams()));
    llm.resetPeak();
    settings.setTranslationConcurrency(5);
    const bool widened = waitFor([&] { return llm.openStreams() > 2; }, 20000);
    check("raising it takes effect on the run already going", widened,
          QStringLiteral("%1 open").arg(llm.openStreams()));
    pump(3000);
    check("and the new cap is not exceeded either", llm.peakStreams() == 5,
          QStringLiteral("peak %1").arg(llm.peakStreams()));

    translation.cancelPaper(paperA);
    translation.cancelPaper(paperB);
    waitFor([&] { return llm.openStreams() == 0; }, 5000);

    QDir(root).removeRecursively();
    { QSettings s; s.clear(); s.sync(); }
    qInfo().noquote() << QStringLiteral("\n%1 passed, %2 failed")
                             .arg(g_pass).arg(g_fail);
    return g_fail ? 1 : 0;
}
