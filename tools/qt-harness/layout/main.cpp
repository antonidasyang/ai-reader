// Saved pane layouts: what a name means, what a width means, and what
// travels with the account.
//
// The promises under test are the ones a reader would notice breaking. A
// layout saved comes back the same layout. A layout saved on the desktop is
// still usable on the laptop -- which is the whole reason a width is a
// FRACTION of the window here and never a pixel count: 1600 px of PDF pane
// carried onto a 1366-wide screen is a reader with no reading pane left.
// Saving over a name replaces that layout instead of quietly growing a
// second one beside it. A layout that names a pane this build has never
// heard of is stepped over rather than choked on, and one that says nothing
// about a pane leaves that pane alone rather than hiding it. A damaged
// layouts/presets value costs the menu its contents and nothing else. And
// the whole document rides the account payload, out and back, without
// anything on the way being able to wipe what is on this machine.
//
// ISOLATION. Nothing here installs the shipping identity, and nothing here
// goes near the platform's native settings backend. The names are throwaway
// ones no real application answers to, the standard directories are
// redirected by Qt's test mode, and the settings file is pinned to a scratch
// directory this test creates -- which matters most on macOS, where the
// native backend is cfprefsd and setTestModeEnabled does NOT redirect it.

#include "LayoutPresets.h"
#include "Settings.h"
#include "StorageIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <cmath>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

// ── Fixtures ────────────────────────────────────────────────────────
// A snapshot shaped exactly like the one Main.qml hands save(): widths as
// a share of the row, never pixels.

static QVariantMap pane(bool visible, double px, double total)
{
    QVariantMap m;
    m.insert(QStringLiteral("visible"), visible);
    if (px > 0 && total > 0)
        m.insert(QStringLiteral("width"), px / total);
    return m;
}

static QVariantMap snapshot(const QStringList &order, const QVariantMap &panes)
{
    QVariantMap m;
    m.insert(QStringLiteral("order"), QVariant(order));
    m.insert(QStringLiteral("panes"), panes);
    return m;
}

// What the reader's desktop looks like: a 3840-wide window with the folder
// browser, the PDF and the paragraphs open, the chat put away, and 40 px
// gone to the splitter handles.
static QVariantMap desktopSnapshot()
{
    const double w = 3840;
    QVariantMap panes;
    panes.insert(QStringLiteral("folder"), pane(true, 240, w));
    panes.insert(QStringLiteral("pdf"), pane(true, 1600, w));
    panes.insert(QStringLiteral("blocks"), pane(true, 1960, w));
    panes.insert(QStringLiteral("chat"), pane(false, 400, w));
    return snapshot(QStringList{QStringLiteral("folder"), QStringLiteral("pdf"),
                                QStringLiteral("blocks"), QStringLiteral("chat")},
                    panes);
}

// Five panes open at once, which is more than a small screen can hold.
static QVariantMap crowdedSnapshot()
{
    const double w = 2000;
    QVariantMap panes;
    panes.insert(QStringLiteral("folder"), pane(true, 200, w));
    panes.insert(QStringLiteral("library"), pane(true, 200, w));
    panes.insert(QStringLiteral("pdf"), pane(true, 600, w));
    panes.insert(QStringLiteral("blocks"), pane(true, 600, w));
    panes.insert(QStringLiteral("chat"), pane(true, 400, w));
    return snapshot(QStringList{QStringLiteral("folder"), QStringLiteral("library"),
                                QStringLiteral("pdf"), QStringLiteral("blocks"),
                                QStringLiteral("chat")},
                    panes);
}

// The pane minimums Main.qml reads off the SplitView.
static QVariantMap minimums()
{
    QVariantMap m;
    m.insert(QStringLiteral("folder"), 0);
    m.insert(QStringLiteral("library"), 0);
    m.insert(QStringLiteral("toc"), 0);
    m.insert(QStringLiteral("pdf"), 280);
    m.insert(QStringLiteral("blocks"), 240);
    m.insert(QStringLiteral("analysis"), 260);
    m.insert(QStringLiteral("research"), 300);
    m.insert(QStringLiteral("tasks"), 260);
    m.insert(QStringLiteral("chat"), 240);
    return m;
}

static QVariantMap panesOf(const QVariantMap &resolved)
{
    return resolved.value(QStringLiteral("panes")).toMap();
}

static bool isVisible(const QVariantMap &resolved, const QString &id)
{
    return panesOf(resolved).value(id).toMap()
        .value(QStringLiteral("visible")).toBool();
}

