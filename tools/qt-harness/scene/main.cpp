// The real main window, at the size of the field log's (2560x1369), with a
// project as big as a real one behind it: forty interpreted papers, answers as
// long as a talkative model writes them, the project-wide analyses, and a
// close reading of the two papers that are open.
//
// The v1.3.38 field log froze for 16-20 s whenever an interpretation landed
// and for seconds after every paper switch, with 147377 items in the window.
// None of it was slow C++: it was QML rebuilding what nobody was looking at.
// Every tab of the research pane was built, six of them hidden, and all seven
// were rebuilt on every interpretation stored anywhere in the project; the
// close reading rebuilt every open part each time one part landed, and was
// rebuilt on every switch while its tab was not even showing. This times those
// moments against budgets the old code missed by 3-10x.
//
// SCENE_VERBOSE=1 prints where the items are and which events ran.
#include "AnalysisExporter.h"
#include "AnalysisListModel.h"
#include "AnalysisService.h"
#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "BatchAnalysisService.h"
#include "ChatContent.h"
#include "ChatService.h"
#include "CursorUtil.h"
#include "FileSyncService.h"
#include "ImportService.h"
#include "UrlImportService.h"
#include "LayoutPresets.h"
#include "LayoutSettings.h"
#include "Library.h"
#include "LibraryDb.h"

#include <QJsonArray>
#include <QJsonObject>
#include "LibraryModel.h"
#include "LibraryAnalysisService.h"
#include "TaskManager.h"
#include "MarkdownRenderer.h"
#include "MetadataService.h"
#include "PaperController.h"
#include "PaperSource.h"
#include "PaperSyncService.h"
#include "PdfSelectionModel.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"
#include "SearchService.h"
#include "Settings.h"
#include "StorageIdentity.h"
#include "StructureService.h"
#include "SyncEngine.h"
#include "Tabs.h"
#include "TocService.h"
#include "TranslationService.h"
#include "UpdateChecker.h"
#include "VisionService.h"


#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QMetaEnum>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QtQml>
#include <algorithm>
#include <functional>
#include <cstdio>


#include "AnalysisPrompts.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

// Fills a JSON schema the way a talkative model would: every property, K
// entries in every array, real paper ids wherever a paper id goes.
static QStringList g_ids;
static int g_k = 5;
static int g_seq = 0;
static QJsonValue fill(const QJsonObject &sch, const QString &name, int depth)
{
    const QString type = sch.value("type").toString();
    if (sch.contains("enum")) return sch.value("enum").toArray().at(g_seq++ % qMax(1, int(sch.value("enum").toArray().size())));
    if (type == "object") {
        QJsonObject o;
        const QJsonObject props = sch.value("properties").toObject();
        for (auto it = props.begin(); it != props.end(); ++it)
            o.insert(it.key(), fill(it.value().toObject(), it.key(), depth + 1));
        return o;
    }
    if (type == "array") {
        QJsonArray a;
        const QJsonObject item = sch.value("items").toObject();
        const bool ids = name.contains("paper", Qt::CaseInsensitive) && item.value("type").toString() == "string";
        const int k = ids ? qMin(int(g_ids.size()), g_k * 2) : g_k;
        for (int i = 0; i < k; ++i)
            a.append(ids ? QJsonValue(g_ids.at((g_seq++ * 7) % g_ids.size())) : fill(item, name, depth + 1));
        return a;
    }
    if (type == "integer" || type == "number") return (g_seq++ % 9) + 1;
    if (type == "boolean") return (g_seq++ % 2) == 0;
    if (name.contains("paperId", Qt::CaseInsensitive) && !g_ids.isEmpty()) return g_ids.at(g_seq++ % g_ids.size());
    if (name == "blockId") return g_seq++ % 40;
    if (name == "page") return (g_seq++ % 12) + 1;
    return QStringLiteral("A sentence of the length a model writes for %1, with enough words in it to wrap once or twice across a pane %2.").arg(name).arg(g_seq++);
}
static QJsonObject fillObj(const QJsonObject &sch) { return fill(sch, QString(), 0).toObject(); }

// SCENE_VERBOSE: every top-level event, by kind and receiver.
struct Tally { quint64 n = 0; qint64 us = 0; };
static QHash<QByteArray, Tally> g_tally;
static bool g_tallying = false;

