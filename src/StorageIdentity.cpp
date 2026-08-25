#include "StorageIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Everything QStandardPaths hands out is <base>/<organization>/<application>,
// and those two names used to be "ai-reader" and "AI Reader" -- a lowercase
// repository name where the brand belongs, and a space in a directory name
// that every shell and script then has to quote. They are D2S/AIReader now.
//
// The rename moves the data rather than orphaning it: the caches, the
// library database and the settings are all keyed to those directories, and
// a user who updates would otherwise find an empty app. Done once, before
// anything reads them, and the old names are asked for the paths directly
// rather than reconstructed from the new ones.
constexpr auto kLegacyOrg = "ai-reader";
constexpr auto kLegacyDomain = "ai-reader.local";
constexpr auto kLegacyApp = "AI Reader";
constexpr auto kOrg = "D2S";
constexpr auto kDomain = "d2ssoft.com";
constexpr auto kApp = "AIReader";

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    bool ok = true;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = QDir(from).relativeFilePath(src);
        const QString dst = to + QChar('/') + rel;
        if (QFileInfo(src).isDir()) {
            ok = QDir().mkpath(dst) && ok;
        } else {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            ok = QFile::copy(src, dst) && ok;
        }
    }
    return ok;
}

} // namespace

namespace StorageIdentity {

Result migrateFromLegacy()
{
    Result result;
    const QList<QStandardPaths::StandardLocation> locations{
        QStandardPaths::AppDataLocation, QStandardPaths::AppLocalDataLocation,
        QStandardPaths::CacheLocation, QStandardPaths::AppConfigLocation};

    // Ask the OLD identity where its things were.
    QCoreApplication::setOrganizationName(QString::fromLatin1(kLegacyOrg));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(kLegacyDomain));
    QCoreApplication::setApplicationName(QString::fromLatin1(kLegacyApp));
    QList<QString> oldPaths;
    for (auto loc : locations)
        oldPaths.append(QStandardPaths::writableLocation(loc));
    // Default-constructed on purpose, while the legacy identity is set: on
    // macOS QSettings names its file after the organization DOMAIN when one
    // is set, so QSettings(org, app) would look somewhere the app never
    // wrote. The object keeps the path it resolved, so it can still be read
    // after the identity below changes.
    QSettings oldSettings;
    const QStringList oldKeys = oldSettings.allKeys();

    // ...then switch to the new one and put them there.
    QCoreApplication::setOrganizationName(QString::fromLatin1(kOrg));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(kDomain));
    QCoreApplication::setApplicationName(QString::fromLatin1(kApp));

    for (int i = 0; i < locations.size(); ++i) {
        const QString from = oldPaths.at(i);
        const QString to = QStandardPaths::writableLocation(locations.at(i));
        if (from.isEmpty() || to.isEmpty() || from == to)
            continue;
        if (!QFileInfo::exists(from) || QFileInfo::exists(to))
            continue;                  // nothing to move, or already moved
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (QDir().rename(from, to)) {
            result.movedDirs.append(to);
            continue;                  // same volume: instant
        }
        // Different volume, or something has a file open: copy instead and
        // leave the original alone rather than risk half a move.
        if (copyTree(from, to))
            result.movedDirs.append(to);
    }

    if (oldKeys.isEmpty())
        return result;
    QSettings fresh;
    if (!fresh.allKeys().isEmpty())
        return result;                 // already migrated, or already used
    for (const QString &key : oldKeys)
        fresh.setValue(key, oldSettings.value(key));
    fresh.sync();
    result.settingsKeys = oldKeys.size();
    return result;
}

QString organization() { return QString::fromLatin1(kOrg); }
QString applicationName() { return QString::fromLatin1(kApp); }

void apply()
{
    migrateFromLegacy();
}

} // namespace StorageIdentity
