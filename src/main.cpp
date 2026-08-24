#include "ChatContent.h"
#include "ChatService.h"
#include "CursorUtil.h"
#include "CrashReporter.h"
#include "LayoutSettings.h"
#include "UpdateChecker.h"
#include "Library.h"
#include "LibraryDb.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "ProjectController.h"
#include "SyncEngine.h"
#include "LibraryModel.h"
#include "MetadataService.h"
#include "SearchService.h"
#include "AiArtifactService.h"
#include "PaperSyncService.h"
#include "FileSyncService.h"
#include "ImportService.h"
#include "AnalysisListModel.h"
#include "AnalysisService.h"
#include "BatchAnalysisService.h"
#include "PaperSource.h"
#include "AnalysisStore.h"
#include "ProjectProfileController.h"
#include "MarkdownRenderer.h"
#include "PaperController.h"
#include "PdfSelectionModel.h"
#include "Settings.h"
#include "StructureService.h"
#include "SummaryService.h"
#include "Tabs.h"
#include "TocService.h"
#include "TranslationService.h"
#include "VisionService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QWindow>
#include <QtQml>

namespace {

QIcon loadAppIcon()
{
    QIcon icon;
    for (int s : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/app-%1.png").arg(s),
                     QSize(s, s));
    }
    return icon;
}

// ── Persistent log file for the GUI build ──────────────────────────
// On Windows the binary uses the GUI subsystem (WIN32_EXECUTABLE
// TRUE), so qDebug / qWarning / qFatal output is silently dropped:
// no console attaches, no popup appears, the user sees the process
// exit. To make first-run failures (missing platform plugin, missing
// QML module, MicroTeX init crash, etc.) diagnosable from a packaged
// install, every Qt log message is also appended to
// <AppData>/launch.log. Capped at ~1 MB by truncating on each
// process start; that's plenty to capture a startup failure and
// keeps the file from growing unbounded across long-running
// sessions.
QString g_launchLogPath;
QtMessageHandler g_prevHandler = nullptr;
QMutex g_launchLogMutex;

void writeLaunchLog(QtMsgType type, const QMessageLogContext &ctx,
                    const QString &msg)
{
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);

    if (g_launchLogPath.isEmpty()) return;

    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG";    break;
    case QtInfoMsg:     level = "INFO";     break;
    case QtWarningMsg:  level = "WARNING";  break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg:    level = "FATAL";    break;
    }

    QMutexLocker lock(&g_launchLogMutex);
    QFile f(g_launchLogPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
       << " [" << level << "] "
       << (ctx.category ? ctx.category : "default") << ": "
       << msg << '\n';
}

