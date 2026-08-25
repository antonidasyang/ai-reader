#pragma once

#include <QString>
#include <QStringList>

// What the app calls itself on disk.
//
// Everything QStandardPaths hands out is <base>/<organization>/<application>,
// and those two names used to be "ai-reader" and "AI Reader": a lowercase
// repository name where the brand belongs, and a space in a directory name
// that every shell and script downstream then has to quote. They are
// D2S/AIReader now.
//
// Renaming them moves the caches, the library database and the settings with
// them -- all of it is keyed to those directories, and a user who updated
// would otherwise find an empty application. It happens once, before anything
// reads a setting, and it asks the OLD identity where its things were rather
// than reconstructing paths from the new one.
namespace StorageIdentity {

// Migrate if needed, then leave the current identity set. Call once, before
// QGuiApplication exists.
void apply();

// What a migration did, for the log and for the tests.
struct Result {
    QStringList movedDirs;
    int settingsKeys = 0;
};
Result migrateFromLegacy();

// The identity itself, for anyone who needs to name it.
QString organization();
QString applicationName();

} // namespace StorageIdentity