class ProbeApp : public QGuiApplication
{
public:
    using QGuiApplication::QGuiApplication;
    bool notify(QObject *r, QEvent *e) override
    {
        if (!g_tallying || m_depth > 0 || QThread::currentThread() != thread())
            return QGuiApplication::notify(r, e);
        const char *cls = r ? r->metaObject()->className() : "(none)";
        const QObject *p = r ? r->parent() : nullptr;
        const char *pc = p ? p->metaObject()->className() : "nothing";
        const char *ev = QMetaEnum::fromType<QEvent::Type>().valueToKey(int(e->type()));
        QByteArray key = QByteArray(ev ? ev : "?") + " -> " + cls + " (in " + pc + ")";
        ++m_depth;
        QElapsedTimer t; t.start();
        const bool h = QGuiApplication::notify(r, e);
        const qint64 us = t.nsecsElapsed() / 1000;
        --m_depth;
        auto &x = g_tally[key]; ++x.n; x.us += us;
        return h;
    }
private:
    int m_depth = 0;
};

static qint64 pump(int ms)
{
    QDeadlineTimer t(ms);
    qint64 longest = 0;
    QElapsedTimer turn;
    while (!t.hasExpired()) {
        turn.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        longest = qMax(longest, turn.elapsed());
    }
    return longest;
}

static int countItems(QQuickItem *it)
{
    if (!it) return 0;
    int n = 1;
    for (QQuickItem *c : it->childItems()) n += countItems(c);
    return n;
}

static QString label(QQuickItem *it)
{
    QString s = QString::fromLatin1(it->metaObject()->className());
    if (!it->objectName().isEmpty()) s += "#" + it->objectName();
    if (auto *ctx = qmlContext(it)) {
        const QString id = ctx->nameForObject(it);
        if (!id.isEmpty()) s += " id=" + id;
    }
    if (!it->isVisible()) s += " [hidden]";
    return s;
}

// Every subtree holding at least `min` items, indented, biggest first.
static void heavy(QQuickItem *it, int min, int depth)
{
    QList<QPair<int, QQuickItem *>> kids;
    for (QQuickItem *c : it->childItems()) kids.append({countItems(c), c});
    std::sort(kids.begin(), kids.end(), [](auto a, auto b) { return a.first > b.first; });
    int shown = 0;
    for (auto &k : kids) {
        if (k.first < min) break;
        std::printf("%*s%7d  %s\n", depth * 2, "", k.first, qPrintable(label(k.second)));
        if (++shown <= 6) heavy(k.second, min, depth + 1);
    }
}

static void byClass(QQuickItem *it, QHash<QByteArray, int> &h)
{
    for (QQuickItem *c : it->childItems()) { ++h[c->metaObject()->className()]; byClass(c, h); }
}

static void census(QQuickWindow *win, const char *when)
{
    const int total = countItems(win->contentItem());
    std::printf("\n=== %s: %d items in a %dx%d window\n", when, total, win->width(), win->height());
    heavy(win->contentItem(), qMax(200, total / 40), 1);
    QHash<QByteArray, int> h; byClass(win->contentItem(), h);
    QList<QPair<int, QByteArray>> v;
    for (auto it = h.begin(); it != h.end(); ++it) v.append({it.value(), it.key()});
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a.first > b.first; });
    std::printf("  by class:");
    for (int i = 0; i < qMin(15, int(v.size())); ++i) std::printf(" %s=%d", v[i].second.constData(), v[i].first);
    std::printf("\n");
    std::fflush(stdout);
}

