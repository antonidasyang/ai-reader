#include "ChatContent.h"
#include "ChatService.h"
#include "CursorUtil.h"
#include "CrashReporter.h"
#include "LayoutPresets.h"
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
#include "PaperSyncService.h"
#include "FileSyncService.h"
#include "ImportService.h"
#include "AnalysisListModel.h"
#include "AnalysisService.h"
#include "BatchAnalysisService.h"
#include "CompareService.h"
#include "AnalysisExporter.h"
#include "StorageIdentity.h"
#include "LibraryAnalysisService.h"
#include "TaskManager.h"
#include "UserPrefsSync.h"
#include "PaperSource.h"
#include "AnalysisStore.h"
#include "ProjectProfileController.h"
#include "MarkdownRenderer.h"
#include "PaperController.h"
#include "PdfSelectionModel.h"
#include "ProxyConfig.h"
#include "Settings.h"
#include "Stall.h"
#include "StructureService.h"
#include "Tabs.h"
#include "TocService.h"
#include "TranslationService.h"
#include "VisionService.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickItem>
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

// ── where the time goes at startup, and where it goes afterwards ─────
//
// "It hangs" is the report we cannot act on. Both halves of the answer are
// cheap enough to keep on permanently and both land in launch.log next to
// everything else, so a user who hits it once has already collected the
// evidence.
//
// bootMark() stamps each phase of startup against a clock started at the
// top of main(). The watchdog is a plain timer on the GUI thread: if it is
// late, the thread was busy, and the gap is how long the window was frozen
// for. It names the phase it was in, which is the whole point -- a stall
// during "restoring the session" and one during "syncing" are different
// bugs.
QElapsedTimer g_boot;

// Every event the GUI thread delivers goes through notify(). Timing the
// outermost one and naming its receiver catches work no hand-placed marker
// was ever going to find: a queued signal arrives as a MetaCall on the
// object that is about to run the slot, so a slow slot names its class
// whether or not anyone thought to mark it.
//
// The cost is two monotonic clock reads per top-level event, and a
// className() that is a pointer into static data rather than an
// allocation. Nothing is built unless the event was actually slow -- and
// the class name is captured *before* the call, because a receiver is
// allowed to delete itself during it.
constexpr int kSlowEventMs = 150;

// How many top-level events the GUI thread has delivered. The watchdog
// reads it: a freeze that spans zero events is a thread blocked *outside*
// event delivery -- a lock, a wait, a synchronous read -- and that is a
// different bug from a slow slot, so the log has to be able to tell them
// apart.
quint64 g_events = 0;

class TimedApplication : public QGuiApplication
{
public:
    using QGuiApplication::QGuiApplication;

    bool notify(QObject *receiver, QEvent *event) override
    {
        // notify() runs for every thread's event loop, and a worker taking
        // half a second is its job, not a freeze. Only this thread's events
        // can hold the window.
        if (m_depth > 0 || QThread::currentThread() != thread())
            return QGuiApplication::notify(receiver, event);

        // Pointers into static data, nothing built: this runs for every
        // event the thread delivers. The parent is worth the two extra
        // dereferences -- a bare QObject receiver says nothing on its own,
        // and "…to QObject (a child of QQmlEngine)" usually says
        // everything. They are read now rather than after, because a
        // receiver is allowed to delete itself during the call.
        const char *cls =
            receiver ? receiver->metaObject()->className() : "(none)";
        const QObject *parent = receiver ? receiver->parent() : nullptr;
        const char *parentCls =
            parent ? parent->metaObject()->className() : "nothing";
        const int type = int(event->type());

        ++m_depth;
        ++g_events;
        Stall::resetBreakdown();
        QElapsedTimer t;
        t.start();
        const bool handled = QGuiApplication::notify(receiver, event);
        const qint64 ms = t.elapsed();
        --m_depth;

        if (ms >= kSlowEventMs) {
            const char *evName =
                QMetaEnum::fromType<QEvent::Type>().valueToKey(type);
            // Not phase(): every Mark inside the event is gone by now and
            // that would always read "idle". This is where the time went,
            // biggest first, with whatever no marker covers as the
            // remainder -- which is the number that says to stop marking
            // and go look at Qt or at QML.
            QByteArray where;
            qint64 accounted = 0;
            const auto breakdown = Stall::takeBreakdown();
            for (int i = 0; i < breakdown.size(); ++i) {
                accounted += breakdown.at(i).second;
                if (i < 4 && breakdown.at(i).second >= 5)
                    where += "\n      " + QByteArray::number(breakdown.at(i).second)
                             + " ms  " + breakdown.at(i).first;
            }
            where += "\n      " + QByteArray::number(qMax(qint64(0), ms - accounted))
                     + " ms  (nothing marked -- QML, the scene graph, or Qt itself)";
            qWarning("[t+%lld ms] %lld ms went into one %s delivered to "
                     "%s (a child of %s):%s",
                     g_boot.elapsed(), ms, evName ? evName : "event", cls,
                     parentCls, where.constData());
        }
        return handled;
    }

private:
    int m_depth = 0;
};


