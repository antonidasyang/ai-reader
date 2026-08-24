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

#include "AnalysisService.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "BlockListModel.h"
#include "LibraryDb.h"
#include "PaperController.h"
#include "PayloadCodec.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "Settings.h"
#include "SyncEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
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

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed")
                             .arg(g_pass)
                             .arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
