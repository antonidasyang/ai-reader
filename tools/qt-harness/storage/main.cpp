// The app's storage identity, and the one settings file it now writes.
//
// Every service in the app -- Settings, Library, Tabs, PaperController,
// ProjectController, AuthController, LayoutSettings, main()
// itself -- holds a default-constructed QSettings, and after
// StorageIdentity::apply() that is one plain JSON file rather than the
// Windows registry, a cfprefsd plist or a .conf. The point of the file is
// support: someone whose app has wedged can be told where it is, can send it
// to us, can edit it by hand, or can delete it and start again. Nobody can
// be talked through regedit.
//
// That promise rests on things that break quietly. Whether the path the app
// reports is the path QSettings actually opens. Whether a value lands in
// that file and nowhere else. Whether a value read back cold on the next
// launch is still the value that was written -- INI failed exactly there,
// handing back the string "true" for a bool and "8192" for an int, so the
// round trip is checked here type by type, from a copy of the file that
// nothing has parsed before. And whether a half-written or corrupt file
// costs a user their configuration.
//
// ISOLATION. Nothing below installs the shipping identity's storage, and
// nothing below goes near the platform's native settings backend. The names
// are throwaway ones no real application answers to, the standard
// directories are redirected by Qt's test mode, and the settings file is
// pinned to a scratch directory this test creates. That last part matters
// most on macOS, where the native backend is cfprefsd -- which
// QStandardPaths::setTestModeEnabled does NOT redirect, and which an earlier
// version of this harness once cleared for real, taking a developer's actual
// preferences with it.

#include "StorageIdentity.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

static QByteArray readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

static bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(bytes) == bytes.size();
}

static QStringList filesUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}