static void dumpTally(const char *what)
{
    QList<QPair<QByteArray, Tally>> v;
    quint64 n = 0; qint64 us = 0;
    for (auto it = g_tally.begin(); it != g_tally.end(); ++it) { v.append({it.key(), it.value()}); n += it->n; us += it->us; }
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a.second.us > b.second.us; });
    std::printf("\n--- events %s: %llu events, %lld ms\n", what, n, us / 1000);
    for (int i = 0; i < qMin(14, int(v.size())); ++i)
        std::printf("  %7llu x %7lld ms  %s\n", v[i].second.n, v[i].second.us / 1000, v[i].first.constData());
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a.second.n > b.second.n; });
    std::printf("  by count:\n");
    for (int i = 0; i < qMin(8, int(v.size())); ++i)
        std::printf("  %7llu x %7lld ms  %s\n", v[i].second.n, v[i].second.us / 1000, v[i].first.constData());
    std::fflush(stdout);
    g_tally.clear();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    ProbeApp app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QStandardPaths::setTestModeEnabled(true);
    const QString scratch = QDir::tempPath() + QStringLiteral("/ai-reader-scene-harness");
    QDir(scratch).removeRecursively();
    const StorageIdentity::Identity sid{
        QStringLiteral("ai-reader-scene-test"),
        QStringLiteral("ai-reader-scene-test.invalid"),
        QStringLiteral("SceneHarness")};
    StorageIdentity::apply(sid, scratch);
    {
        const QSettings probe;
        if (!probe.fileName().startsWith(scratch) || probe.format() == QSettings::NativeFormat) {
            qCritical().noquote() << "refusing to run against" << probe.fileName();
            return 2;
        }
    }

    qmlRegisterUncreatableType<PaperController>(
        "AiReader", 1, 0, "PaperController", QStringLiteral("context property"));
    qmlRegisterUncreatableType<Settings>(
        "AiReader", 1, 0, "Settings", QStringLiteral("context property"));
    qmlRegisterUncreatableType<TranslationService>(
        "AiReader", 1, 0, "TranslationService", QStringLiteral("context property"));
    qmlRegisterUncreatableType<TocService>(
        "AiReader", 1, 0, "TocService", QStringLiteral("context property"));
    qmlRegisterUncreatableType<VisionService>(
        "AiReader", 1, 0, "VisionService", QStringLiteral("context property"));
    qmlRegisterUncreatableType<ChatService>(
        "AiReader", 1, 0, "ChatService", QStringLiteral("context property"));
    qmlRegisterUncreatableType<Library>(
        "AiReader", 1, 0, "Library", QStringLiteral("context property"));
    qmlRegisterUncreatableType<LayoutSettings>(
        "AiReader", 1, 0, "LayoutSettings", QStringLiteral("context property"));
    qmlRegisterUncreatableType<Tabs>(
        "AiReader", 1, 0, "Tabs", QStringLiteral("context property"));
    qmlRegisterUncreatableType<UpdateChecker>(
        "AiReader", 1, 0, "UpdateChecker", QStringLiteral("context property"));
    qmlRegisterUncreatableType<AnalysisService>(
        "AiReader", 1, 0, "AnalysisService", QStringLiteral("context property"));

    Settings settings;
    PaperController paperController;
    PdfSelectionModel pdfSelection(paperController.document());
    CursorUtil cursorUtil;
    StructureService structure(&settings, &paperController);
    TranslationService translation(&settings, &paperController);
    TocService toc(&settings, &paperController);
    VisionService vision(&settings, &paperController);
    ChatService chat(&settings, &paperController, &toc);
    MarkdownRenderer markdown;
    ChatContent chatContent(&markdown);
    Library library;
    LayoutSettings layoutSettings;
    LayoutPresets layoutPresets;

    Tabs tabs(&paperController);
    UpdateChecker updateChecker(&settings);
    LibraryDb libraryDb;
    ApiClient apiClient;
    AuthController auth(&apiClient);
    ProjectController projectController(&apiClient, &auth, &libraryDb);
    SyncEngine syncEngine(&apiClient, &auth, &projectController, &libraryDb);
    LibraryModel libraryModel(&libraryDb, &projectController, &syncEngine);
    MetadataService metadataService(&libraryModel, &paperController);
    SearchService searchService(&libraryDb, &projectController);
    PaperSyncService paperSync(&libraryDb, &projectController, &syncEngine,
                               &auth, &paperController, &translation, &settings);
    FileSyncService fileSync(&apiClient, &libraryDb, &projectController,
                             &syncEngine);
    ImportService importService(&libraryModel, &fileSync, &metadataService,
                                &projectController);
    UrlImportService urlImport(&importService, &projectController, &library);
    AnalysisStore analysisStore(&libraryDb, &projectController, &syncEngine,
                                &auth);
    ProjectProfileController projectProfile(&analysisStore);
    AnalysisService analysisService(&settings, &paperController, &analysisStore,
                                    &projectProfile);
    PaperSource paperSource(&libraryDb, &libraryModel, &projectController,
                            &fileSync);
    AnalysisListModel analysisList(&libraryDb, &libraryModel, &projectController,
                                   &analysisStore);
    BatchAnalysisService batchAnalysis(&settings, &analysisStore,
                                       &projectProfile, &paperSource,
                                       &analysisList);
    LibraryAnalysisService libraryAnalysis(&settings, &analysisStore,
                                           &projectController, &projectProfile);
    AnalysisExporter analysisExporter(&analysisStore, &projectController,
                                      &projectProfile, &libraryAnalysis);

    QQmlApplicationEngine engine;
    auto *ctx = engine.rootContext();
    ctx->setContextProperty("paperController", &paperController);
    ctx->setContextProperty("pdfSelection", &pdfSelection);
    ctx->setContextProperty("structure", &structure);
    ctx->setContextProperty("cursorUtil", &cursorUtil);
    ctx->setContextProperty("settings", &settings);
    ctx->setContextProperty("translation", &translation);
    ctx->setContextProperty("toc", &toc);
    ctx->setContextProperty("vision", &vision);
    ctx->setContextProperty("chat", &chat);
    ctx->setContextProperty("markdown", &markdown);
    ctx->setContextProperty("chatContent", &chatContent);
    ctx->setContextProperty("library", &library);
    ctx->setContextProperty("layoutSettings", &layoutSettings);
    ctx->setContextProperty("layouts", &layoutPresets);
    ctx->setContextProperty("tabs", &tabs);
    ctx->setContextProperty("updates", &updateChecker);
    ctx->setContextProperty("libraryDb", &libraryDb);
    ctx->setContextProperty("auth", &auth);
    ctx->setContextProperty("projects", &projectController);
    ctx->setContextProperty("sync", &syncEngine);
    ctx->setContextProperty("libraryModel", &libraryModel);
    ctx->setContextProperty("metadata", &metadataService);
    ctx->setContextProperty("search", &searchService);
    ctx->setContextProperty("paperSync", &paperSync);
    ctx->setContextProperty("fileSync", &fileSync);
    ctx->setContextProperty("importer", &importService);
    ctx->setContextProperty("urlImport", &urlImport);
    ctx->setContextProperty("profile", &projectProfile);
    ctx->setContextProperty("analysis", &analysisService);
    ctx->setContextProperty("analysisList", &analysisList);
    ctx->setContextProperty("batchAnalysis", &batchAnalysis);
    ctx->setContextProperty("research", &libraryAnalysis);
    ctx->setContextProperty("exporter", &analysisExporter);
    TaskManager taskManager(&settings);
    ctx->setContextProperty("tasks", &taskManager);

    // ── a project the size of a real one ─────────────────────────────────
    // Forty interpreted papers, and the answers as long as a talkative model
    // writes them: eight entries in every list. Two of the papers are real
    // PDFs and get a close reading as well.
    const QStringList pdfs = QString::fromLocal8Bit(qgetenv("PDFS")).split(':', Qt::SkipEmptyParts);
    if (pdfs.size() < 2) {
        qCritical() << "PDFS must name two PDFs, colon-separated";
        return 2;
    }
    const int nPapers = qEnvironmentVariableIntValue("PAPERS") > 0
        ? qEnvironmentVariableIntValue("PAPERS") : 40;
    g_k = qEnvironmentVariableIntValue("K") > 0 ? qEnvironmentVariableIntValue("K") : 8;
    const bool verbose = qEnvironmentVariableIsSet("SCENE_VERBOSE");
    {
        ProjectRow proj;
        proj.id = QStringLiteral("harness-project");
        proj.name = QStringLiteral("Harness project");
        proj.role = QStringLiteral("owner");
        libraryDb.replaceProjects(QList<ProjectRow>{proj});
        libraryDb.claimProjects(QStringList{proj.id}, QStringLiteral("harness-user"));
        qputenv("TEST_USER_ID", "harness-user");
        qputenv("TEST_USER_EMAIL", "harness@example.invalid");
        auth.startCasLogin();
        projectController.selectProject(proj.id);
        for (const QString &f : pdfs) g_ids.append(PaperController::paperIdForFile(f));
        for (int i = g_ids.size(); i < nPapers; ++i) g_ids.append(QStringLiteral("paper-%1").arg(i));
        for (int i = 0; i < g_ids.size(); ++i) {
            const QString title = i < pdfs.size() ? QFileInfo(pdfs.at(i)).completeBaseName()
                                                  : QStringLiteral("A paper about something, number %1").arg(i);
            libraryModel.addPaper(title, g_ids.at(i), i < pdfs.size() ? pdfs.at(i) : QStringLiteral("/nowhere/%1.pdf").arg(i));
            analysisStore.putPaperAnalysis(g_ids.at(i), Analysis::KindQuick, fillObj(AnalysisPrompts::quickDigestSchema()),
                                           "harness-model", "h" + QString::number(i), Analysis::StatusOk, QString(), title);
        }
        for (int i = 0; i < pdfs.size(); ++i) {
            QJsonObject modules;
            for (const QString &id : Analysis::deepModules())
                modules.insert(id, fillObj(AnalysisPrompts::deepModuleSchema(id)));
            analysisStore.putPaperAnalysis(g_ids.at(i), Analysis::KindDeep, QJsonObject{{"modules", modules}},
                                           "harness-model", "d" + QString::number(i), Analysis::StatusOk, QString(),
                                           QFileInfo(pdfs.at(i)).completeBaseName());
        }
        for (const QString &kind : {Analysis::KindTaxonomy, Analysis::KindMap, Analysis::KindConsensus,
                                    Analysis::KindEvolution, Analysis::KindCoverage,
                                    Analysis::KindOpportunities, Analysis::KindActions})
            analysisStore.putLibraryAnalysis(kind, QString(), fillObj(AnalysisPrompts::librarySchema(kind)),
                                             "harness-model", "lib", g_ids.size());
        analysisList.reload();
        // The panes that are drawn from interpretations, all showing.
        for (const char *pane : {"analysis", "research", "library"})
            layoutSettings.setPaneVisible(QString::fromLatin1(pane), true);
    }
    paperController.setAutoSegment(true);
    // Segmenting a freshly opened paper would otherwise send it to the
    // structure service on the real server. Nothing leaves this machine.
    settings.setGrobidEnabled(false);

    engine.addImportPath(QStringLiteral(":/"));
    engine.loadFromModule("AiReader", "Main");
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!win) { qCritical() << "Main did not load"; return 2; }
    win->resize(2560, 1369);   // the field log's window
    win->setVisible(true);
    pump(1500);

    int passed = 0, failed = 0;
    auto check = [&](bool ok, const QString &what) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
        std::fflush(stdout);
        (ok ? passed : failed)++;
    };
    std::printf("a project of %d interpreted papers, %d entries to a list, in a %dx%d window\n",
                int(g_ids.size()), g_k, win->width(), win->height());

    // ── how much is built before anything is open ────────────────────────
    {
        const int items = countItems(win->contentItem());
        if (verbose) census(win, "before any paper is open");
        check(items <= 20000,
              QStringLiteral("the window carries at most 20000 items before a paper is open - %1 "
                             "(every research tab used to be built, six of them hidden)").arg(items));
    }

    // ── switching papers ────────────────────────────────────────────────
    {
        QList<qint64> blocked;
        for (int round = 0; round < 2; ++round) {
            for (const QString &f : pdfs) {
                g_tallying = verbose;
                QElapsedTimer t; t.start();
                tabs.openPaper(QUrl::fromLocalFile(f));
                blocked.append(t.elapsed());
                pump(4000);
                g_tallying = false;
                std::printf("      opened %s (%d paragraphs): blocked %lld ms\n",
                            qPrintable(QFileInfo(f).fileName()), paperController.blockCount(),
                            blocked.last());
                if (verbose) { dumpTally("after the switch"); census(win, "after the switch"); }
            }
        }
        const qint64 worst = *std::max_element(blocked.begin(), blocked.end());
        check(worst <= 250, QStringLiteral("a paper switch blocks the window for at most 250 ms - worst %1 ms "
                                           "(the hidden close reading was rebuilt on every one)").arg(worst));
    }

    // ── a close reading landing part by part ────────────────────────────
    // While one is written every part opens itself, so by the end all nine
    // are open, and each part that lands must cost that part, not nine.
    {
        for (QObject *o : win->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("analysis")) { o->setProperty("mode", 1); break; }
        pump(1000);
        QObject *view = nullptr;
        for (QObject *o : win->findChildren<QObject *>())
            if (QByteArray(o->metaObject()->className()).startsWith("DeepReadView")) { view = o; break; }
        check(view != nullptr, QStringLiteral("the close reading is built once its tab is showing"));
        QVariantMap open;
        for (const QString &id : Analysis::deepModules()) open.insert(id, true);
        if (view) view->setProperty("openById", open);
        pump(1500);
        const int items = countItems(win->contentItem());
        const QString cur = PaperController::paperIdForFile(paperController.pdfSource().toLocalFile());
        QJsonObject modules;
        for (const QString &id : Analysis::deepModules())
            modules.insert(id, fillObj(AnalysisPrompts::deepModuleSchema(id)));
        analysisStore.putPaperAnalysis(cur, Analysis::KindDeep, QJsonObject{{"modules", modules}},
                                       "harness-model", "d-fresh", Analysis::StatusOk, QString(), "x");
        pump(1500);
        QList<qint64> blocked;
        for (int i = 0; i < 3; ++i) {
            const QString id = Analysis::deepModules().at(i);
            modules.insert(id, fillObj(AnalysisPrompts::deepModuleSchema(id)));
            g_tallying = verbose;
            QElapsedTimer t; t.start();
            analysisStore.putPaperAnalysis(cur, Analysis::KindDeep, QJsonObject{{"modules", modules}},
                                           "harness-model", "d-again" + QString::number(i),
                                           Analysis::StatusOk, QString(), "x");
            blocked.append(t.elapsed());
            pump(1500);
            g_tallying = false;
            std::printf("      the \"%s\" part landed: blocked %lld ms\n", qPrintable(id), blocked.last());
            if (verbose) dumpTally("after it landed");
        }
        const qint64 worst = *std::max_element(blocked.begin(), blocked.end());
        check(worst <= 300, QStringLiteral("one part of an open close reading (%1 items on screen) landing "
                                           "blocks for at most 300 ms - worst %2 ms (every open part was "
                                           "rebuilt for each one)").arg(items).arg(worst));
    }

    // ── an interpretation of some other paper stored ────────────────────
    // A batch run, a collaborator's sync: nothing on screen is about it.
    {
        g_tallying = verbose;
        QElapsedTimer t; t.start();
        analysisStore.putPaperAnalysis(g_ids.at(5), Analysis::KindQuick, fillObj(AnalysisPrompts::quickDigestSchema()),
                                       "harness-model", "again", Analysis::StatusOk, QString(), "again");
        const qint64 blocked = t.elapsed();
        pump(2000);
        g_tallying = false;
        if (verbose) dumpTally("after the interpretation was stored");
        check(blocked <= 150, QStringLiteral("storing another paper's interpretation blocks for at most 150 ms "
                                             "- %1 ms (every research tab was rebuilt for it)").arg(blocked));
    }

    // ── one statement's follow-up menu ──────────────────────────────────
    // Made on demand now: it must still open, and go away after.
    {
        QQmlComponent c(&engine);
        c.setData(QByteArray(R"QML(
import QtQuick
import QtQuick.Layouts
import AiReader
ColumnLayout {
    width: 600
    ClaimBlock {
        Layout.fillWidth: true
        claim: ({ text: "A statement.", source: "experimental", type: "method",
                  evidence: [{page: 3, quote: "we see", verified: true, blockId: 4}] })
    }
}
)QML"), QUrl::fromLocalFile(QDir::currentPath() + "/claim.qml"));
        auto *it = qobject_cast<QQuickItem *>(c.create(engine.rootContext()));
        if (!it) { qCritical() << c.errorString(); return 2; }
        it->setParentItem(win->contentItem());
        pump(300);
        QQuickItem *btn = nullptr;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *i) {
            for (QQuickItem *k : i->childItems()) {
                if (btn) return;
                if (k->property("text").toString() == QStringLiteral("⋯")) { btn = k; return; }
                find(k);
            }
        };
        find(it);
        bool opened = false;
        int left = -1;
        if (btn) {
            QMetaObject::invokeMethod(btn, "clicked");
            pump(300);
            for (QObject *o : btn->findChildren<QObject *>())
                if (QByteArray(o->metaObject()->className()).startsWith("Menu_")
                    && o->property("opened").toBool()) {
                    opened = o->property("count").toInt() >= 6;
                    QMetaObject::invokeMethod(o, "close");
                }
            pump(300);
            // destroy() is a deferred delete, which processEvents() outside
            // any event loop never gets round to.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            left = 0;
            for (QObject *o : btn->findChildren<QObject *>())
                if (QByteArray(o->metaObject()->className()).startsWith("Menu_")) ++left;
        }
        check(opened && left == 0,
              QStringLiteral("a statement's follow-up menu opens when asked for, and is gone once closed "
                             "(button %1, opened %2, %3 left)")
                  .arg(btn ? "found" : "missing").arg(opened).arg(left));
        delete it;
    }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
