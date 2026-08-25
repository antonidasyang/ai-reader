// The rename of the app's storage identity, with a user's data in the way.
//
// "ai-reader/AI Reader" became "D2S/AIReader". That is a directory rename
// under every standard location plus a settings key move, and getting it
// wrong means a user updates and finds an empty application: no library, no
// caches, no API key. So it is exercised here on a throwaway root, seeded the
// way a real install looks.

#include "StorageIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

static int g_pass = 0, g_fail = 0;
static void check(const QString &name, bool ok, const QString &detail = {})
{
    (ok ? g_pass : g_fail)++;
    qInfo().noquote() << (ok ? "PASS " : "FAIL ") << name
                      << (detail.isEmpty() ? QString() : "  - " + detail);
}

static bool writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&f) << text;
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Directories go under the Qt test root. Settings need their own
    // isolation on top of that: QSettings' native backend on macOS is
    // cfprefsd, which setTestModeEnabled does NOT redirect -- a test that
    // wrote there would be writing the real application's preferences, and
    // clear() would delete them. Ini format under a scratch path keeps every
    // platform in a directory this test owns. The migration logic under test
    // is format-agnostic: it reads the old QSettings and writes the new one.
    QStandardPaths::setTestModeEnabled(true);
    const QString settingsRoot =
        QDir::tempPath() + QStringLiteral("/ai-reader-storage-harness");
    QDir(settingsRoot).removeRecursively();
    QDir().mkpath(settingsRoot);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);

    // ── Seed an install under the old identity ───────────────────────
    QCoreApplication::setOrganizationName(QStringLiteral("ai-reader"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ai-reader.local"));
    QCoreApplication::setApplicationName(QStringLiteral("AI Reader"));

    const QString oldData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString oldCache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir(oldData).removeRecursively();
    QDir(oldCache).removeRecursively();
    writeFile(oldData + QStringLiteral("/library/library.db"), QStringLiteral("db"));
    writeFile(oldData + QStringLiteral("/cache/blocks/abc.json"),
              QStringLiteral("{\"paperId\":\"abc\"}"));
    writeFile(oldCache + QStringLiteral("/blobs/deadbeef.pdf"),
              QStringLiteral("%PDF"));
    {
        QSettings old;
        old.clear();
        old.setValue(QStringLiteral("llm/model"), QStringLiteral("claude-opus-5"));
        old.setValue(QStringLiteral("ui/language"), QStringLiteral("zh_CN"));
        old.setValue(QStringLiteral("window/width"), 1400);
        old.sync();
    }

    // Make sure the new identity starts empty, so this is a real migration.
    QCoreApplication::setOrganizationName(QStringLiteral("D2S"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("d2ssoft.com"));
    QCoreApplication::setApplicationName(QStringLiteral("AIReader"));
    const QString newData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString newCache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir(newData).removeRecursively();
    QDir(newCache).removeRecursively();
    { QSettings fresh; fresh.clear(); fresh.sync(); }

    check("the new locations do not contain a space",
          !newData.contains(QStringLiteral("AI Reader"))
              && newData.contains(QStringLiteral("AIReader")),
          newData);
    check("...and are under the brand, not the repository name",
          newData.contains(QStringLiteral("D2S"))
              && !newData.contains(QStringLiteral("ai-reader")),
          newData);

    // ── The move ────────────────────────────────────────────────────
    const StorageIdentity::Result result = StorageIdentity::migrateFromLegacy();

    check("the library database came across",
          QFileInfo::exists(newData + QStringLiteral("/library/library.db")),
          QStringLiteral("%1 directories moved").arg(result.movedDirs.size()));
    check("so did the paragraph cache",
          QFileInfo::exists(newData + QStringLiteral("/cache/blocks/abc.json")));
    check("so did the downloaded PDFs",
          QFileInfo::exists(newCache + QStringLiteral("/blobs/deadbeef.pdf")));
    {
        QSettings now;
        check("the settings came across",
              now.value(QStringLiteral("llm/model")).toString()
                  == QStringLiteral("claude-opus-5"),
              QStringLiteral("%1 keys").arg(result.settingsKeys));
        check("...all of them",
              now.value(QStringLiteral("ui/language")).toString()
                      == QStringLiteral("zh_CN")
                  && now.value(QStringLiteral("window/width")).toInt() == 1400);
    }
    check("the app is left running under the new identity",
          QCoreApplication::organizationName() == QStringLiteral("D2S")
              && QCoreApplication::applicationName() == QStringLiteral("AIReader"));

    // ── Running again must not undo it ───────────────────────────────
    {
        QSettings now;
        now.setValue(QStringLiteral("llm/model"), QStringLiteral("changed-since"));
        now.sync();
    }
    writeFile(newData + QStringLiteral("/library/library.db"),
              QStringLiteral("db-with-newer-work"));
    const StorageIdentity::Result again = StorageIdentity::migrateFromLegacy();
    check("a second run moves nothing", again.movedDirs.isEmpty()
              && again.settingsKeys == 0);
    {
        QSettings now;
        check("...and does not overwrite what has happened since",
              now.value(QStringLiteral("llm/model")).toString()
                  == QStringLiteral("changed-since"));
    }
    QFile db(newData + QStringLiteral("/library/library.db"));
    (void)db.open(QIODevice::ReadOnly);
    check("...nor the library it is now using",
          QString::fromUtf8(db.readAll()) == QStringLiteral("db-with-newer-work"));

    // ── A fresh install has nothing to move ─────────────────────────
    QDir(newData).removeRecursively();
    QDir(newCache).removeRecursively();
    QCoreApplication::setOrganizationName(QStringLiteral("ai-reader"));
    QCoreApplication::setApplicationName(QStringLiteral("AI Reader"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .removeRecursively();
    { QSettings old; old.clear(); old.sync(); }
    QCoreApplication::setOrganizationName(QStringLiteral("D2S"));
    QCoreApplication::setApplicationName(QStringLiteral("AIReader"));
    const StorageIdentity::Result clean = StorageIdentity::migrateFromLegacy();
    check("a fresh install migrates nothing and says so",
          clean.movedDirs.isEmpty() && clean.settingsKeys == 0);

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("%1 passed, %2 failed").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
