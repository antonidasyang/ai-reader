// Times a paper switch in the real reading pane.
//
// QtPdf serialises every PDFium call behind one global lock, and the PDF
// viewer renders its pages on Qt's image thread under that lock, a whole page
// at a time. A switch opens the new file twice on the GUI thread -- once for
// the viewer, once for PaperController -- and whichever of those runs after
// the viewer's page images have queued their renders waits behind them. On a
// paper with heavy figures that was 15-18 s of frozen window (v1.3.38 logs).
//
// This drives the shipping PaperController, PdfSelectionModel and AiPdfView,
// wired the way main.cpp and Main.qml wire them, through switches between two
// PDFs whose every page is a dense translucent scatter plot, and times each
// openPdf() -- the GUI thread is blocked for exactly that long.

#include "CursorUtil.h"
#include "PaperController.h"
#include "PdfSelectionModel.h"
#include "Settings.h"
#include "Stall.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtQml>

#include <algorithm>
#include <cstdio>

// Runs the event loop for `ms` and returns its longest single turn: what
// the window was frozen for at worst while the switch settled.
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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ai-reader-harness");
    app.setOrganizationDomain("harness.local");
    app.setApplicationName("SwitchHarness");
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    { QSettings stale; stale.clear(); stale.sync(); }

    qmlRegisterUncreatableType<PaperController>(
        "AiReader", 1, 0, "PaperController", QStringLiteral("context property"));

    const QString pdfA = QString::fromLocal8Bit(qgetenv("PDF_A"));
    const QString pdfB = QString::fromLocal8Bit(qgetenv("PDF_B"));
    const int budgetMs = qEnvironmentVariableIntValue("SWITCH_BUDGET_MS") > 0
        ? qEnvironmentVariableIntValue("SWITCH_BUDGET_MS") : 400;

    Settings settings;
    PaperController paper;
    PdfSelectionModel pdfSelection(paper.document());
    CursorUtil cursorUtil;
    // main.cpp's wiring: the selection builder follows the open file.
    auto syncSelectionSource = [&] {
        const QUrl u = paper.pdfSource();
        pdfSelection.setSource(u.isLocalFile() ? u.toLocalFile() : QString(),
                               paper.pdfPassword());
    };
    QObject::connect(&paper, &PaperController::pdfSourceChanged,
                     &pdfSelection, syncSelectionSource);
    QObject::connect(&paper, &PaperController::pdfPasswordChanged,
                     &pdfSelection, syncSelectionSource);

    QQuickView view;
    auto *ctx = view.engine()->rootContext();
    ctx->setContextProperty("paperController", &paper);
    ctx->setContextProperty("pdfSelection", &pdfSelection);
    ctx->setContextProperty("cursorUtil", &cursorUtil);
    ctx->setContextProperty("settings", &settings);
    view.engine()->addImportPath(QStringLiteral(":/"));

    QQmlComponent comp(view.engine());
    comp.setData(R"QML(
import QtQuick
import QtQuick.Pdf
import AiReader
Item {
    width: 1400; height: 1000
    property alias pdf: pdfView
    // As in Main.qml: the viewer's document follows PaperController.
    PdfDocument {
        id: doc
        source: paperController.pdfSource
        password: paperController.pdfPassword
    }
    AiPdfView {
        id: pdfView
        anchors.fill: parent
        document: doc
        selectionModel: pdfSelection
    }
}
)QML",
                 QUrl::fromLocalFile(QDir::currentPath()
                                     + QStringLiteral("/harness-switch.qml")));
    auto *rootItem = qobject_cast<QQuickItem *>(comp.create(ctx));
    if (!rootItem) {
        qCritical().noquote() << "switch harness failed:" << comp.errorString();
        return 2;
    }
    view.setContent(QUrl(), &comp, rootItem);
    view.resize(1400, 1000);
    view.show();

    paper.openPdf(QUrl::fromLocalFile(pdfA));
    pump(4000);   // let the first paper's visible pages finish rendering
    if (paper.status() != PaperController::Ready || paper.pageCount() == 0) {
        qCritical() << "fixture did not open";
        return 2;
    }

    const int dwellMs = qEnvironmentVariableIntValue("SWITCH_DWELL_MS") > 0
        ? qEnvironmentVariableIntValue("SWITCH_DWELL_MS") : 4000;
    QList<qint64> switches, afters;
    for (int i = 0; i < 6; ++i) {
        const QString next = (i % 2 == 0) ? pdfB : pdfA;
        QElapsedTimer t;
        Stall::resetBreakdown();
        t.start();
        paper.openPdf(QUrl::fromLocalFile(next));
        const qint64 ms = t.elapsed();
        // Where it went, in the same words the app's launch.log uses.
        const auto parts = Stall::takeBreakdown();
        // The reader looks at the page before moving on; meanwhile the
        // viewer lays the new paper out and its pages render.
        const qint64 after = pump(dwellMs);
        switches.append(ms);
        afters.append(after);
        std::printf("switch %d -> %s: GUI thread blocked %lld ms, then at most "
                    "%lld ms at a time while it settled\n", i + 1,
                    qPrintable(QFileInfo(next).fileName()), ms, after);
        for (const auto &part : parts)
            std::printf("      %5lld ms  %s\n", part.second, part.first.constData());
        std::fflush(stdout);
    }
    auto report = [budgetMs](const char *what, QList<qint64> v) {
        std::sort(v.begin(), v.end());
        const bool ok = v.last() <= budgetMs;
        std::printf("%s  %s for at most %d ms   - median %lld ms, worst %lld ms\n",
                    ok ? "PASS" : "FAIL", what, budgetMs, v.at(v.size() / 2),
                    v.last());
        return ok;
    };
    int passed = 0, failed = 0;
    (report("the switch itself blocks the GUI thread", switches) ? passed : failed)++;
    (report("no event-loop turn after it blocks", afters) ? passed : failed)++;
    std::printf("\n%d passed, %d failed\n", passed, failed);
    const bool ok = failed == 0;
    return ok ? 0 : 1;
}
