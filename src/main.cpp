#include "PaperController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader");
    app.setOrganizationDomain("ai-reader.local");
    app.setApplicationName("AI Reader");

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    PaperController paperController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("paperController", &paperController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("AiReader", "Main");
    return app.exec();
}