void bootMark(const char *phase)
{
    qInfo("[boot +%5lld ms] %s", g_boot.elapsed(), phase);
    Stall::setPhase(phase);
}

// Below this a stall is a normal frame's worth of work; above it the user
// sees the window stop responding.
constexpr int kStallMs = 300;
// A stall this long is not a hiccup, it is the bug. When one happens, turn
// on Qt's own per-frame timing for a while so the *next* one says where the
// time went: `polish` is laying the scene out on this thread, and
// `blockedForSync` is waiting on the renderer. Nothing else separates
// "the layout is too expensive" from "the scene is too big to draw", and
// the two lead to opposite fixes.
//
// Off until then, because it is a line per frame. Freezes come in runs --
// the field logs show four in the first twelve seconds -- so arming on the
// first one still catches the rest.
constexpr int kFrameTimingTriggerMs = 1000;
constexpr int kFrameTimingWindowMs  = 15000;

// How many items the window is carrying. Laying out and syncing a scene is
// walked per item, so when the question is "why does a frame take seconds"
// the size of the thing being walked is the first number worth having.
int countItems(QQuickItem *item)
{
    if (!item)
        return 0;
    int n = 1;
    const auto kids = item->childItems();
    for (QQuickItem *c : kids)
        n += countItems(c);
    return n;
}

void installStallWatchdog(QQuickWindow *win, QObject *owner)
{
    auto *timer = new QTimer(owner);
    // Ten wake-ups a second: coarse enough to leave a laptop alone, fine
    // enough that anything worth calling a freeze spans several ticks.
    timer->setInterval(100);
    auto *last = new qint64(g_boot.elapsed());
    auto *lastEvents = new quint64(g_events);
    auto *armed = new bool(false);
    QObject::connect(timer, &QTimer::timeout, owner,
                     [last, lastEvents, armed, owner, win]() {
        const qint64 now = g_boot.elapsed();
        const qint64 gap = now - *last;
        *last = now;
        const quint64 events = g_events - *lastEvents;
        *lastEvents = g_events;
        if (gap < kStallMs)
            return;
        // One event means one slow slot, and the line above this one names
        // it. Thousands mean a storm of small work. None at all means the
        // thread was blocked outside event delivery entirely -- waiting on
        // a lock, a thread, or a file -- which is a different bug.
        qWarning("[t+%lld ms] the window was frozen for %lld ms (it started at "
                 "t+%lld ms, and delivered %llu events while it was) during: %s",
                 now, gap, now - gap, events, Stall::phase());
        if (*armed || gap < kFrameTimingTriggerMs)
            return;
        // An explicit QT_LOGGING_RULES belongs to whoever set it; do not
        // overwrite someone's deliberate configuration to chase this.
        if (qEnvironmentVariableIsSet("QT_LOGGING_RULES"))
            return;
        *armed = true;
        if (win)
            qWarning("[t+%lld ms] the window is %dx%d and is carrying %d items.",
                     now, int(win->width()), int(win->height()),
                     countItems(win->contentItem()));
        qWarning("[t+%lld ms] ...that was a long one. Per-frame timing is on "
                 "for the next %d s: `polish` is this thread laying the scene "
                 "out, `blockedForSync` is it waiting on the renderer.",
                 now, kFrameTimingWindowMs / 1000);
        QLoggingCategory::setFilterRules(
            QStringLiteral("qt.scenegraph.time.renderloop=true"));
        QTimer::singleShot(kFrameTimingWindowMs, owner, [] {
            QLoggingCategory::setFilterRules(
                QStringLiteral("qt.scenegraph.time.renderloop=false"));
            qWarning("[t+%lld ms] per-frame timing off again.",
                     g_boot.elapsed());
        });
    });
    timer->start();
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



// ── Remote sessions ────────────────────────────────────────────────
// Qt Quick composes the whole window on the GPU. Inside an RDP session
// there is no GPU: Direct3D falls back to WARP, every frame is rendered
// in software into a full-window surface, and RDP then has to encode and
// ship that whole surface -- which is the opposite of what the protocol
// is good at (small dirty rectangles). The Qt Quick *software* renderer
// repaints only the regions that changed, which maps onto RDP properly,
// and the basic render loop stops the app from chasing vsync it can
// never hit.
//
// Both are environment variables read by Qt during QGuiApplication
// construction, so this has to run before the application object exists
// -- which is also why the choice is read straight out of QSettings
// rather than through Settings.
bool g_remoteRendering = false;

bool detectRemoteSession()
{
#ifdef Q_OS_WIN
    return GetSystemMetrics(SM_REMOTESESSION) != 0;
#else
    // X11 forwarding and the like: no reliable signal, and the local case
    // must not pay for a guess.
    return false;
#endif
}

void applyRemoteRenderingHints()
{
    const QString mode =
        QSettings().value(QStringLiteral("ui/remoteMode"),
                          QStringLiteral("auto")).toString();
    const bool remote = mode == QLatin1String("on")
                        || (mode != QLatin1String("off") && detectRemoteSession());
    if (!remote)
        return;
    g_remoteRendering = true;
    // An explicit environment variable always wins: it is how someone
    // debugs this from a command line.
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "basic");
}

} // namespace

