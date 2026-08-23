// Harness for the paper_data sync bridge: drives the real PaperController /
// TranslationService / SyncEngine / PaperSyncService stack against a fake
// backend (FakeSync) and asserts the ownership rules.
//
//   rule 1  same account, another machine  → adopted whenever we have nothing
//   rule 2  another account                → adopted only into our gaps; a
//                                            segmentation or a paragraph
//                                            translation of our own wins

#include "FakeSync.h"

#include "ApiClient.h"
#include "AuthController.h"
#include "BlockCache.h"
#include "BlockListModel.h"
#include "LibraryDb.h"
#include "PaperController.h"
#include "FileSyncService.h"
#include "PaperSyncService.h"
#include "ProjectController.h"
#include "Settings.h"
#include "SyncEngine.h"
#include "TranslationCache.h"
#include "TranslationService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
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
                      << (detail.isEmpty() ? QString() : "  — " + detail);
}

static void pump(int ms)
{
    QDeadlineTimer t(ms);
    while (!t.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
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
            sync.syncNow();   // a syncNow during a round in flight is dropped
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return cond();
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

static const QUuid kNs =
    QUuid::fromString(QStringLiteral("{4a1f2e90-7b3c-4d6a-9f21-a1b2c3d40002}"));

static QString artifactId(const QString &project, const QString &paper,
                          const QString &kind, const QString &author)
{
    const QString name = project + '|' + paper + '|' + kind + '|' + author;
    return QUuid::createUuidV5(kNs, name.toUtf8()).toString(QUuid::WithoutBraces);
}

static QString encode(const QJsonObject &inner)
{
    return QString::fromLatin1(
        qCompress(QJsonDocument(inner).toJson(QJsonDocument::Compact), 9).toBase64());
}

static QJsonObject decode(const QString &payload)
{
    return QJsonDocument::fromJson(
               qUncompress(QByteArray::fromBase64(payload.toLatin1()))).object();
}

static QJsonObject blocksPayload(const QString &paperId, int n)
{
    QJsonArray arr;
    for (int i = 0; i < n; ++i) {
        arr.append(QJsonObject{
            {"id", i}, {"ord", i}, {"page", i / 3}, {"kind", 0},
            {"text", QStringLiteral("Donor paragraph %1 — text that came down "
                                    "from the project.").arg(i)},
            {"bbox", QJsonArray{10, 20.0 + i, 400, 30}}});
    }
    return QJsonObject{{"paperId", paperId}, {"blocks", arr}};
}

static QJsonObject artifactData(const QString &paperId, const QString &kind,
                                const QString &author, const QString &email,
                                const QJsonObject &inner, int n,
                                const QString &stamp)
{
    return QJsonObject{
        {"paperId", paperId}, {"kind", kind}, {"author", author},
        {"authorEmail", email}, {"codec", "zlib-b64"}, {"n", n},
        {"payload", encode(inner)}, {"updatedAt", stamp}};
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("PaperSyncHarness");

    const QString pdfA = QString::fromLocal8Bit(qgetenv("PDF_A"));
    const QString pdfB = QString::fromLocal8Bit(qgetenv("PDF_B"));
    const QString paperA = PaperController::paperIdForFile(pdfA);
    const QString paperB = PaperController::paperIdForFile(pdfB);
    Q_ASSERT(!paperA.isEmpty() && !paperB.isEmpty() && paperA != paperB);

    // Throwaway app-data root — the org/app names above keep every cache, the
    // SQLite mirror and QSettings out of the real profile.
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    // QSettings is not under AppDataLocation (it is a plist in
    // ~/Library/Preferences keyed by the org/app names above), so wiping the
    // app-data root alone would carry the previous run's settings over.
    { QSettings stale; stale.clear(); stale.sync(); }

    const QString PROJ = QStringLiteral("11111111-2222-3333-4444-555555555555");
    const QString ME   = QStringLiteral("me-user-id");
    const QString OTHER = QStringLiteral("other-user-id");
    qputenv("TEST_USER_ID", ME.toUtf8());
    qputenv("TEST_USER_EMAIL", "me@example.test");

    FakeSync backend;
    backend.setProject(PROJ, QStringLiteral("owner"));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // A collaborator already segmented paper A into 7 paragraphs.
    backend.seed(artifactId(PROJ, paperA, "blocks", OTHER), "paper_data",
                 artifactData(paperA, "blocks", OTHER, "other@example.test",
                              blocksPayload(paperA, 7), 7, stamp));
    // ...and this same account segmented paper B into 4, on another machine.
    backend.seed(artifactId(PROJ, paperB, "blocks", ME), "paper_data",
                 artifactData(paperB, "blocks", ME, "me@example.test",
                              blocksPayload(paperB, 4), 4, stamp));

    qputenv("TEST_SERVER_URL", backend.baseUrl().toUtf8());

    Settings settings;
    PaperController paper;
    TranslationService translation(&settings, &paper);
    LibraryDb db;
    db.replaceProjects({ProjectRow{PROJ, QStringLiteral("Harness"), {},
                                   QStringLiteral("owner"), 0}});
    ApiClient api;
    AuthController auth(&api);
    ProjectController projects(&api, &auth, &db);
    SyncEngine sync(&api, &auth, &projects, &db);
    PaperSyncService paperSync(&db, &projects, &sync, &auth, &paper,
                               &translation, &settings);
    FileSyncService fileSync(&api, &db, &projects, &sync);

    auth.startCasLogin();
    projects.selectProject(PROJ);
    check("signed in and a project selected",
          auth.authenticated() && projects.currentId() == PROJ
              && projects.canWrite());

    const bool pulled = waitFor([&] {
        return !db.paperData(PROJ, paperA, QStringLiteral("blocks")).isEmpty();
    }, 8000);
    check("the pull indexed the collaborator's segmentation", pulled);
    check("paper_data is indexed, not scanned",
          db.paperData(PROJ, paperA, QStringLiteral("blocks")).size() == 1
              && db.paperData(PROJ, paperA, QStringLiteral("blocks"))
                     .first().count == 7);

    // ── rule 2: nothing of ours, so the collaborator's work is used ────
    paper.openPdf(QUrl::fromLocalFile(pdfA));
    check("opening a paper we've never segmented takes the project's copy",
          paper.blockCount() == 7,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));
    check("it is attributed to the collaborator",
          paperSync.blocksOrigin() == OTHER, paperSync.blocksOrigin());
    check("the attribution chip names them",
          paperSync.blocksOriginLabel() == QStringLiteral("other@example.test"),
          paperSync.blocksOriginLabel());
    check("the reader is told where the paragraphs came from",
          paperSync.notice().contains(QStringLiteral("other@example.test")),
          paperSync.notice());
    pump(2500);
    check("adopted paragraphs are not re-published under our name",
          backend.pushed().isEmpty(),
          QStringLiteral("%1 pushed").arg(backend.pushed().size()));

    // ── translations: ours per paragraph, theirs only in the gaps ─────
    const QString model = QStringLiteral("test-model");
    const QString prompt = TranslationCache::sha(QStringLiteral("system prompt"));
    const QString lang = QStringLiteral("zh-CN");
    const QString srcMine = QStringLiteral("Donor paragraph 1 — text that came "
                                           "down from the project.");
    const QString srcGap = QStringLiteral("Donor paragraph 2 — text that came "
                                          "down from the project.");
    translation.cache()->store(1, srcMine, model, prompt, lang,
                               QStringLiteral("MY OWN TRANSLATION"));
    pump(1200);

    QJsonArray donorEntries{
        QJsonObject{{"blockId", 1}, {"src", TranslationCache::sha(srcMine)},
                    {"model", model}, {"prompt", prompt}, {"lang", lang},
                    {"text", QStringLiteral("THEIR TRANSLATION")}},
        QJsonObject{{"blockId", 2}, {"src", TranslationCache::sha(srcGap)},
                    {"model", model}, {"prompt", prompt}, {"lang", lang},
                    {"text", QStringLiteral("THEIR GAP FILLER")}}};
    backend.seed(artifactId(PROJ, paperA, "translations", OTHER), "paper_data",
                 artifactData(paperA, "translations", OTHER,
                              QStringLiteral("other@example.test"),
                              QJsonObject{{"paperId", paperA},
                                          {"entries", donorEntries}},
                              2, stamp));
    sync.syncNow();
    waitForSync([&] {
        return !translation.cache()->lookup(2, srcGap, model, prompt, lang).isEmpty();
    }, sync, 20000);
    check("a paragraph we translated ourselves is not overwritten",
          translation.cache()->lookup(1, srcMine, model, prompt, lang)
              == QStringLiteral("MY OWN TRANSLATION"));
    check("a paragraph we never translated is filled from the project",
          translation.cache()->lookup(2, srcGap, model, prompt, lang)
              == QStringLiteral("THEIR GAP FILLER"));
    check("only our own entry is offered back to the project",
          translation.cache()->ownEntriesJson().size() == 1
              && translation.cache()->count() == 2,
          QStringLiteral("own=%1 total=%2")
              .arg(translation.cache()->ownEntriesJson().size())
              .arg(translation.cache()->count()));

    const auto transPushes = [&] {
        QList<QJsonObject> out;
        for (const QJsonObject &o : backend.pushed()) {
            if (o.value("data").toObject().value("kind").toString()
                == QLatin1String("translations"))
                out.append(o);
        }
        return out;
    };
    // Publication is throttled (15 s), so this is a wait, not a peek.
    waitFor([&] { return !transPushes().isEmpty(); }, 20000);
    check("our one translation was published",
          transPushes().size() >= 1,
          QStringLiteral("%1 pushes").arg(transPushes().size()));
    if (!transPushes().isEmpty()) {
        const QJsonObject d = transPushes().last().value("data").toObject();
        const QJsonArray entries =
            decode(d.value("payload").toString()).value("entries").toArray();
        check("the published artifact carries only what we own",
              entries.size() == 1
                  && entries.at(0).toObject().value("text").toString()
                         == QStringLiteral("MY OWN TRANSLATION"));
        check("it is keyed to this member",
              transPushes().last().value("id").toString()
                  == artifactId(PROJ, paperA, "translations", ME));
    }

    // ── rule 2, the other half: our own segmentation wins ─────────────
    backend.clearPushed();
    paper.rebuildBlocks();
    const bool segmented = waitFor([&] {
        return !paper.extracting() && paper.blockCount() > 0
               && paper.blockCount() != 7;
    }, 30000);
    check("re-segmenting locally replaces the adopted paragraphs", segmented,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));
    check("and they are ours now", paperSync.blocksOrigin().isEmpty());
    const int localCount = paper.blockCount();

    const auto blockPushes = [&] {
        QList<QJsonObject> out;
        for (const QJsonObject &o : backend.pushed()) {
            if (o.value("data").toObject().value("kind").toString()
                == QLatin1String("blocks"))
                out.append(o);
        }
        return out;
    };
    waitFor([&] { return !blockPushes().isEmpty(); }, 20000);
    check("our own segmentation is published", !blockPushes().isEmpty(),
          QStringLiteral("%1 pushes").arg(blockPushes().size()));
    if (!blockPushes().isEmpty()) {
        const QJsonObject o = blockPushes().last();
        const QJsonObject d = o.value("data").toObject();
        check("published under this member's deterministic id",
              o.value("id").toString() == artifactId(PROJ, paperA, "blocks", ME));
        check("with a payload that inflates back to the same paragraphs",
              decode(d.value("payload").toString()).value("blocks").toArray().size()
                  == localCount);
        check("and a paragraph count the index can use",
              d.value("n").toInt() == localCount);
    }

    // ── attribution the reading pane can show ─────────────────────────
    // These paragraphs are ours now, so the header chip must be gone.
    check("no attribution chip once the paragraphs are ours",
          paperSync.blocksOriginLabel().isEmpty(),
          paperSync.blocksOriginLabel());

    {
        // Real block ids and text, so TranslationService's own rehydrate is
        // what puts the labels on the rows.
        settings.setTargetLang(QStringLiteral("zh-CN"));
        settings.setTranslationPrompt(QStringLiteral("HARNESS {{lang}}"));
        // resolveLanguageName()'s mapping for zh-CN, which systemPrompt()
        // substitutes before hashing.
        const QString prompt = TranslationCache::sha(
            QStringLiteral("HARNESS Simplified Chinese (zh-CN)"));
        const QString realModel = settings.model();
        const Block *b0 = paper.blocks()->blockAt(0);
        const Block *b1 = paper.blocks()->blockAt(1);
        const Block *b2 = paper.blocks()->blockAt(2);
        Q_ASSERT(b0 && b1 && b2);

        translation.cache()->store(b0->id, b0->text, realModel, prompt,
                                   QStringLiteral("zh-CN"),
                                   QStringLiteral("MINE"));
        backend.seed(artifactId(PROJ, paperA, "translations", OTHER) + "-b",
                     "paper_data",
                     artifactData(paperA, "translations", OTHER,
                                  QStringLiteral("other@example.test"),
                                  QJsonObject{
                                      {"paperId", paperA},
                                      {"entries", QJsonArray{QJsonObject{
                                           {"blockId", b1->id},
                                           {"src", TranslationCache::sha(b1->text)},
                                           {"model", realModel},
                                           {"prompt", prompt},
                                           {"lang", "zh-CN"},
                                           {"text", "THEIRS"}}}}},
                                  1, QDateTime::currentDateTimeUtc()
                                         .toString(Qt::ISODate)));
        // ...and one of our own, as it comes back from another machine.
        backend.seed(artifactId(PROJ, paperA, "translations", ME) + "-b",
                     "paper_data",
                     artifactData(paperA, "translations", ME,
                                  QStringLiteral("me@example.test"),
                                  QJsonObject{
                                      {"paperId", paperA},
                                      {"entries", QJsonArray{QJsonObject{
                                           {"blockId", b2->id},
                                           {"src", TranslationCache::sha(b2->text)},
                                           {"model", realModel},
                                           {"prompt", prompt},
                                           {"lang", "zh-CN"},
                                           {"text", "MINE ELSEWHERE"}}}}},
                                  1, QDateTime::currentDateTimeUtc()
                                         .toString(Qt::ISODate)));
        sync.syncNow();
        waitForSync([&] {
            return !translation.cache()
                        ->lookup(b1->id, b1->text, realModel, prompt,
                                 QStringLiteral("zh-CN")).isEmpty();
        }, sync, 20000);
        translation.refreshFromCache();

        const auto originOfRow = [&](int row) {
            return paper.blocks()
                ->data(paper.blocks()->index(row),
                       BlockListModel::TranslationOriginRole).toString();
        };
        check("a paragraph we translated carries no attribution",
              originOfRow(0).isEmpty(), originOfRow(0));
        check("a collaborator's paragraph is labelled with their name",
              originOfRow(1) == QStringLiteral("other@example.test"),
              originOfRow(1));
        check("our own translation from another machine is not labelled",
              originOfRow(2).isEmpty(), originOfRow(2));
        // The bug this caught: entries coming back from our other machine
        // must stay ours, or the next publish would replace the artifact
        // with just what this machine happens to have translated.
        // The bug this caught in full: the entry that came back from our
        // other machine has to appear in what we publish, or the next push
        // would replace the artifact with only what this machine translated.
        bool republished = false, collaboratorLeaked = false;
        for (const QJsonValue &v : translation.cache()->ownEntriesJson()) {
            const QString t = v.toObject().value(QStringLiteral("text")).toString();
            if (t == QStringLiteral("MINE ELSEWHERE"))
                republished = true;
            if (t == QStringLiteral("THEIRS"))
                collaboratorLeaked = true;
        }
        check("our other machine's entries stay ours to publish", republished);
        check("a collaborator's entry is still left out of our artifact",
              !collaboratorLeaked);
        settings.setTranslationPrompt(QString());
    }

    // ── rule 1: the same account's work from another machine ──────────
    // Paper B's first open, so the notice is the one this adoption wrote.
    paperSync.dismissNotice();
    paper.openPdf(QUrl::fromLocalFile(pdfB));
    check("a paper segmented on our other machine comes back",
          paper.blockCount() == 4,
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));
    check("and counts as ours, not as somebody else's",
          paperSync.blocksOrigin().isEmpty());
    check("the reader is told it was restored, not borrowed",
          paperSync.notice().contains(QStringLiteral("another machine")),
          paperSync.notice());

    // Re-opening must not hand our paragraphs back to the collaborator's.
    paper.openPdf(QUrl::fromLocalFile(pdfA));
    check("re-opening keeps our segmentation, not the project's",
          paper.blockCount() == localCount && paperSync.blocksOrigin().isEmpty(),
          QStringLiteral("%1 paragraphs").arg(paper.blockCount()));

    // ── the opt-out ───────────────────────────────────────────────────
    // Segment paper B locally with sharing off: nothing may leave, and the
    // work must still be there to offer once sharing comes back.
    settings.setSharePaperData(false);
    paper.openPdf(QUrl::fromLocalFile(pdfB));
    backend.clearPushed();
    paper.rebuildBlocks();
    waitFor([&] { return !paper.extracting() && paper.blockCount() > 4; }, 30000);
    const int offCount = paper.blockCount();
    pump(18000);
    check("nothing leaves the machine with sharing turned off",
          blockPushes().isEmpty(),
          QStringLiteral("%1 block pushes").arg(blockPushes().size()));

    // Work done while sharing was off is offered the next time the paper is
    // opened — otherwise a library segmented before today would never reach
    // the project at all.
    settings.setSharePaperData(true);
    backend.clearPushed();
    paper.openPdf(QUrl::fromLocalFile(pdfA));
    pump(500);
    paper.openPdf(QUrl::fromLocalFile(pdfB));
    const bool offered = waitFor([&] {
        for (const QJsonObject &o : blockPushes()) {
            if (o.value("data").toObject().value("n").toInt() == offCount)
                return true;
        }
        return false;
    }, 25000);
    check("re-opening offers a segmentation done while sharing was off", offered,
          QStringLiteral("%1 block pushes").arg(blockPushes().size()));

    // ...but only once: the same payload must not keep bumping the project.
    backend.clearPushed();
    paper.openPdf(QUrl::fromLocalFile(pdfA));
    pump(500);
    paper.openPdf(QUrl::fromLocalFile(pdfB));
    pump(22000);
    check("an unchanged segmentation is not published again",
          blockPushes().isEmpty(),
          QStringLiteral("%1 block pushes").arg(blockPushes().size()));

    // ── an old server must not be sent these objects at all ──────────
    // Its body limit is 100 KB; a rejected batch would stall the outbox for
    // ordinary library edits too, so we wait for it to be upgraded.
    backend.setPushLimit(0);
    backend.clearPushed();
    sync.syncNow();
    waitFor([&] { return sync.serverPushLimit() == 0; }, 8000);
    paper.openPdf(QUrl::fromLocalFile(pdfA));
    paper.rebuildBlocks();
    waitFor([&] { return !paper.extracting() && paper.blockCount() > 0; }, 30000);
    pump(18000);
    check("a server that never advertised a push limit is left alone",
          blockPushes().isEmpty(),
          QStringLiteral("%1 block pushes").arg(blockPushes().size()));

    // ── naming a paper the library downloaded ─────────────────────────
    // Those are served from the content-addressed blob cache, so the file on
    // disk is called <sha256>.pdf — which is what the tab bar and the
    // Interpret pane used to show instead of the paper's title.
    {
        const QString itemId = QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        const QString sha = QString(64, QChar('a'));
        const auto put = [&](const QString &id, const QString &type,
                             const QJsonObject &data) {
            SyncObjectRow r;
            r.id = id;
            r.projectId = PROJ;
            r.type = type;
            r.data = data;
            r.version = 1;
            db.upsertFromServer(r);
        };
        put(itemId, QStringLiteral("item"),
            QJsonObject{{"title", "Attention Is All You Need"}});
        put(QStringLiteral("ffffffff-1111-2222-3333-444444444444"),
            QStringLiteral("attachment"),
            QJsonObject{{"itemId", itemId}, {"sha256", sha},
                        {"storageKey", "blobs/" + sha}});

        const QString blobDir = root + QStringLiteral("/library/blobs");
        QDir().mkpath(blobDir);
        const QString blob = blobDir + QChar('/') + sha + QStringLiteral(".pdf");
        QFile f(blob);
        f.open(QIODevice::WriteOnly);
        f.write("%PDF-1.4\n");
        f.close();

        check("a downloaded paper is named by its library title",
              fileSync.titleForFile(blob)
                  == QStringLiteral("Attention Is All You Need"),
              fileSync.titleForFile(blob));
        check("a file the library doesn't know keeps its filename",
              fileSync.titleForFile(pdfA).isEmpty(),
              fileSync.titleForFile(pdfA));

        // The other half: a paper added straight from disk records the path
        // it came from, and is named from that.
        put(QStringLiteral("11111111-aaaa-bbbb-cccc-dddddddddddd"),
            QStringLiteral("item"),
            QJsonObject{{"title", "A Paper Added From Disk"},
                        {"localPath", pdfA}});
        check("and a paper added from disk is named too",
              fileSync.titleForFile(pdfA)
                  == QStringLiteral("A Paper Added From Disk"),
              fileSync.titleForFile(pdfA));
    }

    QDir(root).removeRecursively();
    { QSettings s; s.clear(); s.sync(); }
    qInfo().noquote() << QStringLiteral("\n%1 passed, %2 failed")
                             .arg(g_pass).arg(g_fail);
    return g_fail ? 1 : 0;
}
