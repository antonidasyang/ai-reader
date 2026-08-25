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
#include "ApiClient.h"
#include "AuthController.h"
#include "BatchAnalysisService.h"
#include "ChatContent.h"
#include "ChatService.h"
#include "CompareService.h"
#include "CursorUtil.h"
#include "FileSyncService.h"
#include "ImportService.h"
#include "LayoutSettings.h"
#include "Library.h"
#include "LibraryDb.h"
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
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("DialogHarness");
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    { QSettings stale; stale.clear(); stale.sync(); }

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
    CompareService compareService(&settings, &analysisStore, &projectController,
                                  &projectProfile);
    LibraryAnalysisService libraryAnalysis(&settings, &analysisStore,
                                           &projectController, &projectProfile);
    AnalysisExporter analysisExporter(&analysisStore, &projectController,
                                      &projectProfile, &libraryAnalysis,
                                      &compareService);

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
    ctx->setContextProperty("compare", &compareService);
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

    const QStringList dialogs = {
        QStringLiteral("SettingsDialog"),   QStringLiteral("PromptsDialog"),
        QStringLiteral("PasswordDialog"),   QStringLiteral("MetadataDialog"),
        QStringLiteral("MembersDialog"),    QStringLiteral("ProjectSettingsDialog"),
        QStringLiteral("ProjectProfileDialog"),
        QStringLiteral("VisionDialog"),     QStringLiteral("ChangelogDialog"),
        QStringLiteral("BatchAnalysisDialog"), QStringLiteral("CompareDialog"),
        QStringLiteral("QuitTasksDialog"), QStringLiteral("ResumeTasksDialog"),
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
                                QStringLiteral("TasksPane") };
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