static double widthOf(const QVariantMap &resolved, const QString &id)
{
    return panesOf(resolved).value(id).toMap()
        .value(QStringLiteral("width"), -1).toDouble();
}

static double visibleTotal(const QVariantMap &resolved)
{
    double sum = 0;
    const QVariantMap panes = panesOf(resolved);
    for (auto it = panes.begin(); it != panes.end(); ++it) {
        const QVariantMap e = it.value().toMap();
        if (e.value(QStringLiteral("visible")).toBool())
            sum += e.value(QStringLiteral("width"), 0).toDouble();
    }
    return sum;
}

static QString storedDocument()
{
    return QSettings().value(LayoutPresets::settingsKey()).toString();
}

static void writeDocument(const QString &text)
{
    QSettings s;
    s.setValue(LayoutPresets::settingsKey(), text);
    s.sync();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    // Wiped at the START of a run, never at the end, so a failure can be
    // picked over afterwards.
    const QString root =
        QDir::tempPath() + QStringLiteral("/ai-reader-layout-harness");
    QDir(root).removeRecursively();

    // Names no real application uses: even a mistake in the code under test
    // writes somewhere nobody cares about. Never "D2S"/"AIReader".
    const StorageIdentity::Identity id{
        QStringLiteral("ai-reader-layout-test"),
        QStringLiteral("ai-reader-layout-test.invalid"),
        QStringLiteral("LayoutHarness")};
    StorageIdentity::apply(id, root);
    {
        QSettings stale;
        stale.clear();
        stale.sync();
    }
    check("the harness writes a settings file of its own, not a real one",
          QSettings().fileName().startsWith(root)
              && QSettings().format() != QSettings::NativeFormat,
          QSettings().fileName());

    // ── A layout comes back the way it went in ──────────────────────
    {
        LayoutPresets layouts;
        check("a fresh install has no saved layouts",
              layouts.names().isEmpty() && layouts.current().isEmpty());

        layouts.save(QStringLiteral("Reading"), desktopSnapshot());
        check("a saved layout is offered by name",
              layouts.names() == QStringList{QStringLiteral("Reading")},
              layouts.names().join(QStringLiteral(", ")));
        check("...and saving it is what makes it the current one",
              layouts.current() == QStringLiteral("Reading"));

        const QVariantMap back = layouts.load(QStringLiteral("Reading"));
        const QVariantList order = back.value(QStringLiteral("order")).toList();
        check("...with the panes in the order they were saved in",
              order.size() == 4 && order.at(0).toString() == QStringLiteral("folder")
                  && order.at(3).toString() == QStringLiteral("chat"),
              QStringLiteral("%1 entries").arg(order.size()));

        const QVariantMap panes = back.value(QStringLiteral("panes")).toMap();
        const double folder =
            panes.value(QStringLiteral("folder")).toMap()
                .value(QStringLiteral("width")).toDouble();
        check("...and each width a share of the window, to four places",
              std::fabs(folder - 240.0 / 3840.0) < 0.0002,
              QString::number(folder));
        check("...including the pane that was put away",
              !panes.value(QStringLiteral("chat")).toMap()
                   .value(QStringLiteral("visible")).toBool()
                  && panes.value(QStringLiteral("blocks")).toMap()
                         .value(QStringLiteral("visible")).toBool());

        // The point of the whole exercise: nothing in the document is a
        // pixel count, so nothing in it is about the screen it was saved on.
        bool anyPixels = false;
        for (auto it = panes.begin(); it != panes.end(); ++it) {
            const double w = it.value().toMap()
                                 .value(QStringLiteral("width"), 0).toDouble();
            if (w > 1.0)
                anyPixels = true;
        }
        check("no width in the document is a pixel count", !anyPixels,
              storedDocument());

        check("asking for a layout nobody saved gives nothing, not a wrong one",
              layouts.load(QStringLiteral("Nope")).isEmpty()
                  && !layouts.contains(QStringLiteral("Nope")));
    }

    // ── The same layout on a smaller screen ─────────────────────────
    {
        LayoutPresets layouts;   // reads what the block above wrote
        const QVariantMap laptop =
            layouts.resolve(QStringLiteral("Reading"), 1366, minimums());
        check("a layout saved at 3840 resolves on a 1366-wide window",
              laptop.value(QStringLiteral("found")).toBool());

        // Proportional: each pane keeps the share of the row it had.
        const double f = widthOf(laptop, QStringLiteral("folder"));
        const double p = widthOf(laptop, QStringLiteral("pdf"));
        const double b = widthOf(laptop, QStringLiteral("blocks"));
        check("...with the folder pane still a sixteenth of it",
              std::fabs(f - 1366.0 * 240.0 / 3840.0) < 1.5,
              QStringLiteral("%1 px, wanted %2")
                  .arg(f).arg(1366.0 * 240.0 / 3840.0));
        check("...the PDF still a little under half",
              std::fabs(p - 1366.0 * 1600.0 / 3840.0) < 1.5,
              QStringLiteral("%1 px, wanted %2")
                  .arg(p).arg(1366.0 * 1600.0 / 3840.0));
        check("...and the paragraphs still the widest of the three",
              std::fabs(b - 1366.0 * 1960.0 / 3840.0) < 1.5 && b > p,
              QStringLiteral("%1 px, wanted %2")
                  .arg(b).arg(1366.0 * 1960.0 / 3840.0));
        check("...the three of them fitting the window they are for",
              visibleTotal(laptop) <= 1366.0,
              QStringLiteral("%1 px of 1366").arg(visibleTotal(laptop)));
        check("...none of them below the width it says it needs",
              p >= 280 && b >= 240);
        check("a pane that was put away is still put away, and takes no column",
              !isVisible(laptop, QStringLiteral("chat"))
                  && visibleTotal(laptop) < 1366.0);
        check("...but it is given a width, so showing it later is not a sliver",
              widthOf(laptop, QStringLiteral("chat")) >= 240,
              QString::number(widthOf(laptop, QStringLiteral("chat"))));

        // Everything the layout is silent about is missing from the answer,
        // which is how QML knows to leave those panes exactly as they are.
        const QVariantMap panes = panesOf(laptop);
        check("a pane the layout never mentions is absent, not hidden",
              !panes.contains(QStringLiteral("toc"))
                  && !panes.contains(QStringLiteral("analysis"))
                  && panes.size() == 4,
              QStringLiteral("%1 panes named").arg(panes.size()));

        const QVariantMap same =
            layouts.resolve(QStringLiteral("Reading"), 3840, minimums());
        check("resolved on the window it was saved on, it is itself again",
              std::fabs(widthOf(same, QStringLiteral("folder")) - 240) < 1
                  && std::fabs(widthOf(same, QStringLiteral("pdf")) - 1600) < 1,
              QStringLiteral("%1 / %2")
                  .arg(widthOf(same, QStringLiteral("folder")))
                  .arg(widthOf(same, QStringLiteral("pdf"))));
        check("a layout nobody saved resolves to nothing at all",
              !layouts.resolve(QStringLiteral("Nope"), 1366, minimums())
                   .value(QStringLiteral("found")).toBool());
    }

    // ── A layout that cannot fit ────────────────────────────────────
    {
        LayoutPresets layouts;
        layouts.save(QStringLiteral("Everything"), crowdedSnapshot());

        const QVariantMap small =
            layouts.resolve(QStringLiteral("Everything"), 800, minimums());
        check("five panes on an 800-wide window still fit inside it",
              visibleTotal(small) <= 800.0,
              QStringLiteral("%1 px of 800").arg(visibleTotal(small)));
        check("...with every one of them at or above its minimum",
              widthOf(small, QStringLiteral("pdf")) >= 280
                  && widthOf(small, QStringLiteral("blocks")) >= 240
                  && widthOf(small, QStringLiteral("chat")) >= 240,
              QStringLiteral("pdf %1, blocks %2, chat %3")
                  .arg(widthOf(small, QStringLiteral("pdf")))
                  .arg(widthOf(small, QStringLiteral("blocks")))
                  .arg(widthOf(small, QStringLiteral("chat"))));
        check("...and the panes with room to give are the ones that gave it",
              widthOf(small, QStringLiteral("folder")) < 200
                  && widthOf(small, QStringLiteral("folder")) > 0,
              QString::number(widthOf(small, QStringLiteral("folder"))));

        // Narrower than the minimums add up to. There is no arrangement that
        // fits, so every pane gets exactly what it asked for and the
        // SplitView is left to do what it does -- which is at least an
        // answer the reader can drag their way out of.
        const QVariantMap tiny =
            layouts.resolve(QStringLiteral("Everything"), 600, minimums());
        check("a window too narrow for the layout gives every pane its minimum",
              qRound(widthOf(tiny, QStringLiteral("pdf"))) == 280
                  && qRound(widthOf(tiny, QStringLiteral("blocks"))) == 240
                  && qRound(widthOf(tiny, QStringLiteral("chat"))) == 240,
              QStringLiteral("pdf %1, blocks %2, chat %3")
                  .arg(widthOf(tiny, QStringLiteral("pdf")))
                  .arg(widthOf(tiny, QStringLiteral("blocks")))
                  .arg(widthOf(tiny, QStringLiteral("chat"))));

        layouts.remove(QStringLiteral("Everything"));
    }

    // ── Names ───────────────────────────────────────────────────────
    {
        LayoutPresets layouts;
        const int before = layouts.names().size();

        // Saving over a name replaces that layout. The reader asked for one
        // layout called "Reading", not two.
        QVariantMap changed = desktopSnapshot();
        QVariantMap panes = changed.value(QStringLiteral("panes")).toMap();
        panes.insert(QStringLiteral("chat"), pane(true, 400, 3840));
        changed.insert(QStringLiteral("panes"), panes);
        layouts.save(QStringLiteral("Reading"), changed);
        check("saving over a name replaces that layout rather than adding one",
              layouts.names().size() == before,
              QStringLiteral("%1 layouts").arg(layouts.names().size()));
        check("...and it is the new arrangement that is stored",
              layouts.load(QStringLiteral("Reading"))
                  .value(QStringLiteral("panes")).toMap()
                  .value(QStringLiteral("chat")).toMap()
                  .value(QStringLiteral("visible")).toBool());

        // Case is not what tells two layouts apart: nobody keeps "Reading"
        // and "reading" on the same menu on purpose.
        layouts.save(QStringLiteral("  reading  "), desktopSnapshot());
        check("a name that differs only in case is the same name",
              layouts.names().size() == before,
              layouts.names().join(QStringLiteral(", ")));
        check("...and the spelling just typed is the one kept",
              layouts.names().contains(QStringLiteral("reading")),
              layouts.names().join(QStringLiteral(", ")));
        check("...with the surrounding spaces trimmed off it",
              layouts.existingName(QStringLiteral("READING"))
                  == QStringLiteral("reading"),
              layouts.existingName(QStringLiteral("READING")));

        layouts.save(QStringLiteral("   "), desktopSnapshot());
        check("a name that is nothing but spaces saves nothing",
              layouts.names().size() == before,
              layouts.names().join(QStringLiteral(", ")));

        // Renaming.
        layouts.save(QStringLiteral("Writing"), crowdedSnapshot());
        check("renaming a layout gives it the new name and keeps the old one's "
              "arrangement",
              layouts.rename(QStringLiteral("Writing"), QStringLiteral("Notes"))
                  && layouts.contains(QStringLiteral("Notes"))
                  && !layouts.contains(QStringLiteral("Writing"))
                  && layouts.load(QStringLiteral("Notes"))
                             .value(QStringLiteral("panes")).toMap().size() == 5,
              layouts.names().join(QStringLiteral(", ")));
        check("renaming onto a name another layout already has is refused",
              !layouts.rename(QStringLiteral("Notes"), QStringLiteral("reading"))
                  && layouts.contains(QStringLiteral("Notes"))
                  && layouts.contains(QStringLiteral("reading")));
        check("...and so is renaming to nothing, or renaming what is not there",
              !layouts.rename(QStringLiteral("Notes"), QStringLiteral("  "))
                  && !layouts.rename(QStringLiteral("Ghost"),
                                     QStringLiteral("Notes")));
        check("re-spelling a layout's own name is not a collision",
              layouts.rename(QStringLiteral("Notes"), QStringLiteral("NOTES"))
                  && layouts.names().contains(QStringLiteral("NOTES")));

        check("the menu lists them in an order that does not depend on "
              "who saved what first",
              layouts.names() == QStringList{QStringLiteral("NOTES"),
                                             QStringLiteral("reading")},
              layouts.names().join(QStringLiteral(", ")));

        // Deleting.
        layouts.setCurrent(QStringLiteral("NOTES"));
        check("deleting a layout takes it off the menu",
              layouts.remove(QStringLiteral("NOTES"))
                  && !layouts.contains(QStringLiteral("NOTES")));
        check("...and deleting the one on screen leaves no current layout",
              layouts.current().isEmpty());
        check("deleting one that is not there changes nothing",
              !layouts.remove(QStringLiteral("Ghost"))
                  && layouts.names() == QStringList{QStringLiteral("reading")});
    }

    // ── A layout from another build ─────────────────────────────────
    {
        // A pane this build has never heard of, in a document that is
        // otherwise perfectly good. Ignoring it must cost the rest nothing
        // -- and it must survive being read by this build, or upgrading,
        // running the old version once and upgrading again would empty the
        // reader's layouts one pane at a time.
        writeDocument(QStringLiteral(
            "{\"version\":1,\"presets\":[{\"name\":\"Future\","
            "\"order\":[\"folder\",\"notebook\",\"pdf\",\"blocks\"],"
            "\"panes\":{\"folder\":{\"visible\":true,\"width\":0.1},"
            "\"notebook\":{\"visible\":true,\"width\":0.2},"
            "\"pdf\":{\"visible\":true,\"width\":0.35},"
            "\"blocks\":{\"visible\":true,\"width\":0.35}}}]}"));

        LayoutPresets layouts;
        check("a layout naming a pane this build has never heard of still loads",
              layouts.names() == QStringList{QStringLiteral("Future")});

        const QVariantMap plan =
            layouts.resolve(QStringLiteral("Future"), 1400, minimums());
        check("...and the panes this build does have come back correctly",
              plan.value(QStringLiteral("found")).toBool()
                  && std::fabs(widthOf(plan, QStringLiteral("folder")) - 140) < 2
                  && widthOf(plan, QStringLiteral("pdf")) >= 280
                  && visibleTotal(plan) <= 1400,
              QStringLiteral("%1 px of 1400").arg(visibleTotal(plan)));
        check("...the unknown pane carried along rather than thrown away",
              panesOf(plan).contains(QStringLiteral("notebook"))
                  && layouts.load(QStringLiteral("Future"))
                         .value(QStringLiteral("order")).toList().size() == 4,
              storedDocument());
        layouts.save(QStringLiteral("Mine"), desktopSnapshot());
        check("...and it is still there after this build saves another layout",
              storedDocument().contains(QStringLiteral("notebook")),
              storedDocument());
    }

    // ── A damaged document ──────────────────────────────────────────
    {
        writeDocument(QStringLiteral("{\"version\":1,\"presets\":[{\"name\":\"Rea"));
        LayoutPresets layouts;
        check("a corrupt layouts value costs the menu its contents and nothing "
              "else",
              layouts.names().isEmpty() && layouts.current().isEmpty());
        layouts.save(QStringLiteral("Reading"), desktopSnapshot());
        check("...and saving a layout over it puts the app back in business",
              layouts.names() == QStringList{QStringLiteral("Reading")}
                  && LayoutPresets::isPresetDocument(storedDocument()));

        // Documents that are well-formed JSON but not ours.
        writeDocument(QStringLiteral("{\"presets\":\"a string, not a list\"}"));
        LayoutPresets foreign;
        check("a document of the right shape but the wrong kind is not read as "
              "layouts",
              foreign.names().isEmpty()
                  && !LayoutPresets::isPresetDocument(storedDocument()));
        check("...nor is an empty value, nor a list of things that are not "
              "layouts",
              !LayoutPresets::isPresetDocument(QString())
                  && !LayoutPresets::isPresetDocument(QStringLiteral("[]"))
                  && !LayoutPresets::isPresetDocument(
                      QStringLiteral("{\"presets\":[{\"nom\":\"x\"}]}")));
        check("...while an account that deleted its last layout still has "
              "something to say",
              LayoutPresets::isPresetDocument(
                  QStringLiteral("{\"version\":1,\"presets\":[]}")));
    }

    // ── Out to the account, and back ────────────────────────────────
    {
        writeDocument(QString());
        LayoutPresets layouts;
        Settings settings;
        // The two lines main() has to wire: a layout saved here reaches the
        // account sync, and a layout that arrives from the account reaches
        // the menu.
        QObject::connect(&layouts, &LayoutPresets::presetsChanged,
                         &settings, &Settings::layoutPresetsChanged);
        QObject::connect(&settings, &Settings::layoutPresetsChanged,
                         &layouts, &LayoutPresets::reload);

        check("the layouts document is one of the settings that follow the user",
              Settings::accountSettingKeys().contains(
                  LayoutPresets::settingsKey()),
              LayoutPresets::settingsKey());
        check("...and a fresh install would send an empty one",
              Settings::defaultAccountSettings()
                  .value(LayoutPresets::settingsKey()).toString().isEmpty());

        layouts.save(QStringLiteral("Reading"), desktopSnapshot());
        layouts.save(QStringLiteral("Writing"), crowdedSnapshot());
        const QJsonObject payload = settings.exportAccountSettings();
        const QString sent =
            payload.value(LayoutPresets::settingsKey()).toString();
        check("the payload carries the document this machine holds",
              !sent.isEmpty() && sent == storedDocument()
                  && sent.contains(QStringLiteral("Reading"))
                  && sent.contains(QStringLiteral("Writing")));
        check("...as one string, so the sync layer never has to understand it",
              payload.value(LayoutPresets::settingsKey()).isString());

        // Another machine, wiped, taking the payload.
        writeDocument(QString());
        layouts.reload();
        check("a machine with no layouts starts with none",
              layouts.names().isEmpty());
        settings.importAccountSettings(payload);
        check("...and the account's layouts arrive whole",
              storedDocument() == sent
                  && layouts.names() == QStringList{QStringLiteral("Reading"),
                                                    QStringLiteral("Writing")},
              layouts.names().join(QStringLiteral(", ")));
        check("...with their widths still shares of a window, not pixels",
              std::fabs(layouts.load(QStringLiteral("Reading"))
                            .value(QStringLiteral("panes")).toMap()
                            .value(QStringLiteral("folder")).toMap()
                            .value(QStringLiteral("width")).toDouble()
                        - 240.0 / 3840.0) < 0.0002);
        check("...and the round trip changed nothing about the document",
              settings.exportAccountSettings()
                  .value(LayoutPresets::settingsKey()).toString() == sent);

        // Rubbish from the account leaves what is here alone. This is the
        // one thing in the whole exchange that could cost a reader their
        // layouts, so it is the one thing that must not be clever.
        QJsonObject junk = payload;
        junk.insert(LayoutPresets::settingsKey(),
                    QStringLiteral("{\"presets\":\"not a list\"}"));
        settings.importAccountSettings(junk);
        check("a payload whose layouts are rubbish leaves the local ones alone",
              storedDocument() == sent && layouts.names().size() == 2);

        junk.insert(LayoutPresets::settingsKey(), QStringLiteral("<html>418</html>"));
        settings.importAccountSettings(junk);
        check("...and so does one that is not JSON at all",
              storedDocument() == sent && layouts.names().size() == 2);

        junk.insert(LayoutPresets::settingsKey(), QString());
        settings.importAccountSettings(junk);
        check("...and an account that has never saved a layout cannot wipe one "
              "that has",
              storedDocument() == sent && layouts.names().size() == 2);

        QJsonObject wrongType = payload;
        wrongType.insert(LayoutPresets::settingsKey(), 42);
        settings.importAccountSettings(wrongType);
        check("...nor can a value of the wrong type entirely",
              storedDocument() == sent && layouts.names().size() == 2);

        // A real deletion, on the other hand, has to travel.
        QJsonObject fewer = payload;
        LayoutPresets other;
        other.remove(QStringLiteral("Writing"));
        fewer.insert(LayoutPresets::settingsKey(), storedDocument());
        writeDocument(sent);
        layouts.reload();
        settings.importAccountSettings(fewer);
        check("a layout deleted on another machine is deleted here too",
              layouts.names() == QStringList{QStringLiteral("Reading")},
              layouts.names().join(QStringLiteral(", ")));

        // Two machines that hold the same layouts must produce the same
        // bytes, or every launch would spend a write arguing about them.
        writeDocument(QString());
        LayoutPresets a;
        a.save(QStringLiteral("Beta"), crowdedSnapshot());
        a.save(QStringLiteral("alpha"), desktopSnapshot());
        const QString oneOrder = storedDocument();
        writeDocument(QString());
        LayoutPresets b;
        b.save(QStringLiteral("alpha"), desktopSnapshot());
        b.save(QStringLiteral("Beta"), crowdedSnapshot());
        check("the same layouts saved in either order are the same payload",
              oneOrder == storedDocument() && !oneOrder.isEmpty());
        b.save(QStringLiteral("alpha"), desktopSnapshot());
        check("...and saving one again over itself is not a change to push",
              storedDocument() == oneOrder);
    }

    qInfo().noquote() << "";
    qInfo().noquote()
        << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