int main(int argc, char *argv[])
{
    // Before the application object: the org/app names make QSettings
    // resolvable, and the rendering hints below are environment variables
    // Qt reads while QGuiApplication is being constructed.
    // Names the storage after the brand and the product (D2S / AIReader),
    // moving what the old names left behind. Before anything reads a
    // setting or a cache -- which includes the rendering hints below.
    g_boot.start();
    StorageIdentity::apply();
    applyRemoteRenderingHints();

    TimedApplication app(argc, argv);
    app.setWindowIcon(loadAppIcon());

    // QStandardPaths needs the org/app names above to resolve the
    // per-user AppData directory, so install the logger now — not
    // before — and any qWarning from this point on lands in the file.
    installLaunchLogger();
    bootMark("logger installed");

    // Dump every storage location at startup so users (and us) can
    // tell at a glance where the app is reading + writing — useful
    // when "I deleted the cache and it's still there" turns out to
    // mean the data lives somewhere unexpected (registry, keychain,
    // a different AppData root in a different Qt version).
    if (g_remoteRendering) {
        qInfo().noquote() << "Remote session: using the software renderer and "
                             "the basic render loop (Settings -> Appearance to "
                             "change)";
    }
    // Before anything can make a request: Qt asks the system for a proxy on
    // every one of them, on the thread that owns the reply, and on Windows
    // that question can take seconds. Ask once now, off this thread.
    ProxyConfig::install(
        QUrl(QSettings()
                 .value(QStringLiteral("server/url"),
                        QStringLiteral("https://aireader.d2ssoft.com"))
                 .toString()));

    qInfo().noquote() << "Storage:";
    qInfo().noquote() << "  AppData       :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qInfo().noquote() << "  AppLocalData  :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    qInfo().noquote() << "  Cache         :"
                      << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    qInfo().noquote() << "  Config        :"
                      << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    qInfo().noquote() << "  Settings file :"
                      << StorageIdentity::settingsFilePath();
    // A settings file the app could not read is the difference between "it
    // forgot everything" and "it was damaged and here is the copy we kept",
    // and only the log can tell them apart afterwards.
    {
        QSettings probe;
        if (probe.status() != QSettings::NoError)
            qWarning().noquote()
                << "  Settings could not be read (status"
                << int(probe.status())
                << "); the app is starting from defaults. A copy of the "
                   "unreadable file is kept next to it with a .corrupt suffix.";
    }

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
    settings.setRemoteRenderingActive(g_remoteRendering);
    bootMark("settings loaded (includes the keychain read)");

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
    //
    // The app is written in English and ships one catalog, zh_CN, so there
    // are exactly two languages it can be in. Whatever we are asked for is
    // resolved to one of them first, and Qt's own catalog is then loaded in
    // that same language — handing QLocale::system() straight to both used
    // to mix them: a German or Japanese Windows gave English menus with
    // German or Japanese dialog buttons and context menus, because Qt has a
    // catalog for those locales and we don't. Chinese in any script or
    // region gets zh_CN (Simplified for a zh_TW reader beats English);
    // everything else gets untranslated English on both sides, which is why
    // "en" installs no translator at all.
    QTranslator appTranslator;
    QTranslator qtTranslator;
    auto applyLanguage = [&](const QString &code) {
        const QLocale asked = code.isEmpty() ? QLocale::system() : QLocale(code);
        const bool chinese = asked.language() == QLocale::Chinese;
        QCoreApplication::removeTranslator(&appTranslator);
        QCoreApplication::removeTranslator(&qtTranslator);
        // "The app came up in the wrong language" is a support question, and
        // the answer is this line: what was asked for, and what it became.
        qInfo().noquote()
            << QStringLiteral("UI language: %1 (setting: %2)")
                   .arg(chinese ? QStringLiteral("zh_CN") : QStringLiteral("en"),
                        code.isEmpty()
                            ? QStringLiteral("system=%1").arg(asked.name())
                            : code);
        if (!chinese)
            return;  // English is the source language; nothing to install.
        const QLocale loc(QStringLiteral("zh_CN"));
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

    bootMark("translators installed");

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
    // Its own mark: this is the one that touches the filesystem. Restoring
    // a folder that lives on a disconnected network share is a stall the
    // app cannot avoid, only name.
    bootMark("reading services constructed");
    Library library;
    bootMark("folder pane restored");
    LayoutSettings layoutSettings;
    // Named arrangements of the panes. The presets themselves ride the
    // account payload as one JSON string; which one this screen is showing
    // stays here, because that is a fact about this screen.
    LayoutPresets layoutPresets;
    QObject::connect(&layoutPresets, &LayoutPresets::presetsChanged,
                     &settings, &Settings::layoutPresetsChanged);
    QObject::connect(&settings, &Settings::layoutPresetsChanged,
                     &layoutPresets, &LayoutPresets::reload);
    Tabs tabs(&paperController);
    UpdateChecker updateChecker(&settings);
    LibraryDb libraryDb;
    ApiClient apiClient;
    AuthController auth(&apiClient);
    // The settings that belong to the person rather than the machine follow
    // the account: pulled when they sign in, pushed a few seconds after they
    // change. It wires itself to auth and to Settings' own change signals,
    // and stays silent when there is no account or no network -- the local
    // value is what the running app obeys either way.
    UserPrefsSync prefsSync(&apiClient, &auth, &settings);
    ProjectController projectController(&apiClient, &auth, &libraryDb);
    SyncEngine syncEngine(&apiClient, &auth, &projectController, &libraryDb);
    LibraryModel libraryModel(&libraryDb, &projectController, &syncEngine);
    MetadataService metadataService(&libraryModel, &paperController);
    SearchService searchService(&libraryDb, &projectController);
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
    bootMark("library, sync and file services constructed");
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
    CompareService compareService(&settings, &analysisStore, &projectController,
                                  &projectProfile);
    LibraryAnalysisService libraryAnalysis(&settings, &analysisStore,
                                           &projectController, &projectProfile);

    // One queue for everything that calls a model. Services submit to it
    // instead of starting their own work, which is what keeps two runs off
    // one paper and what makes "what is this app doing right now" a question
    // with an answer.
    TaskManager taskManager(&settings);
    // A paper's file on disk is a checksum when it came from a project, so
    // the viewer would otherwise list "Translate -- 9f3c1e…". The library
    // knows the real title; the project is whichever one is open.
    taskManager.setContext(
        [&projectController] { return projectController.currentId(); },
        [&libraryModel](const QString &paperId) -> QString {
            const QString itemId = libraryModel.findByPaperId(paperId);
            if (itemId.isEmpty())
                return {};
            return libraryModel.itemFields(itemId).value(QStringLiteral("title")).toString();
        });
    // Picking work back up needs the paper it was being done to. The manager
    // asks for it by id; the library knows which file that is.
    taskManager.setPaperOpener([&libraryModel, &fileSync](const QString &paperId) {
        const QString itemId = libraryModel.findByPaperId(paperId);
        if (itemId.isEmpty())
            return;
        const QVariantMap fields = libraryModel.itemFields(itemId);
        fileSync.openItem(itemId, fields.value(QStringLiteral("localPath")).toString());
    });
    // ...and once its paragraphs are on disk, the work that was waiting for
    // that paper can start.
    QObject::connect(&paperController, &PaperController::paperCacheReady,
                     &taskManager,
                     [&taskManager](const QString &) { taskManager.retryPending(); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &taskManager, [&] {
        // Whatever is still in flight when the window closes is written
        // down, so the next launch can offer it back rather than pretending
        // it never happened.
        taskManager.saveInterrupted();
    });
    AnalysisExporter analysisExporter(&analysisStore, &projectController,
                                      &projectProfile, &libraryAnalysis,
                                      &compareService);

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
                     [&tabs](const QString &) {
                         Stall::Mark mark("renaming the open tabs");
                         tabs.refreshTitles();
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

    bootMark("analysis and task services constructed");
    QQmlApplicationEngine engine;
    // Everything that calls a model goes through the one queue. Registered
    // here, after the manager exists and before the pending work from the
    // last session is offered back -- the resumers are registered inside
    // these calls.
    translation.setTasks(&taskManager);
    toc.setTasks(&taskManager);
    vision.setTasks(&taskManager);
    structure.setTasks(&taskManager);
    analysisService.setTasks(&taskManager);
    batchAnalysis.setTasks(&taskManager);
    libraryAnalysis.setTasks(&taskManager);

    engine.rootContext()->setContextProperty("paperController", &paperController);
    engine.rootContext()->setContextProperty("pdfSelection", &pdfSelection);
    engine.rootContext()->setContextProperty("structure", &structure);
    engine.rootContext()->setContextProperty("cursorUtil", &cursorUtil);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("translation", &translation);
    engine.rootContext()->setContextProperty("toc", &toc);
    engine.rootContext()->setContextProperty("vision", &vision);
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("markdown", &markdown);
    engine.rootContext()->setContextProperty("chatContent", &chatContent);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("layoutSettings", &layoutSettings);
    engine.rootContext()->setContextProperty("layouts", &layoutPresets);
    engine.rootContext()->setContextProperty("tabs", &tabs);
    engine.rootContext()->setContextProperty("updates", &updateChecker);
    engine.rootContext()->setContextProperty("libraryDb", &libraryDb);
    engine.rootContext()->setContextProperty("auth", &auth);
    engine.rootContext()->setContextProperty("projects", &projectController);
    engine.rootContext()->setContextProperty("sync", &syncEngine);
    engine.rootContext()->setContextProperty("libraryModel", &libraryModel);
    engine.rootContext()->setContextProperty("metadata", &metadataService);
    engine.rootContext()->setContextProperty("search", &searchService);
    engine.rootContext()->setContextProperty("paperSync", &paperSync);
    engine.rootContext()->setContextProperty("fileSync", &fileSync);
    engine.rootContext()->setContextProperty("importer", &importService);
    engine.rootContext()->setContextProperty("profile", &projectProfile);
    engine.rootContext()->setContextProperty("analysis", &analysisService);
    engine.rootContext()->setContextProperty("analysisList", &analysisList);
    engine.rootContext()->setContextProperty("batchAnalysis", &batchAnalysis);
    engine.rootContext()->setContextProperty("compare", &compareService);
    engine.rootContext()->setContextProperty("research", &libraryAnalysis);
    engine.rootContext()->setContextProperty("exporter", &analysisExporter);
    engine.rootContext()->setContextProperty("tasks", &taskManager);

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

    bootMark("QML engine configured");
    engine.loadFromModule("AiReader", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Failed to instantiate AiReader/Main — check the lines"
                       " above for QML errors.";
        return 1;
    }
    bootMark("QML scene built");
    // From here on the window exists, so a stall is a frozen window.
    installStallWatchdog(
        qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)), &app);

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
    Stall::setPhase("restoring the papers that were open");
    if (!tabs.restoreSession())
        paperController.restoreLast();
    bootMark("open papers restored");

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
        auto *win = qobject_cast<QQuickWindow *>(root);
        if (!win) {
            // Not the window we expected. It is still hidden, so show it
            // rather than leave the user staring at nothing.
            if (auto *anyWindow = qobject_cast<QWindow *>(root))
                anyWindow->setVisible(true);
        }
        if (win) {
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
            // Main.qml leaves the window hidden so everything above lands
            // before anyone sees it; this is where it is finally shown, and
            // it is shown straight into the state it was left in. Restoring
            // "Minimized" or "Hidden" would mean the user cannot see the
            // app at launch, so anything else becomes Windowed.
            QWindow::Visibility vis = QWindow::Windowed;
            if (ws.contains(QStringLiteral("visibility"))) {
                const auto v = static_cast<QWindow::Visibility>(
                    ws.value(QStringLiteral("visibility")).toInt());
                if (v == QWindow::Maximized || v == QWindow::FullScreen
                    || v == QWindow::Windowed)
                    vis = v;
            }
            ws.endGroup();
            win->setVisibility(vis);

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

    bootMark("window geometry restored -- handing over to the event loop");
    Stall::setPhase("idle");


    return app.exec();
}