// A format of the harness's own, for the one thing that cannot be checked
// from outside: that a write which fails part way through does not damage
// the file that is already there. The write handler puts bytes on the device
// and then reports failure, which is the worst case -- if Qt were writing in
// place, those bytes would be the file.
static bool g_writeShouldFail = false;
static bool harnessRead(QIODevice &device, QSettings::SettingsMap &map)
{
    Q_UNUSED(device);
    Q_UNUSED(map);
    return true;
}
static bool harnessWrite(QIODevice &device, const QSettings::SettingsMap &map)
{
    Q_UNUSED(map);
    device.write(g_writeShouldFail ? QByteArray("HALF A DOCUMENT")
                                   : QByteArray("{\"written\":true}\n"));
    return !g_writeShouldFail;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    const QString root =
        QDir::tempPath() + QStringLiteral("/ai-reader-storage-harness");
    // Everything that must not sit inside the directory the checks below
    // insist holds exactly one file: the cold copy, the damaged files, the
    // atomic-write fixture.
    const QString side = root + QStringLiteral("-side");
    QDir(root).removeRecursively();
    QDir(side).removeRecursively();
    QDir().mkpath(side);

    // Names no real application uses, so that even a mistake in the code
    // under test writes somewhere nobody cares about. Never "D2S"/"AIReader".
    const StorageIdentity::Identity id{
        QStringLiteral("ai-reader-storage-test"),
        QStringLiteral("ai-reader-storage-test.invalid"),
        QStringLiteral("StorageHarness")};
    StorageIdentity::apply(id, root);

    // ── The identity ────────────────────────────────────────────────
    check("apply() installs the names it was handed",
          QCoreApplication::organizationName() == id.organization
              && QCoreApplication::organizationDomain() == id.domain
              && QCoreApplication::applicationName() == id.application);

    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    check("...so the standard directories are organization/application",
          data.section(QChar('/'), -2)
              == id.organization + QChar('/') + id.application,
          data);

    // What a real install's directories look like. Only the two names are
    // set here -- no store is opened and nothing is written -- and Qt's test
    // mode has these paths pointing inside ~/.qttest either way.
    const StorageIdentity::Identity ship = StorageIdentity::current();
    {
        QCoreApplication::setOrganizationName(ship.organization);
        QCoreApplication::setOrganizationDomain(ship.domain);
        QCoreApplication::setApplicationName(ship.application);
        const QString shipData =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString shipCache =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString tail = shipData.section(QChar('/'), -2);
        check("a real install's directories are the brand and the product",
              tail == QStringLiteral("D2S/AIReader")
                  && shipCache.section(QChar('/'), -2) == tail,
              shipData);
        check("...with no space in either name, and not the repository name",
              !tail.contains(QChar(' '))
                  && !tail.contains(QStringLiteral("ai-reader")),
              tail);
        StorageIdentity::apply(id, root);   // back to the throwaway install
    }

    // ── One file, where the app says it is ──────────────────────────
    const QString file = StorageIdentity::settingsFilePath();
    check("the app can say where its settings are",
          !file.isEmpty() && file.endsWith(QStringLiteral(".json"))
              && file.startsWith(root),
          file);
    {
        QSettings s;
        check("...and it is the file a default-constructed QSettings opens",
              s.fileName() == file
                  && s.format() == StorageIdentity::settingsFormat(),
              s.fileName());
    }
    check("...and the format was registered exactly once",
          StorageIdentity::settingsFormat() == StorageIdentity::settingsFormat()
              && StorageIdentity::settingsFormat() != QSettings::InvalidFormat
              && StorageIdentity::settingsFormat() != QSettings::NativeFormat,
          QStringLiteral("format id %1")
              .arg(int(StorageIdentity::settingsFormat())));
    {
        const QFileInfo fi(file);
        // One file per application inside one directory per organization --
        // which on macOS is named after the domain rather than the name,
        // because that is the name QSettings itself keys on there.
        const QString orgDir = fi.dir().dirName();
        check("...one file per application, in one directory per organization",
              fi.fileName() == id.application + QStringLiteral(".json")
                  && (orgDir == id.organization || orgDir == id.domain)
                  && fi.absolutePath() == root + QChar('/') + orgDir,
              orgDir + QChar('/') + fi.fileName());
    }
    {
        // The shipping install's file, named without going anywhere near it:
        // the root is still this test's scratch directory.
        const QFileInfo fi(StorageIdentity::settingsFilePath(ship));
        check("a real install's file is AIReader.json under the brand",
              fi.fileName() == QStringLiteral("AIReader.json")
                  && (fi.dir().dirName() == ship.organization
                      || fi.dir().dirName() == ship.domain),
              fi.dir().dirName() + QChar('/') + fi.fileName());
    }

    // ── Everything the app stores, written the way the app writes it ─
    const QString prompt = QStringLiteral(
        "Translate the paragraph.\n\n- keep \"quotes\" and \\backslashes\n"
        "- a=b is not a section header\n- 保留原文的段落结构");
    const QString longPrompt = QStringLiteral("override: ")
        + QStringLiteral("每一段都要逐句翻译，不要合并。").repeated(4000);
    const QString hexKey = QStringLiteral("reading/")
        + QStringLiteral("9f86d081884c7d659a2feaa0c55ad015")
        + QStringLiteral("a3bf4f1b2b0b822cd15d6c15b0f00a08");
    const QUrl lastUrl(QStringLiteral("file:///Users/x/papers/a%20paper.pdf"));
    {
        QSettings s;
        s.setValue(QStringLiteral("llm/model"), QStringLiteral("claude-opus-5"));
        s.setValue(QStringLiteral("grobid/enabled"), true);
        s.setValue(QStringLiteral("paper/autoSegment"), false);
        s.setValue(QStringLiteral("llm/maxTokens"), 8192);
        s.setValue(QStringLiteral("window/width"), 1400);
        s.setValue(QStringLiteral("llm/temperature"), 0.2);
        s.setValue(QStringLiteral("translation/model"), QStringLiteral("20240620"));
        s.setValue(QStringLiteral("llm/baseUrl"), QString());
        s.setValue(QStringLiteral("prompts/translation"), prompt);
        s.setValue(QStringLiteral("prompts/override"), longPrompt);
        s.setValue(QStringLiteral("tabs/urls"),
                   QStringList{QStringLiteral("file:///a.pdf"),
                               QStringLiteral("file:///b,1.pdf")});
        s.setValue(QStringLiteral("tabs/recent"),
                   QStringList{QStringLiteral("only-one")});
        s.setValue(QStringLiteral("paper/lastUrl"), lastUrl);
        s.setValue(QStringLiteral("layout/splitterState"),
                   QByteArray("0.30,0.45,0.25"));
        // Nesting: three levels, a 64-character hash as a segment, a dot in
        // a name, a space in a name -- and the pair QSettings allows but
        // JSON has no room for, a value and a branch at the same name.
        s.setValue(QStringLiteral("panes/folder/visible"), true);
        s.setValue(hexKey, 12);
        s.setValue(QStringLiteral("updates/manifest.url"),
                   QStringLiteral("https://aireader.d2ssoft.com/manifest.json"));
        s.setValue(QStringLiteral("panes/Paper Pane/width"), 320);
        s.setValue(QStringLiteral("panes/summary"), QStringLiteral("collapsed"));
        s.setValue(QStringLiteral("panes/summary/visible"), true);
        s.sync();
        check("writing all of it reports no error", s.status() == QSettings::NoError);
    }

    const QByteArray raw = readBytes(file);
    const QString text = QString::fromUtf8(raw);
    check("a value written through a default-constructed QSettings reaches it",
          text.contains(QStringLiteral("\"model\": \"claude-opus-5\"")),
          QFileInfo::exists(file) ? QStringLiteral("file exists")
                                  : QStringLiteral("no file at all"));
    check("...as JSON a person can read: indented, sorted, one trailing newline",
          text.startsWith(QStringLiteral("{\n"))
              && text.contains(QStringLiteral("\n    \"llm\": {"))
              && text.contains(QStringLiteral("\n            \"visible\": true"))
              && text.indexOf(QStringLiteral("\"grobid\""))
                     < text.indexOf(QStringLiteral("\"llm\""))
              && text.endsWith(QStringLiteral("}\n"))
              && !text.endsWith(QStringLiteral("}\n\n")));
    check("...and Chinese is left as Chinese, not \\u escapes",
          text.contains(QStringLiteral("保留原文的段落结构")));
    check("...with the keys nested, a level per '/'",
          text.contains(QStringLiteral("\"panes\": {"))
              && text.contains(QStringLiteral("\"folder\": {")));

    // Each of these stands in for a different service holding its own
    // QSettings. The promise is that there is one file, not one per service.
    {
        QSettings library;
        library.setValue(QStringLiteral("library/lastFolder"),
                         QStringLiteral("/papers"));
        QSettings tabs;
        tabs.setValue(QStringLiteral("tabs/active"), 2);
        QSettings auth;
        auth.setValue(QStringLiteral("server/url"),
                      QStringLiteral("https://aireader.d2ssoft.com"));
        library.sync();
        tabs.sync();
        auth.sync();
    }
    const QString shared = QString::fromUtf8(readBytes(file));
    check("every service in the app writes that same file",
          shared.contains(QStringLiteral("\"lastFolder\": \"/papers\""))
              && shared.contains(QStringLiteral("\"active\": 2"))
              && shared.contains(QStringLiteral("\"url\": \"https://aireader.d2ssoft.com\"")));
    check("...and nothing was written anywhere else",
          filesUnder(root) == QStringList{file},
          filesUnder(root).join(QStringLiteral(", ")));

    // ── The round trip, read cold ───────────────────────────────────
    // A fresh QSettings on the same path would answer out of Qt's parsed
    // cache and prove nothing. The next launch parses the file, so the file
    // is copied somewhere nothing has looked at and opened there. This is
    // the section INI could not pass.
    const QString coldFile = side + QStringLiteral("/cold.json");
    check("the file can be copied away and opened on its own",
          QFile::copy(file, coldFile));
    QSettings cold(coldFile, StorageIdentity::settingsFormat());
    check("...with no complaint about its contents",
          cold.status() == QSettings::NoError);

    check("a bool comes back a bool, on and off",
          cold.value(QStringLiteral("grobid/enabled")).typeId() == QMetaType::Bool
              && cold.value(QStringLiteral("grobid/enabled")).toBool()
              && cold.value(QStringLiteral("paper/autoSegment")).typeId()
                     == QMetaType::Bool
              && !cold.value(QStringLiteral("paper/autoSegment")).toBool(),
          cold.value(QStringLiteral("grobid/enabled")).typeName());
    check("an int comes back an int, not a double",
          cold.value(QStringLiteral("window/width")).typeId() == QMetaType::Int
              && cold.value(QStringLiteral("window/width")).toInt() == 1400
              && cold.value(QStringLiteral("llm/maxTokens")).toInt() == 8192,
          cold.value(QStringLiteral("window/width")).typeName());
    check("a fraction comes back a double, and keeps its value",
          cold.value(QStringLiteral("llm/temperature")).typeId()
                  == QMetaType::Double
              && qFuzzyCompare(
                  cold.value(QStringLiteral("llm/temperature")).toDouble(), 0.2));
    check("a string stays a string, even when it is all digits",
          cold.value(QStringLiteral("translation/model")).typeId()
                  == QMetaType::QString
              && cold.value(QStringLiteral("translation/model")).toString()
                     == QStringLiteral("20240620"));
    check("a prompt keeps its line breaks, quotes, backslashes and Chinese",
          cold.value(QStringLiteral("prompts/translation")).toString() == prompt);
    check("a very long prompt override survives whole",
          cold.value(QStringLiteral("prompts/override")).toString() == longPrompt,
          QStringLiteral("%1 characters")
              .arg(cold.value(QStringLiteral("prompts/override")).toString().size()));
    check("a value that was cleared is still there, and still empty",
          cold.contains(QStringLiteral("llm/baseUrl"))
              && cold.value(QStringLiteral("llm/baseUrl")).typeId()
                     == QMetaType::QString
              && cold.value(QStringLiteral("llm/baseUrl")).toString().isEmpty());
    check("a list comes back a list, commas and all",
          cold.value(QStringLiteral("tabs/urls")).typeId() == QMetaType::QStringList
              && cold.value(QStringLiteral("tabs/urls")).toStringList()
                     == QStringList{QStringLiteral("file:///a.pdf"),
                                    QStringLiteral("file:///b,1.pdf")});
    check("...and a list of one is still a list, not a bare string",
          cold.value(QStringLiteral("tabs/recent")).typeId() == QMetaType::QStringList
              && cold.value(QStringLiteral("tabs/recent")).toStringList()
                     == QStringList{QStringLiteral("only-one")},
          cold.value(QStringLiteral("tabs/recent")).typeName());
    // JSON has no type for either of these, so they travel as their string
    // form and the accessor puts them back. It works for a URL, and for a
    // QByteArray that holds text; a QByteArray of real binary would not
    // survive, and nothing in the app stores one.
    check("the last-opened paper comes back a URL the app can open",
          cold.value(QStringLiteral("paper/lastUrl")).toUrl() == lastUrl,
          cold.value(QStringLiteral("paper/lastUrl")).toUrl().toString());
    check("a QByteArray of text comes back byte for byte",
          cold.value(QStringLiteral("layout/splitterState")).toByteArray()
              == QByteArray("0.30,0.45,0.25"));

    check("a key nested three deep comes back at the same key",
          cold.value(QStringLiteral("panes/folder/visible")).toBool());
    check("...so does one whose segment is a 64-character hash",
          cold.value(hexKey).typeId() == QMetaType::Int
              && cold.value(hexKey).toInt() == 12);
    check("...one with a dot in the name",
          cold.value(QStringLiteral("updates/manifest.url")).toString()
              == QStringLiteral("https://aireader.d2ssoft.com/manifest.json"));
    check("...and one with a space in it",
          cold.value(QStringLiteral("panes/Paper Pane/width")).toInt() == 320);
    check("a value and a branch can share a name, as QSettings allows",
          cold.value(QStringLiteral("panes/summary")).toString()
                  == QStringLiteral("collapsed")
              && cold.value(QStringLiteral("panes/summary/visible")).toBool());
    check("nothing was lost and nothing was invented",
          cold.allKeys().size() == QSettings().allKeys().size(),
          QStringLiteral("%1 keys cold, %2 warm")
              .arg(cold.allKeys().size())
              .arg(QSettings().allKeys().size()));

    // ── A write that fails must not take the old file with it ───────
    // QSettings hands a format handler a QSaveFile: the bytes go to a
    // temporary file and are renamed over the real one only once the handler
    // returns true. This checks that claim rather than trusting it, with a
    // handler that writes rubbish and then reports failure.
    {
        const QString atomicRoot = side + QStringLiteral("/atomic");
        const QSettings::Format fmt = QSettings::registerFormat(
            QStringLiteral("atomictest"), harnessRead, harnessWrite,
            Qt::CaseSensitive);
        QSettings::setPath(fmt, QSettings::UserScope, atomicRoot);
        QString atomicFile;
        {
            QSettings s(fmt, QSettings::UserScope, id.organization,
                        id.application);
            atomicFile = s.fileName();
            s.setValue(QStringLiteral("k"), 1);
            s.sync();
        }
        check("the fixture wrote a good file first",
              readBytes(atomicFile) == QByteArray("{\"written\":true}\n"));
        g_writeShouldFail = true;
        {
            QSettings s(fmt, QSettings::UserScope, id.organization,
                        id.application);
            s.setValue(QStringLiteral("k"), 2);
            s.sync();
            check("a failed write is reported, not swallowed",
                  s.status() != QSettings::NoError,
                  QStringLiteral("status %1").arg(int(s.status())));
        }
        g_writeShouldFail = false;
        check("...and the file that was already there is untouched",
              readBytes(atomicFile) == QByteArray("{\"written\":true}\n"),
              QString::fromUtf8(readBytes(atomicFile)));
        check("...with no half-written leftovers beside it",
              filesUnder(atomicRoot) == QStringList{atomicFile},
              filesUnder(atomicRoot).join(QStringLiteral(", ")));
    }

    // ── A corrupt file ──────────────────────────────────────────────
    {
        const QString broken = side + QStringLiteral("/broken.json");
        const QByteArray damaged("{\n    \"llm\": {\n        \"model\": \"cla");
        writeBytes(broken, damaged);
        QSettings s(broken, StorageIdentity::settingsFormat());
        check("a corrupt file is reported as a format error",
              s.status() == QSettings::FormatError,
              QStringLiteral("status %1").arg(int(s.status())));
        check("...the app sees no keys, so every setting falls back to its default",
              s.allKeys().isEmpty()
                  && s.value(QStringLiteral("llm/model"),
                             QStringLiteral("claude-opus-5"))
                             .toString()
                         == QStringLiteral("claude-opus-5"));
        check("...and what was in it is kept, because the next write replaces it",
              readBytes(broken + QStringLiteral(".corrupt")) == damaged);
    }
    {
        // An empty file is an install with nothing saved yet, not a damaged
        // one -- our own writes cannot produce one, so something outside the
        // app made it, and refusing to start over it would help nobody.
        const QString empty = side + QStringLiteral("/empty.json");
        writeBytes(empty, QByteArray());
        QSettings s(empty, StorageIdentity::settingsFormat());
        check("an empty file is not a corrupt one",
              s.status() == QSettings::NoError && s.allKeys().isEmpty());
    }

    // ── Applying twice, and starting over ───────────────────────────
    StorageIdentity::apply(id, root);
    check("applying again points at the same file and disturbs nothing",
          StorageIdentity::settingsFilePath() == file
              && QSettings().value(QStringLiteral("llm/model")).toString()
                     == QStringLiteral("claude-opus-5")
              && filesUnder(root) == QStringList{file});

    // The support instruction: delete the file, start again. It has to leave
    // an install that comes up empty rather than one that will not come up.
    check("the file can be deleted", QFile::remove(file));
    check("...and the app then reads a clean slate",
          QSettings().allKeys().isEmpty());
    {
        QSettings s;
        s.setValue(QStringLiteral("ui/language"), QStringLiteral("zh_CN"));
        s.sync();
    }
    check("...and writes it straight back",
          QString::fromUtf8(readBytes(file))
              .contains(QStringLiteral("\"language\": \"zh_CN\"")));

    // ── Where it goes when nobody pins it ───────────────────────────
    // The shipping call passes no root at all, so this is the line that
    // decides where a real install's file ends up -- and it is the one line
    // the checks above never reach. Qt's test mode has the config directory
    // pointing inside ~/.qttest, and naming a file does not create one, so
    // asking is free.
    StorageIdentity::apply(id);
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    check("left to itself it lands in the platform's config directory",
          !configDir.isEmpty()
              && StorageIdentity::settingsFilePath().startsWith(configDir
                                                                + QChar('/'))
              && StorageIdentity::settingsFilePath().endsWith(
                  QStringLiteral(".json")),
          StorageIdentity::settingsFilePath());
    StorageIdentity::apply(id, root);

    // ── The keychain's insecure fallback ────────────────────────────
    // With no keyring on the box, QtKeychain stores secrets in a QSettings of
    // its own, built as QSettings(service) from the service string -- ours is
    // "ai-reader". That constructor takes an explicit organization, so it is
    // NativeFormat whatever default format we set, and its store is named
    // after the service alone. Both of those have to hold, or the app's API
    // key would end up in the file we tell people to mail us. Checked with a
    // throwaway service name for the same reason as everything else here.
    {
        QSettings fallback(id.organization);   // shaped exactly like theirs
        check("the keychain's insecure fallback stays out of our file",
              fallback.fileName() != file
                  && !fallback.fileName().startsWith(root),
              fallback.fileName());
        check("...because that constructor is native, not our default format",
              fallback.format() == QSettings::NativeFormat);
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
