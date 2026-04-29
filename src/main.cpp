#include "ChatService.h"
#include "Library.h"
#include "MarkdownRenderer.h"
#include "PaperController.h"
#include "Settings.h"
#include "SummaryService.h"
#include "TocService.h"
#include "TranslationService.h"
#include "VisionService.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTranslator>
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

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader");
    app.setOrganizationDomain("ai-reader.local");
    app.setApplicationName("AI Reader");
    app.setWindowIcon(loadAppIcon());

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

    Settings settings;

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
        if (qtTranslator.load(loc, QStringLiteral("qtbase"),
                              QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QCoreApplication::installTranslator(&qtTranslator);
    };
    applyLanguage(settings.uiLanguage());

    PaperController paperController;
    TranslationService translation(&settings, &paperController);
    SummaryService summary(&settings, &paperController);
    TocService toc(&settings, &paperController);
    VisionService vision(&settings, &paperController);
    ChatService chat(&settings, &paperController, &toc);
    MarkdownRenderer markdown;
    Library library;

    QObject::connect(&paperController, &PaperController::pdfSourceChanged,
                     &summary, [&]() { summary.setPaperTitle(paperController.fileName()); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("paperController", &paperController);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("translation", &translation);
    engine.rootContext()->setContextProperty("summary", &summary);
    engine.rootContext()->setContextProperty("toc", &toc);
    engine.rootContext()->setContextProperty("vision", &vision);
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("markdown", &markdown);
    engine.rootContext()->setContextProperty("library", &library);

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

    engine.loadFromModule("AiReader", "Main");
    return app.exec();
}
