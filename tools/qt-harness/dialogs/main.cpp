// Opens every dialog in the app, offscreen, and fails on any QML warning.
//
// Loading Main.qml proves the window's own tree is sound, but a dialog's
// content is only exercised when it is actually shown -- which is where a
// mistyped property or an undefined binding in a delegate finally speaks up.
// This driver builds the same services main.cpp does, registers the same
// context properties, then instantiates and opens each dialog in turn.

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
#include <QGuiApplication>
#include <QList>
#include <QMetaMethod>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRectF>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>
#include <QQuickItem>
#include <QtQml>

static QStringList g_problems;
static QtMessageHandler g_prev = nullptr;

static void collect(QtMsgType type, const QMessageLogContext &ctx,
                    const QString &msg)
{
    // The Shortcut multi-binding note is pre-existing noise from Main.qml and
    // says nothing about the dialogs.
    const bool known =
        msg.contains(QStringLiteral("Only binding to one of "
                                    "multiple key bindings"))
        || msg.contains(QStringLiteral("Populating font family aliases"));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        && !known)
        g_problems.append(msg);
    if (g_prev)
        g_prev(type, ctx, msg);
}

static void pump(int ms)
{
    QDeadlineTimer t(ms);
    while (!t.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// A named claim, printed the way the loops below print theirs, and counted
// with the warnings so one tally covers everything that can go wrong here.
static void check(const QString &what, bool ok, const QString &detail = {})
{
    qInfo().noquote() << (ok ? "PASS  " : "FAIL  ") << what
                      << (detail.isEmpty() ? QString()
                                           : QStringLiteral("  - ") + detail);
    if (!ok)
        g_problems.append(detail.isEmpty()
                              ? what
                              : QStringLiteral("%1: %2").arg(what, detail));
}

// Every item drawn inside `root`, however deep. The VISUAL tree, not the
// QObject one: a QML item's QObject parent is not reliably the item it is
// drawn inside, and it is the drawn tree that has the geometry.
static void collectItems(QQuickItem *root, QList<QQuickItem *> &out)
{
    if (!root)
        return;
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *kid : kids) {
        out.append(kid);
        collectItems(kid, out);
    }
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // ── ISOLATION ───────────────────────────────────────────────────────
    // This driver no longer only reads: it saves layout presets through the
    // real LayoutPresets, which writes to the settings file. So before
    // anything is constructed, the app's whole notion of where it lives is
    // pointed somewhere throwaway -- names no shipping build answers to (never
    // D2S/AIReader), Qt's test mode for the standard directories, and the one
    // JSON settings file pinned to a scratch directory of our own. That last
    // part is the one that matters on macOS, where the native backend is
    // cfprefsd and setTestModeEnabled does NOT redirect it: a harness that
    // cleared a real settings domain is exactly the accident this prevents.
    QStandardPaths::setTestModeEnabled(true);
    const QString scratch =
        QDir::tempPath() + QStringLiteral("/ai-reader-dialogs-harness");
    QDir(scratch).removeRecursively();
    const StorageIdentity::Identity id{
        QStringLiteral("ai-reader-dialogs-test"),
        QStringLiteral("ai-reader-dialogs-test.invalid"),
        QStringLiteral("DialogHarness")};
    StorageIdentity::apply(id, scratch);

    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Belt and braces before a recursive delete: the only directory this is
    // ever allowed to be is the harness's own.
    if (root.contains(QStringLiteral("DialogHarness"))) {
        QDir(root).removeRecursively();
        QDir().mkpath(root);
    }
    { QSettings stale; stale.clear(); stale.sync(); }

    // Nothing below runs unless the writes are landing somewhere harmless.
    {
        const QSettings probe;
        const bool safe = probe.fileName().startsWith(scratch)
            && probe.format() != QSettings::NativeFormat
            && root.contains(QStringLiteral("DialogHarness"));
        check(QStringLiteral("the harness writes a settings file of its own, "
                             "not a real one"),
              safe, probe.fileName());
        if (!safe) {
            qCritical().noquote()
                << "refusing to run against" << probe.fileName();
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

    // A menu with no saved layouts on it never builds a delegate, and neither
    // does a dialog with nothing to list -- so the half of LayoutMenu and
    // ManageLayoutsDialog that draws a saved layout would go untested, which
    // is the half worth testing. (TasksPane below is seeded for the same
    // reason.) Two layouts, picked for the branches they force open:
    //
    //   Reading          panes showing, and among them a pane id no build has
    //                    ever heard of -- the dialog has to fall back to
    //                    spelling the id out rather than dropping the pane
    //   Nothing showing   every pane put away, for the "No panes showing" row
    //
    // and Reading is made the current one, so the "in use" wording is built
    // too. Widths are FRACTIONS of the window, the way Main.qml saves them:
    // a pixel count here would be a fixture that lies about the file format.
    {
        const auto pane = [](bool visible, double fraction) {
            QVariantMap m;
            m.insert(QStringLiteral("visible"), visible);
            m.insert(QStringLiteral("width"), fraction);
            return m;
        };

        QVariantMap readingPanes;
        readingPanes.insert(QStringLiteral("folder"), pane(true, 0.12));
        readingPanes.insert(QStringLiteral("pdf"), pane(true, 0.44));
        readingPanes.insert(QStringLiteral("blocks"), pane(true, 0.24));
        readingPanes.insert(QStringLiteral("chat"), pane(false, 0.10));
        readingPanes.insert(QStringLiteral("wormhole"), pane(true, 0.10));
        QVariantMap reading;
        reading.insert(QStringLiteral("order"),
                       QVariant(QStringList{QStringLiteral("folder"),
                                            QStringLiteral("pdf"),
                                            QStringLiteral("blocks"),
                                            QStringLiteral("chat"),
                                            QStringLiteral("wormhole")}));
        reading.insert(QStringLiteral("panes"), readingPanes);
        layoutPresets.save(QStringLiteral("Reading"), reading);

        QVariantMap awayPanes;
        awayPanes.insert(QStringLiteral("folder"), pane(false, 0.12));
        awayPanes.insert(QStringLiteral("pdf"), pane(false, 0.44));
        awayPanes.insert(QStringLiteral("blocks"), pane(false, 0.32));
        awayPanes.insert(QStringLiteral("chat"), pane(false, 0.12));
        QVariantMap away;
        away.insert(QStringLiteral("order"),
                    QVariant(QStringList{QStringLiteral("folder"),
                                         QStringLiteral("pdf"),
                                         QStringLiteral("blocks"),
                                         QStringLiteral("chat")}));
        away.insert(QStringLiteral("panes"), awayPanes);
        layoutPresets.save(QStringLiteral("Nothing showing"), away);

        layoutPresets.setCurrent(QStringLiteral("Reading"));
        check(QStringLiteral("two layouts are on the menu before any of it is "
                             "built"),
              layoutPresets.names().size() == 2
                  && layoutPresets.current() == QStringLiteral("Reading"),
              layoutPresets.names().join(QStringLiteral(", ")));
    }

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
    ctx->setContextProperty("profile", &projectProfile);
    ctx->setContextProperty("analysis", &analysisService);
    ctx->setContextProperty("analysisList", &analysisList);
    ctx->setContextProperty("batchAnalysis", &batchAnalysis);
    ctx->setContextProperty("research", &libraryAnalysis);
    ctx->setContextProperty("exporter", &analysisExporter);
    TaskManager taskManager(&settings);
    ctx->setContextProperty("tasks", &taskManager);
    engine.addImportPath(QStringLiteral(":/"));

    g_prev = qInstallMessageHandler(collect);

    // A window to parent the dialogs to; Overlay.overlay needs one.
    QQmlComponent windowComp(&engine);
    windowComp.setData(
        "import QtQuick\nimport QtQuick.Controls\n"
        "ApplicationWindow { width: 1200; height: 800; visible: true }",
        QUrl::fromLocalFile(QDir::currentPath() + QStringLiteral("/harness-window.qml")));
    QObject *winObj = windowComp.create();
    if (!winObj) {
        qCritical() << "harness window failed:" << windowComp.errorString();
        return 2;
    }
    auto *win = qobject_cast<QQuickWindow *>(winObj);
    pump(300);

    // A library with no papers builds no row delegate, and the row is where
    // nearly all of LibraryPane now lives: the state dot, the star, the
    // relevance and advice chips, the close-reading chip, the one-liner and
    // the per-paper menu. Seeded for the same reason the layout presets and
    // the task rows above are -- an empty list would exercise none of it.
    // Three papers, picked for the branches they force open: one untouched,
    // one interpreted and starred, one interpreted, close-read and set
    // aside.
    {
        // The store first, the sign-in second: signing in is what opens the
        // session and re-reads the cached project list, so a project written
        // afterwards would be there on disk with the controller still
        // holding an empty list -- no role, no canWrite, and every write
        // below silently refused.
        ProjectRow proj;
        proj.id = QStringLiteral("harness-project");
        proj.name = QStringLiteral("Harness project");
        proj.role = QStringLiteral("owner");
        libraryDb.replaceProjects(QList<ProjectRow>{proj});
        libraryDb.claimProjects(QStringList{proj.id},
                                QStringLiteral("harness-user"));

        qputenv("TEST_USER_ID", "harness-user");
        qputenv("TEST_USER_EMAIL", "harness@example.invalid");
        auth.startCasLogin();
        projectController.selectProject(proj.id);

        const QString untouched = libraryModel.addPaper(
            QStringLiteral("A paper nobody has read"),
            QStringLiteral("paper-untouched"), QStringLiteral("/nowhere/a.pdf"));
        const QString starred = libraryModel.addPaper(
            QStringLiteral("A paper marked for a close read"),
            QStringLiteral("paper-starred"), QStringLiteral("/nowhere/b.pdf"));
        const QString aside = libraryModel.addPaper(
            QStringLiteral("A paper already close-read, then set aside"),
            QStringLiteral("paper-aside"), QStringLiteral("/nowhere/c.pdf"));

        const QJsonObject digest{
            {QStringLiteral("oneLiner"),
             QStringLiteral("One line about what this paper is for.")},
            {QStringLiteral("relevance"),
             QJsonObject{{QStringLiteral("level"), QStringLiteral("high")}}},
            {QStringLiteral("advice"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("read_full")}}}};
        analysisStore.putPaperAnalysis(
            QStringLiteral("paper-starred"), Analysis::KindQuick, digest,
            QStringLiteral("harness-model"), QStringLiteral("hash-1"),
            Analysis::StatusOk, QString(), QStringLiteral("Starred"));
        analysisStore.putPaperAnalysis(
            QStringLiteral("paper-aside"), Analysis::KindQuick, digest,
            QStringLiteral("harness-model"), QStringLiteral("hash-2"),
            Analysis::StatusOk, QString(), QStringLiteral("Aside"));

        QJsonObject modules;
        for (const QString &id : Analysis::deepModules())
            modules.insert(id, QJsonObject{{QStringLiteral("claims"),
                                            QJsonArray{}}});
        analysisStore.putPaperAnalysis(
            QStringLiteral("paper-aside"), Analysis::KindDeep,
            QJsonObject{{QStringLiteral("modules"), modules}},
            QStringLiteral("harness-model"), QStringLiteral("hash-3"),
            Analysis::StatusOk, QString(), QStringLiteral("Aside"));

        analysisList.setToRead(starred, true);
        analysisList.setExcluded(aside, true);
        analysisList.setHideExcluded(false);
        analysisList.reload();
        check(QStringLiteral("the library pane has rows to draw before any of "
                             "it is built"),
              analysisList.rowCount() == 3 && analysisList.toReadCount() == 1
                  && analysisList.deepDoneCount() == 1,
              QStringLiteral("rows=%1 starred=%2 interpreted=%3 close-read=%4 "
                             "canWrite=%5 project=%6 item=%7")
                  .arg(analysisList.rowCount())
                  .arg(analysisList.toReadCount())
                  .arg(analysisList.interpretedCount())
                  .arg(analysisList.deepDoneCount())
                  .arg(analysisStore.canWrite())
                  .arg(projectController.currentId())
                  .arg(untouched));
    }

    const QStringList dialogs = {
        QStringLiteral("SettingsDialog"),   QStringLiteral("PasswordDialog"),
        QStringLiteral("MetadataDialog"),
        QStringLiteral("MembersDialog"),    QStringLiteral("ProjectSettingsDialog"),
        QStringLiteral("ProjectProfileDialog"),
        QStringLiteral("VisionDialog"),     QStringLiteral("ChangelogDialog"),
        QStringLiteral("QuitTasksDialog"), QStringLiteral("ResumeTasksDialog"),
        QStringLiteral("SaveLayoutDialog"), QStringLiteral("ManageLayoutsDialog"),
    };

    int opened = 0;
    for (const QString &name : dialogs) {
        const int before = g_problems.size();
        QQmlComponent comp(&engine);
        comp.setData(QStringLiteral("import QtQuick\nimport QtQuick.Controls\n"
                                    "import AiReader\n%1 { }")
                         .arg(name)
                         .toUtf8(),
                     QUrl::fromLocalFile(QDir::currentPath()
                                         + QStringLiteral("/harness-%1.qml")
                                               .arg(name)));
        QObject *obj = comp.create(engine.rootContext());
        if (!obj) {
            g_problems.append(QStringLiteral("%1 failed to create: %2")
                                  .arg(name, comp.errorString()));
            continue;
        }
        obj->setParent(win);
        if (auto *item = qobject_cast<QQuickItem *>(obj->property("contentItem")
                                                        .value<QObject *>()))
            Q_UNUSED(item);
        QMetaObject::invokeMethod(obj, "open");
        pump(400);
        QMetaObject::invokeMethod(obj, "close");
        pump(120);
        ++opened;
        const int added = g_problems.size() - before;
        qInfo().noquote() << (added == 0 ? "PASS  " : "FAIL  ") << name
                          << (added == 0 ? QString()
                                         : QStringLiteral("(%1 warnings)").arg(added));
        obj->deleteLater();
        pump(60);
    }

    // SaveLayoutDialog is never opened with a bare open(): the window calls
    // openForSave() or openForRename(name), and those are what choose the
    // verb, pre-fill the field, and decide whether a name already in use is
    // an offer to replace or a refusal. Opened the generic way above it is a
    // dialog with nothing in it, so it is opened again here twice, the way
    // the app opens it -- once for each verb, and each time carrying a name
    // that is already taken, so both clash wordings are built.
    {
        const QString taken = QStringLiteral("Nothing showing");
        for (int pass = 0; pass < 2; ++pass) {
            const bool renaming = pass == 1;
            const QString what = renaming
                ? QStringLiteral("SaveLayoutDialog openForRename(\"Reading\")")
                : QStringLiteral("SaveLayoutDialog openForSave()");
            const int before = g_problems.size();

            QQmlComponent comp(&engine);
            comp.setData("import QtQuick\nimport QtQuick.Controls\n"
                         "import AiReader\nSaveLayoutDialog { }",
                         QUrl::fromLocalFile(
                             QDir::currentPath()
                             + QStringLiteral("/harness-SaveLayoutDialog-%1.qml")
                                   .arg(pass)));
            QObject *obj = comp.create(engine.rootContext());
            if (!obj) {
                g_problems.append(QStringLiteral("%1 failed to create: %2")
                                      .arg(what, comp.errorString()));
                continue;
            }
            obj->setParent(win);
            if (renaming) {
                // A QML function's arguments are QVariant unless the file
                // typed them; ask which, rather than guessing and having
                // invokeMethod warn about the method it could not find.
                const QString name = QStringLiteral("Reading");
                if (obj->metaObject()->indexOfMethod("openForRename(QVariant)") >= 0)
                    QMetaObject::invokeMethod(obj, "openForRename",
                                              Q_ARG(QVariant, QVariant(name)));
                else
                    QMetaObject::invokeMethod(obj, "openForRename",
                                              Q_ARG(QString, name));
            } else {
                QMetaObject::invokeMethod(obj, "openForSave");
            }
            pump(400);

            // Saving pre-fills the layout on screen, which is a name that
            // exists -- that is the "already exists, saving replaces it"
            // path, built for free. Renaming has to be typed into to reach
            // its own refusal, so the other layout's name goes in the field.
            if (renaming) {
                QList<QQuickItem *> items;
                collectItems(qobject_cast<QQuickItem *>(
                                 obj->property("contentItem").value<QObject *>()),
                             items);
                for (QQuickItem *item : items)
                    if (item->inherits("QQuickTextField")) {
                        item->setProperty("text", taken);
                        break;
                    }
                pump(250);
            }

            QMetaObject::invokeMethod(obj, "close");
            pump(120);
            ++opened;
            const int added = g_problems.size() - before;
            qInfo().noquote() << (added == 0 ? "PASS  " : "FAIL  ") << what
                              << (added == 0
                                      ? QString()
                                      : QStringLiteral("(%1 warnings)").arg(added));
            obj->deleteLater();
            pump(60);
        }
    }

    // A pane with an empty model never instantiates its delegate, so the
    // interesting half of TasksPane would go unexercised. Give it one task
    // in each state a row can be drawn in.
    {
        Tasks::Request running;
        running.kind = Tasks::Kind::Translate;
        running.title = QStringLiteral("Translate");
        running.paperId = QStringLiteral("paper-a");
        running.paperTitle = QStringLiteral("Attention Is All You Need");
        running.steps = 40;
        const QString a = taskManager.submit(running, [] {}, [] {});
        taskManager.setProgress(a, 12);
        taskManager.setNote(a, QStringLiteral("Paragraph 12"));

        Tasks::Request queued;
        queued.kind = Tasks::Kind::DeepInterpret;
        queued.title = QStringLiteral("Close read");
        queued.paperId = QStringLiteral("paper-b");
        queued.paperTitle = QStringLiteral("Deep Residual Learning");
        queued.steps = 9;
        taskManager.submit(queued, [] {}, [] {});

        Tasks::Request failed;
        failed.kind = Tasks::Kind::LibraryAnalysis;
        failed.title = QStringLiteral("Research map");
        failed.paperId = QStringLiteral("paper-c");
        const QString c = taskManager.submit(failed, [] {}, [] {});
        pump(60);
        taskManager.finish(c, false, QStringLiteral("the gateway returned 502"));
    }
    pump(120);

    // Panes are not dialogs -- they are docked into the split view rather
    // than opened -- but they are built out of the same shared controls and
    // break the same way, so they are instantiated here too. Every tab's
    // subtree is declared inline, so creating one exercises all of them.
    const QStringList panes = { QStringLiteral("ResearchPane"),
                                QStringLiteral("TasksPane"),
                                // The library pane absorbed the batch dialog
                                // in 1.3.26 -- filters, a progress bar, a
                                // bulk menu and a per-row menu, all of which
                                // used to be exercised here as a dialog.
                                QStringLiteral("LibraryPane") };
    int built = 0;
    for (const QString &name : panes) {
        const int before = g_problems.size();
        QQmlComponent comp(&engine);
        comp.setData(QStringLiteral("import QtQuick\nimport QtQuick.Controls\n"
                                    "import AiReader\n%1 { width: 480; height: 720 }")
                         .arg(name)
                         .toUtf8(),
                     QUrl::fromLocalFile(QDir::currentPath()
                                         + QStringLiteral("/harness-%1.qml")
                                               .arg(name)));
        QObject *obj = comp.create(engine.rootContext());
        if (!obj) {
            g_problems.append(QStringLiteral("%1 failed to create: %2")
                                  .arg(name, comp.errorString()));
            continue;
        }
        if (auto *item = qobject_cast<QQuickItem *>(obj))
            item->setParentItem(win->contentItem());
        pump(400);
        // A menu's items are not built until it is popped, exactly as a
        // dialog's are not until it is opened -- and the library pane's two
        // menus are where most of what it can do now lives. The row menu
        // takes its values rather than the row, so it can be opened from
        // here without a real right-click.
        if (name == QStringLiteral("LibraryPane")) {
            for (const QString &menuName : {QStringLiteral("libraryBulkMenu"),
                                            QStringLiteral("libraryRowMenu")}) {
                QObject *menu = obj->findChild<QObject *>(menuName);
                if (!menu) {
                    g_problems.append(QStringLiteral("%1 has no %2")
                                          .arg(name, menuName));
                    continue;
                }
                if (menuName.endsWith(QStringLiteral("RowMenu")))
                    QMetaObject::invokeMethod(
                        menu, "openFor",
                        Q_ARG(QVariant, QStringLiteral("some-item")),
                        Q_ARG(QVariant, QStringLiteral("paper-aside")),
                        Q_ARG(QVariant, QStringLiteral("A paper")),
                        Q_ARG(QVariant, QStringLiteral("/nowhere/c.pdf")),
                        Q_ARG(QVariant, QStringLiteral("done")),
                        Q_ARG(QVariant, QStringLiteral("done")),
                        Q_ARG(QVariant, true), Q_ARG(QVariant, true));
                else
                    QMetaObject::invokeMethod(menu, "popup");
                pump(250);
                QMetaObject::invokeMethod(menu, "close");
                pump(80);
            }
        }
        ++built;
        const int added = g_problems.size() - before;
        qInfo().noquote() << (added == 0 ? "PASS  " : "FAIL  ") << name
                          << (added == 0 ? QString()
                                         : QStringLiteral("(%1 warnings)").arg(added));
        obj->deleteLater();
        pump(60);
    }

    // The selected tab's pill used to sit against the top of its bar -- 3 px
    // above it, 7 below -- because the bar was sized independently of the
    // button it frames. Geometry is the only way to see that without eyes.
    {
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport QtQuick.Controls\nimport AiReader\n"
                     "AppTabBar { width: 400\n"
                     "  AppTabButton { text: \"One\" }\n"
                     "  AppTabButton { text: \"Two\" }\n"
                     "  AppTabButton { text: \"Three\" } }",
                     QUrl::fromLocalFile(QDir::currentPath()
                                         + QStringLiteral("/harness-tabs.qml")));
        QObject *obj = comp.create(engine.rootContext());
        auto *bar = qobject_cast<QQuickItem *>(obj);
        if (!bar) {
            g_problems.append(QStringLiteral("AppTabBar failed to create: %1")
                                  .arg(comp.errorString()));
        } else {
            bar->setParentItem(win->contentItem());
            pump(300);
            QQuickItem *tab = nullptr;
            for (QQuickItem *child : bar->findChildren<QQuickItem *>())
                if (child->inherits("QQuickTabButton") && child->height() > 0) {
                    tab = child;
                    break;
                }
            if (!tab) {
                g_problems.append(QStringLiteral("no tab button found in AppTabBar"));
            } else {
                const QPointF inBar = tab->mapToItem(bar, QPointF(0, 0));
                const qreal above = inBar.y();
                const qreal below = bar->height() - inBar.y() - tab->height();
                const bool centred = qAbs(above - below) <= 1.0;
                qInfo().noquote()
                    << (centred ? "PASS  " : "FAIL  ")
                    << "the selected tab sits in the middle of its bar"
                    << QStringLiteral("  - %1 px above, %2 px below (bar %3, tab %4)")
                           .arg(above).arg(below).arg(bar->height()).arg(tab->height());
                if (!centred)
                    g_problems.append(QStringLiteral("tab off-centre: %1 vs %2")
                                          .arg(above).arg(below));
            }
            obj->deleteLater();
            pump(60);
        }
    }

    // ── LayoutMenu ──────────────────────────────────────────────────────
    // A Menu is not a Dialog and cannot go through the loop above -- and it
    // is the one file here whose bug made no noise whatsoever: the tick
    // marking the layout in use was drawn ON TOP of that layout's name.
    // Nothing warns about that, and offscreen nothing shows it either. Only
    // the geometry says it.
    //
    // The cause is worth naming, because it is what the assertion is shaped
    // around: the Fusion style puts a MenuItem's check indicator at the
    // control's leftPadding and gives its own IconLabel a matching left
    // padding to clear it, so a contentItem written from scratch -- which
    // starts at x = 0 of the content area -- lands underneath the tick. So
    // the claim is: whatever draws the layout's name must start at or past
    // the right edge of whatever sits to its left, the names must all start
    // in the same column, and the row must be wide enough to want both.
    {
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport QtQuick.Controls\nimport AiReader\n"
                     "LayoutMenu { }",
                     QUrl::fromLocalFile(QDir::currentPath()
                                         + QStringLiteral("/harness-LayoutMenu.qml")));
        QObject *obj = comp.create(engine.rootContext());
        if (!obj) {
            g_problems.append(QStringLiteral("LayoutMenu failed to create: %1")
                                  .arg(comp.errorString()));
        } else {
            obj->setParent(win);
            obj->setProperty("parent", QVariant::fromValue(win->contentItem()));
            // Since 6.8 a Menu wants a window of its own, and an offscreen run
            // never exposes one, so its rows would never be laid out. Drawn
            // inside the window we already have, they are.
            if (obj->metaObject()->indexOfProperty("popupType") >= 0)
                obj->setProperty("popupType", 0);      // Popup.Item
            QMetaObject::invokeMethod(obj, "open");
            pump(500);

            // What the menu asks the window for. Renaming and deleting moved
            // into ManageLayoutsDialog; a menu still shouting the old signals
            // is a menu with two answers to the same question.
            QStringList sigs;
            const QMetaObject *mo = obj->metaObject();
            for (int i = 0; i < mo->methodCount(); ++i)
                if (mo->method(i).methodType() == QMetaMethod::Signal)
                    sigs.append(QString::fromLatin1(mo->method(i).name()));
            check(QStringLiteral("the menu asks the window for the three things "
                                 "it can do"),
                  sigs.contains(QStringLiteral("applyRequested"))
                      && sigs.contains(QStringLiteral("saveRequested"))
                      && sigs.contains(QStringLiteral("manageRequested")));
            check(QStringLiteral("...and not for the two the manage dialog took "
                                 "over"),
                  !sigs.contains(QStringLiteral("renameRequested"))
                      && !sigs.contains(QStringLiteral("deleteRequested")));

            const QStringList names = layoutPresets.names();
            const int count = obj->property("count").toInt();
            QList<QQuickItem *> rows;
            for (int i = 0; i < count; ++i) {
                QQuickItem *row = nullptr;
                if (!QMetaObject::invokeMethod(obj, "itemAt",
                                               Q_RETURN_ARG(QQuickItem *, row),
                                               Q_ARG(int, i)))
                    break;
                if (row && names.contains(row->property("text").toString()))
                    rows.append(row);
            }
            if (rows.isEmpty()) {          // itemAt() unavailable: walk instead
                QList<QQuickItem *> items;
                collectItems(qobject_cast<QQuickItem *>(
                                 obj->property("contentItem").value<QObject *>()),
                             items);
                for (QQuickItem *item : items)
                    if (item->inherits("QQuickMenuItem")
                        && names.contains(item->property("text").toString()))
                        rows.append(item);
            }
            check(QStringLiteral("the menu has a row for every saved layout"),
                  rows.size() == names.size(),
                  QStringLiteral("%1 rows among %2 items, for %3 layouts")
                      .arg(rows.size()).arg(count).arg(names.size()));

            qreal column = 0;             // the widest tick column of any row
            bool anyTick = false;
            QList<qreal> lefts;
            for (QQuickItem *row : rows) {
                const QString name = row->property("text").toString();
                QList<QQuickItem *> items;
                collectItems(row, items);

                // Whatever actually renders the name -- the style's own label
                // inside an IconLabel, or one the menu declares itself.
                QQuickItem *label = nullptr;
                for (QQuickItem *item : items)
                    if (item->inherits("QQuickText")
                        && item->property("text").toString() == name) {
                        label = item;
                        break;
                    }
                if (!label)
                    for (QQuickItem *item : items)
                        if (item->property("text").toString().contains(name)) {
                            label = item;
                            break;
                        }
                if (!label) {
                    check(QStringLiteral("the row for “%1” draws its name").arg(name),
                          false, QStringLiteral("nothing in the row renders it"));
                    continue;
                }

                const QRectF labelRect = label->mapRectToItem(
                    row, QRectF(0, 0, label->width(), label->height()));
                // Text carries padding of its own, and it is the drawn glyphs
                // that have to clear the tick, not the item's edge.
                const qreal labelLeft =
                    labelRect.left() + label->property("leftPadding").toReal();
                lefts.append(labelLeft);

                // How far right the tick column reaches, measured rather than
                // assumed: the style's check indicator if the row still uses
                // one, and anything the menu draws to the left of the name
                // inside the row's own layout if it does not.
                qreal tickRight = 0;
                QString drawnBy;
                if (auto *ind = row->property("indicator").value<QQuickItem *>()) {
                    if (ind->property("visible").toBool() && ind->width() > 0) {
                        tickRight = ind->mapRectToItem(
                                           row, QRectF(0, 0, ind->width(), ind->height()))
                                        .right();
                        drawnBy = QStringLiteral("the check indicator");
                    }
                }
                if (QQuickItem *line = label->parentItem()) {
                    const QList<QQuickItem *> siblings = line->childItems();
                    for (QQuickItem *sib : siblings) {
                        if (sib == label || !sib->property("visible").toBool()
                            || sib->width() <= 0)
                            continue;
                        const QRectF r = sib->mapRectToItem(
                            row, QRectF(0, 0, sib->width(), sib->height()));
                        if (r.left() < labelRect.left() && r.right() > tickRight) {
                            tickRight = r.right();
                            drawnBy = QStringLiteral("the item beside the name");
                        }
                    }
                }
                if (tickRight > 0) {
                    anyTick = true;
                    column = qMax(column, tickRight);
                }

                check(QStringLiteral("the name of “%1” is drawn clear of the tick")
                          .arg(name),
                      labelLeft >= tickRight - 0.5,
                      QStringLiteral("name from x=%1, %2 ends at x=%3")
                          .arg(labelLeft)
                          .arg(drawnBy.isEmpty() ? QStringLiteral("nothing to its left")
                                                 : drawnBy)
                          .arg(tickRight));

                const qreal wanted = tickRight + label->implicitWidth();
                check(QStringLiteral("...and the row asks for the width of both"),
                      row->implicitWidth() >= wanted - 0.5,
                      QStringLiteral("“%1” implicitWidth %2, tick + name want %3")
                          .arg(name).arg(row->implicitWidth()).arg(wanted));
            }

            // A layout is in use, so a tick is on the menu; if nothing could
            // be found drawing one, the two checks above were measuring
            // against nothing and their passes meant nothing.
            check(QStringLiteral("something draws the tick for the layout in use"),
                  anyTick,
                  QStringLiteral("in use: %1").arg(layoutPresets.current()));
            check(QStringLiteral("the tick column is a column, not a sliver"),
                  column >= 12,
                  QStringLiteral("it ends at x=%1").arg(column));

            QStringList shown;
            bool lined = !lefts.isEmpty();
            for (qreal left : lefts) {
                shown.append(QString::number(left));
                if (left < column - 0.5)
                    lined = false;
            }
            check(QStringLiteral("every layout's name starts in the same column"),
                  lined,
                  QStringLiteral("names from x = %1, column ends at x=%2")
                      .arg(shown.join(QStringLiteral(", "))).arg(column));

            QMetaObject::invokeMethod(obj, "close");
            pump(120);
            obj->deleteLater();
            pump(60);
        }
    }

    qInstallMessageHandler(g_prev);
    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 dialogs and %2 panes built, %3 warnings")
                             .arg(opened)
                             .arg(built)
                             .arg(g_problems.size());
    for (const QString &p : g_problems)
        qInfo().noquote() << "  " << p;
    return g_problems.isEmpty() ? 0 : 1;
}
