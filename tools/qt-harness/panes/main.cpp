// Measures what a splitter drag costs, in the panes that make it expensive.
//
// Dragging a handle resizes a pane on every mouse move; the panes that
// re-lay-out wrapped text or the PDF page table on each of those are what
// made a drag crawl -- and on a remote desktop, where every frame is an
// encoded bitmap, it is the difference between usable and not. Each such
// pane takes a `resizing` flag that holds its layout while the handle moves,
// and this driver sweeps a real pane's width to show what that is worth.

#include "ChatContent.h"
#include "ChatService.h"
#include "CursorUtil.h"
#include "LayoutSettings.h"
#include "MarkdownRenderer.h"
#include "PaperController.h"
#include "PdfSelectionModel.h"
#include "Settings.h"
#include "TocService.h"
#include "TranslationService.h"
#include "VisionService.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQmlPropertyMap>
#include <QQuickView>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtQml>

static void pump(int ms)
{
    QDeadlineTimer t(ms);
    while (!t.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Sweep the item's width the way a drag does, and report the wall time.
static double sweep(QQuickItem *item, int from, int to, int step)
{
    QElapsedTimer timer;
    timer.start();
    for (int w = from; w <= to; w += step) {
        item->setWidth(w);
        // One event-loop turn per step: a drag delivers one mouse move per
        // frame, and the layout it triggers runs in that turn.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return timer.nsecsElapsed() / 1e6;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("PaneHarness");
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

    const QString pdf = QString::fromLocal8Bit(qgetenv("PDF_A"));
    Settings settings;
    PaperController paper;
    paper.setAutoSegment(true);
    PdfSelectionModel pdfSelection(paper.document());
    CursorUtil cursorUtil;
    TranslationService translation(&settings, &paper);
    TocService toc(&settings, &paper);
    VisionService vision(&settings, &paper);
    ChatService chat(&settings, &paper, &toc);
    MarkdownRenderer markdown;
    ChatContent chatContent(&markdown);
    LayoutSettings layoutSettings;

    paper.openPdf(QUrl::fromLocalFile(pdf));
    pump(5000);

    QQuickView view;
    auto *ctx = view.engine()->rootContext();
    ctx->setContextProperty("paperController", &paper);
    ctx->setContextProperty("pdfSelection", &pdfSelection);
    ctx->setContextProperty("cursorUtil", &cursorUtil);
    ctx->setContextProperty("settings", &settings);
    ctx->setContextProperty("translation", &translation);
    ctx->setContextProperty("toc", &toc);
    ctx->setContextProperty("vision", &vision);
    ctx->setContextProperty("chat", &chat);
    ctx->setContextProperty("markdown", &markdown);
    ctx->setContextProperty("chatContent", &chatContent);
    ctx->setContextProperty("layoutSettings", &layoutSettings);
    // The paragraph pane names the member a segmentation came from; the
    // bridge that answers that needs the whole sync stack, and none of it
    // matters to a layout measurement.
    QQmlPropertyMap paperSyncStub;
    paperSyncStub.insert(QStringLiteral("blocksOriginLabel"), QString());
    paperSyncStub.insert(QStringLiteral("blocksOrigin"), QString());
    paperSyncStub.insert(QStringLiteral("notice"), QString());
    ctx->setContextProperty("paperSync", &paperSyncStub);
    view.engine()->addImportPath(QStringLiteral(":/"));

    QQmlComponent comp(view.engine());
    comp.setData(R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import AiReader
Item {
    width: 1000; height: 800
    property alias pdf: pdfView
    property alias blocks: blockList
    PdfDocument { id: doc; source: paperController.pdfSource }
    AiPdfView {
        id: pdfView
        height: parent.height
        width: 600
        document: doc
    }
    BlockList {
        id: blockList
        anchors.right: parent.right
        height: parent.height
        width: 400
        model: paperController.blocks
        paperStatus: paperController.status
    }
}
)QML",
                 QUrl::fromLocalFile(QDir::currentPath()
                                     + QStringLiteral("/harness-panes.qml")));
    auto *rootItem = qobject_cast<QQuickItem *>(comp.create(ctx));
    if (!rootItem) {
        qCritical().noquote() << "pane harness failed:" << comp.errorString();
        return 2;
    }
    view.setContent(QUrl(), &comp, rootItem);
    view.resize(1000, 800);
    view.show();
    pump(3000);

    auto *pdfView = rootItem->property("pdf").value<QQuickItem *>();
    auto *blocks = rootItem->property("blocks").value<QQuickItem *>();
    if (!pdfView || !blocks) {
        qCritical() << "panes not found";
        return 2;
    }
    qInfo().noquote() << "paragraphs:" << paper.blockCount()
                      << " pages:" << paper.pageCount();

    struct Case { QQuickItem *item; const char *name; };
    const Case cases[] = {{pdfView, "AiPdfView"}, {blocks, "BlockList"}};
    int failures = 0;
    for (const Case &c : cases) {
        // Warm up, so the first sweep does not pay for lazy delegate
        // creation that a real drag would already have behind it.
        c.item->setProperty("resizing", false);
        sweep(c.item, 420, 520, 5);
        pump(300);

        const double live = sweep(c.item, 520, 720, 2);
        pump(300);
        c.item->setProperty("resizing", true);
        const double held = sweep(c.item, 520, 720, 2);
        c.item->setProperty("resizing", false);
        pump(300);

        const double ratio = held > 0 ? live / held : 0;
        qInfo().noquote()
            << QStringLiteral("%1: %2 ms for 100 steps live, %3 ms held  "
                              "(%4x cheaper while dragging)")
                   .arg(QString::fromLatin1(c.name), -12)
                   .arg(live, 0, 'f', 1)
                   .arg(held, 0, 'f', 1)
                   .arg(ratio, 0, 'f', 1);
        if (held > live) {
            qInfo().noquote() << "  FAIL  holding the layout was not cheaper";
            ++failures;
        }
    }
    qInfo().noquote() << "";
    qInfo().noquote() << (failures == 0 ? "OK" : "FAILURES ABOVE");
    return failures == 0 ? 0 : 1;
}
