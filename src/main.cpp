#include "PaperController.h"
#include "Settings.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
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

    Settings settings;
    PaperController paperController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("paperController", &paperController);
    engine.rootContext()->setContextProperty("settings", &settings);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("AiReader", "Main");
    return app.exec();
}
