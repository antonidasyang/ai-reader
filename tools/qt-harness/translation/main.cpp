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
#include "TranslationService.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QGuiApplication>
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
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

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

    paper.openPdf(QUrl::fromLocalFile(pdf));
    paper.rebuildBlocks();
    const bool segmented =
        waitFor([&] { return !paper.extracting() && paper.blockCount() > 8; },
                60000);
    check("the fixture segmented", segmented,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));

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

    QDir(root).removeRecursively();
    qInfo().noquote() << QStringLiteral("\n%1 passed, %2 failed")
                             .arg(g_pass).arg(g_fail);
    return g_fail ? 1 : 0;
}