void installLaunchLogger()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    g_launchLogPath = dir + QStringLiteral("/launch.log");

    // Truncate on each process start so the file doesn't balloon.
    QFile::remove(g_launchLogPath);

    g_prevHandler = qInstallMessageHandler(writeLaunchLog);
    qInfo().noquote() << "ai-reader" << QStringLiteral(AIREADER_VERSION)
                      << "starting up; log:" << g_launchLogPath;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader");
    app.setOrganizationDomain("ai-reader.local");
    app.setApplicationName("AI Reader");
    app.setWindowIcon(loadAppIcon());

    // QStandardPaths needs the org/app names above to resolve the
    // per-user AppData directory, so install the logger now — not
    // before — and any qWarning from this point on lands in the file.
    installLaunchLogger();

    // Dump every storage location at startup so users (and us) can
    // tell at a glance where the app is reading + writing — useful
    // when "I deleted the cache and it's still there" turns out to
    // mean the data lives somewhere unexpected (registry, keychain,
    // a different AppData root in a different Qt version).
    qInfo().noquote() << "Storage:";
    qInfo().noquote() << "  AppData       :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qInfo().noquote() << "  AppLocalData  :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    qInfo().noquote() << "  Cache         :"
                      << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    qInfo().noquote() << "  Config        :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    qInfo().noquote() << "  QSettings file:"
                      << QSettings().fileName();

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    qmlRegisterUncreatableType<PaperController>(
        "AiReader", 1, 0, "PaperController",
        QStringLiteral("Use the paperController context property"));
    qmlRegisterUncreatableType<Settings>(
        "AiReader", 1, 0, "Settings",
        QStringLiteral("Use the settings context property"));
    qmlRegisterUncreatableType<TranslationService>(
        "AiReader", 1, 0, "TranslationService",
        QStringLiteral("Use the translation context property"));
    qmlRegisterUncreatableType<SummaryService>(
        "AiReader", 1, 0, "SummaryService",
        QStringLiteral("Use the summary context property"));
    qmlRegisterUncreatableType<TocService>(
        "AiReader", 1, 0, "TocService",
        QStringLiteral("Use the toc context property"));
    qmlRegisterUncreatableType<VisionService>(
        "AiReader", 1, 0, "VisionService",
        QStringLiteral("Use the vision context property"));
    qmlRegisterUncreatableType<ChatService>(
        "AiReader", 1, 0, "ChatService",
        QStringLiteral("Use the chat context property"));
    qmlRegisterUncreatableType<Library>(
        "AiReader", 1, 0, "Library",
        QStringLiteral("Use the library context property"));
    qmlRegisterUncreatableType<LayoutSettings>(
        "AiReader", 1, 0, "LayoutSettings",
        QStringLiteral("Use the layoutSettings context property"));
    qmlRegisterUncreatableType<Tabs>(
        "AiReader", 1, 0, "Tabs",
        QStringLiteral("Use the tabs context property"));
    qmlRegisterUncreatableType<UpdateChecker>(
        "AiReader", 1, 0, "UpdateChecker",
        QStringLiteral("Use the updates context property"));
    qmlRegisterUncreatableType<AnalysisService>(
        "AiReader", 1, 0, "AnalysisService",
        QStringLiteral("Use the analysis context property"));

    Settings settings;

    // Start Sentry as early as possible so it can catch crashes that
    // happen during the rest of bootstrap. Internally it's a no-op
    // when the user hasn't opted in, when Sentry isn't compiled in,
    // or when the build was made without a DSN. Pair stop() with
    // aboutToQuit so queued events get a chance to flush.
    CrashReporter::start(&settings);
    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     &app, []() { CrashReporter::stop(); });
    QObject::connect(&settings, &Settings::crashReportsOptInChanged, &app, [&]() {
        // Live-toggle: enabling re-runs the (idempotent) start();
        // disabling tears the SDK back down so events stop flowing
        // without a restart.
        if (settings.crashReportsOptIn())
            CrashReporter::start(&settings);
        else
            CrashReporter::stop();
    });

    // Install translators based on the persisted ui/language setting.
    // Empty ⇒ follow QLocale::system(); otherwise load the .qm matching
    // the explicit code. qtbase translations come from the Qt install so
    // standard dialog buttons (OK/Cancel/etc.) get translated too — if
    // the qtbase file isn't present at runtime we silently skip it.
    QTranslator appTranslator;
    QTranslator qtTranslator;
    auto applyLanguage = [&](const QString &code) {
        const QLocale loc = code.isEmpty() ? QLocale::system() : QLocale(code);
        QCoreApplication::removeTranslator(&appTranslator);
        QCoreApplication::removeTranslator(&qtTranslator);
        if (appTranslator.load(loc, QStringLiteral("ai-reader"),
                               QStringLiteral("_"), QStringLiteral(":/i18n")))
            QCoreApplication::installTranslator(&appTranslator);
        // Qt's own strings (OK/Cancel/Close on standard buttons...).
        // QLibraryInfo points at the BUILD machine's Qt install, which
        // doesn't exist on end-user machines — packaged builds ship
        // the catalogs beside the exe (windeployqt: translations\
        // with merged qt_<locale>.qm) or in the mac bundle's
        // Resources. Try each location, and both catalog names.
        const QStringList qtTrDirs = {
            QLibraryInfo::path(QLibraryInfo::TranslationsPath),
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/translations"),
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/../Resources/translations"),
        };
        bool qtLoaded = false;
        for (const QString &dir : qtTrDirs) {
            if (qtTranslator.load(loc, QStringLiteral("qtbase"),
                                  QStringLiteral("_"), dir)
                || qtTranslator.load(loc, QStringLiteral("qt"),
                                     QStringLiteral("_"), dir)) {
                qtLoaded = true;
                break;
            }
        }
        if (qtLoaded)
            QCoreApplication::installTranslator(&qtTranslator);
        else
            qWarning() << "No Qt base translations found for"
                       << loc.name() << "- standard buttons stay English";
    };
    applyLanguage(settings.uiLanguage());

    PaperController paperController;
    PdfSelectionModel pdfSelection(paperController.document());
    CursorUtil cursorUtil;
    StructureService structure(&settings, &paperController);
    TranslationService translation(&settings, &paperController);
    SummaryService summary(&settings, &paperController);
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
    AiArtifactService aiArtifactService(&libraryDb, &projectController,
                                        &syncEngine, &auth, &paperController);
    // Bridges the two big per-paper caches (paragraph segmentation and
    // translations) to the project, so the same account on another machine —
    // and collaborators who haven't done the work — get them for free.
    PaperSyncService paperSync(&libraryDb, &projectController, &syncEngine,
                               &auth, &paperController, &translation,
                               &settings);
    FileSyncService fileSync(&apiClient, &libraryDb, &projectController,
                             &syncEngine);
    ImportService importService(&libraryModel, &fileSync, &metadataService,
                                &projectController);
    // The interpretation layer (§2–§15 of the paper-interpretation spec):
    // AnalysisStore persists every generated reading as an ordinary synced
    // object, and the research profile steers every prompt so the answers
    // are about this project rather than papers in general.
    AnalysisStore analysisStore(&libraryDb, &projectController, &syncEngine,
                                &auth);
    ProjectProfileController projectProfile(&analysisStore);
    AnalysisService analysisService(&settings, &paperController, &analysisStore,
                                    &projectProfile);
    // Interpreting papers nobody has opened (§7): PaperSource fetches and
    // segments them one at a time behind the reader's back, the batch runs
    // several model calls over the results, and the list model is what the
    // dialog shows.
    PaperSource paperSource(&libraryDb, &libraryModel, &projectController,
                            &fileSync);
    AnalysisListModel analysisList(&libraryDb, &libraryModel,
                                   &projectController, &analysisStore);
    BatchAnalysisService batchAnalysis(&settings, &analysisStore,
                                       &projectProfile, &paperSource,
                                       &analysisList);

    // Auto-segmentation is a Settings switch, but PaperController must not
    // depend on Settings (it predates it and is constructed first), so the
    // value is pushed in and kept in sync here.
    paperController.setAutoSegment(settings.autoSegment());
    QObject::connect(&settings, &Settings::autoSegmentChanged,
                     &paperController, [&]() {
                         paperController.setAutoSegment(settings.autoSegment());
                     });

    // A translation run belongs to the paper it was started on and keeps going
    // when the reader switches tabs. Closing the tab is what ends it —
    // otherwise a paper nobody has open would go on spending tokens.
    QObject::connect(&tabs, &Tabs::paperClosed, &translation,
                     [&translation](const QUrl &url) {
                         if (!url.isLocalFile())
                             return;
                         translation.cancelPaper(
                             PaperController::paperIdForFile(url.toLocalFile()));
                     });

    // A paper opened from the library lives in the content-addressed blob
    // cache, so its filename is a sha256. Wherever the app shows "which paper
    // is this", prefer the library's title for it.
    auto paperDisplayName = [&fileSync](const QUrl &url) -> QString {
        if (!url.isLocalFile())
            return {};
        return fileSync.titleForFile(url.toLocalFile());
    };
    tabs.setTitleResolver(paperDisplayName);
    // Titles land with a sync, after the tabs are already drawn.
    QObject::connect(&syncEngine, &SyncEngine::projectSynced, &tabs,
                     [&tabs](const QString &) { tabs.refreshTitles(); });

    QObject::connect(&paperController, &PaperController::pdfSourceChanged,
                     &summary, [&]() {
                         const QString title =
                             paperDisplayName(paperController.pdfSource());
                         summary.setPaperTitle(title.isEmpty()
                                                   ? paperController.fileName()
                                                   : title);
                     });
    QObject::connect(&paperController, &PaperController::pdfSourceChanged,
                     &analysisService, [&]() {
                         const QString title =
                             paperDisplayName(paperController.pdfSource());
                         analysisService.setPaperTitle(
                             title.isEmpty() ? paperController.fileName()
                                             : title);
                     });
    // GROBID → TOC wiring: when StructureService successfully applies
    // a TEI segmentation it also extracts the section outline; hand it
    // to the TOC pipeline, which adopts it only when the user doesn't
    // already have a TOC (LLM stays the fallback / explicit action).
    QObject::connect(&structure, &StructureService::outlineExtracted,
                     &toc, &TocService::adoptStructuredOutline);
    // Triple-click paragraph selection follows the block model
    // (clusterer or GROBID), so it always matches the reading pane.
    QObject::connect(&paperController, &PaperController::blocksChanged,
                     &pdfSelection, [&paperController, &pdfSelection]() {
                         pdfSelection.setParagraphs(
                             paperController.blocks()->allBlocks());
                     });
    // The selection model's background builder opens its own document
    // instance — keep it pointed at the current file + password.
    auto syncSelectionSource = [&paperController, &pdfSelection]() {
        const QUrl u = paperController.pdfSource();
        pdfSelection.setSource(u.isLocalFile() ? u.toLocalFile()
                                               : QString(),
                               paperController.pdfPassword());
    };
    QObject::connect(&paperController, &PaperController::pdfSourceChanged,
                     &pdfSelection, syncSelectionSource);
    QObject::connect(&paperController, &PaperController::pdfPasswordChanged,
                     &pdfSelection, syncSelectionSource);
    syncSelectionSource();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("paperController", &paperController);
    engine.rootContext()->setContextProperty("pdfSelection", &pdfSelection);
    engine.rootContext()->setContextProperty("structure", &structure);
    engine.rootContext()->setContextProperty("cursorUtil", &cursorUtil);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("translation", &translation);
    engine.rootContext()->setContextProperty("summary", &summary);
    engine.rootContext()->setContextProperty("toc", &toc);
    engine.rootContext()->setContextProperty("vision", &vision);
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("markdown", &markdown);
    engine.rootContext()->setContextProperty("chatContent", &chatContent);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("layoutSettings", &layoutSettings);
    engine.rootContext()->setContextProperty("tabs", &tabs);
    engine.rootContext()->setContextProperty("updates", &updateChecker);
    engine.rootContext()->setContextProperty("libraryDb", &libraryDb);
    engine.rootContext()->setContextProperty("auth", &auth);
    engine.rootContext()->setContextProperty("projects", &projectController);
    engine.rootContext()->setContextProperty("sync", &syncEngine);
    engine.rootContext()->setContextProperty("libraryModel", &libraryModel);
    engine.rootContext()->setContextProperty("metadata", &metadataService);
    engine.rootContext()->setContextProperty("search", &searchService);
    engine.rootContext()->setContextProperty("aiArtifacts", &aiArtifactService);
    engine.rootContext()->setContextProperty("paperSync", &paperSync);
    engine.rootContext()->setContextProperty("fileSync", &fileSync);
    engine.rootContext()->setContextProperty("importer", &importService);
    engine.rootContext()->setContextProperty("profile", &projectProfile);
    engine.rootContext()->setContextProperty("analysis", &analysisService);
    engine.rootContext()->setContextProperty("analysisList", &analysisList);
    engine.rootContext()->setContextProperty("batchAnalysis", &batchAnalysis);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // Re-load translators and ask the QML engine to re-evaluate every
    // qsTr() binding when the user picks a new UI language. C++ tr()
    // strings already in flight (cached errors, etc.) won't change
    // until they're regenerated.
    QObject::connect(&settings, &Settings::uiLanguageChanged, &app, [&]() {
        applyLanguage(settings.uiLanguage());
        engine.retranslate();
    });

    // qt_add_qml_module bakes our QML files into the resource system
    // at qrc:/AiReader/... and the qmldir at qrc:/AiReader/qmldir.
    // Adding :/ to the engine's import path makes that qmldir
    // discoverable by name; without this an out-of-tree windeployqt
    // copy of dist\\AiReader\\qmldir (if present and stale) can mask
    // the embedded one and the load fails with
    //     Module \"AiReader\" contains no type named \"Main\"
    // even though the C++ side of the module is fully registered.
    engine.addImportPath(QStringLiteral(":/"));
    qInfo().noquote() << "QML import paths:" << engine.importPathList();

    engine.loadFromModule("AiReader", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Failed to instantiate AiReader/Main — check the lines"
                       " above for QML errors.";
        return 1;
    }

    // Auto-check for updates on launch if the user opted in. Deferred
    // a few seconds so the network call doesn't compete with the
    // initial QML render — the banner appearing two seconds in is
    // fine; a stuttery launch is not.
    if (settings.autoCheckUpdates()) {
        QTimer::singleShot(2500, &updateChecker, &UpdateChecker::checkNow);
    }

    // Re-open the previously open papers (if any) once the QML scene
    // is live so Connections like the password-prompt dialog can
    // react to the load. The folder pane already restored its state
    // inside Library's constructor — the model is read-only there so
    // it can safely run before QML is up.
    //
    // Tabs is now the source of truth for the open-paper list. If
    // there is no saved tab list yet (fresh install or first run
    // after upgrade) we fall back to PaperController's legacy
    // single-paper restore so existing users don't see a blank
    // window on first launch with the new build.
    if (!tabs.restoreSession())
        paperController.restoreLast();

    // Restore + persist the main window's geometry and visibility.
    // Done in C++ (rather than via Qt.labs.settings in QML) because
    // that module isn't shipped with every Qt install.
    //
    // Persistence is event-driven, not shutdown-driven: aboutToQuit
    // fires *after* the close event has already flipped visibility to
    // Hidden, so guarding "save geometry only when Windowed" there
    // would always skip the write. Instead we listen to the window's
    // own geometry/visibility signals and snapshot via a debounced
    // timer — collapses bursts (e.g. width and visibility both change
    // when maximizing) into one save once the dust settles.
    if (auto *root = engine.rootObjects().value(0)) {
        if (auto *win = qobject_cast<QQuickWindow *>(root)) {
            QSettings ws;
            ws.beginGroup(QStringLiteral("window"));
            const bool hasGeom = ws.contains(QStringLiteral("width"))
                              && ws.contains(QStringLiteral("height"));
            if (hasGeom) {
                win->setWidth(ws.value(QStringLiteral("width")).toInt());
                win->setHeight(ws.value(QStringLiteral("height")).toInt());
            }
            if (ws.contains(QStringLiteral("x")))
                win->setX(ws.value(QStringLiteral("x")).toInt());
            if (ws.contains(QStringLiteral("y")))
                win->setY(ws.value(QStringLiteral("y")).toInt());
            if (ws.contains(QStringLiteral("visibility"))) {
                const int vis = ws.value(QStringLiteral("visibility")).toInt();
                // Only honor sensible visibilities. Restoring "Minimized"
                // or "Hidden" would mean the user can't see the app at
                // launch — clamp to Windowed in that case.
                const auto v = static_cast<QWindow::Visibility>(vis);
                if (v == QWindow::Maximized
                    || v == QWindow::FullScreen
                    || v == QWindow::Windowed) {
                    win->setVisibility(v);
                }
            }
            ws.endGroup();

            auto *saveTimer = new QTimer(win);
            saveTimer->setSingleShot(true);
            saveTimer->setInterval(250);
            QObject::connect(saveTimer, &QTimer::timeout, win, [win]() {
                QSettings ws;
                ws.beginGroup(QStringLiteral("window"));
                const auto vis = win->visibility();
                // Skip Hidden/Minimized so a user who quits while
                // minimized doesn't reopen to a hidden window.
                if (vis == QWindow::Windowed
                    || vis == QWindow::Maximized
                    || vis == QWindow::FullScreen) {
                    ws.setValue(QStringLiteral("visibility"), int(vis));
                }
                // Only persist geometry while in the normal Windowed
                // state — otherwise we'd overwrite the un-maximize
                // fallback with the screen-sized geometry.
                if (vis == QWindow::Windowed) {
                    ws.setValue(QStringLiteral("width"),  win->width());
                    ws.setValue(QStringLiteral("height"), win->height());
                    ws.setValue(QStringLiteral("x"),      win->x());
                    ws.setValue(QStringLiteral("y"),      win->y());
                }
                ws.endGroup();
            });
            auto kick = [saveTimer]() { saveTimer->start(); };
            QObject::connect(win, &QWindow::widthChanged,      win, kick);
            QObject::connect(win, &QWindow::heightChanged,     win, kick);
            QObject::connect(win, &QWindow::xChanged,          win, kick);
            QObject::connect(win, &QWindow::yChanged,          win, kick);
            QObject::connect(win, &QWindow::visibilityChanged, win, kick);
        }
    }

    return app.exec();
}
