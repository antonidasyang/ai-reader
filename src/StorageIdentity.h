#pragma once

#include <QSettings>
#include <QString>

// What the app calls itself on disk, and where it keeps its settings.
//
// THE NAMES. Everything QStandardPaths hands out is
// <base>/<organization>/<application>, so those two names are what a user
// finds when they go looking in Finder or Explorer. They are D2S/AIReader:
// the brand and the product, rather than a lowercase repository name where
// the brand belongs and a space in a directory name that every shell and
// script downstream then has to quote.
//
// THE SETTINGS FILE. QSettings' native backend is three different things --
// the Windows registry, a cfprefsd-managed plist on macOS, a .conf on Linux
// -- and on the platform most of our users are on it is not a file at all.
// Someone whose app will not start cannot be talked through regedit, and
// nobody can send us a registry hive. So the whole app writes ONE plain JSON
// file instead: apply() registers a JSON format with QSettings and makes it
// the default, which is what every default-constructed QSettings in the app
// then opens, and pins the directory that file goes in rather than leaving
// it to Qt's per-platform default. The file can be read, hand-edited,
// diffed, backed up, mailed to us, and deleted to get a wedged install back
// on its feet.
//
// JSON rather than INI because INI is a lossy round trip: Qt writes every
// value as text and the next launch reads text back, so a bool returns as
// the string "true" and an int as "8192". JSON has real types, and what
// comes back is what went in.
namespace StorageIdentity {

// The three names an install is keyed to. The domain is empty on purpose:
// QSettings on Apple platforms keys its file on the organization DOMAIN when
// one is set and falls back to the NAME when it is not, so a domain here
// would scatter the settings file into a "d2ssoft.com" directory while every
// other directory the app owns sits under "D2S". The struct still carries
// the field because a test identity may want one.
struct Identity {
    QString organization;
    QString domain;
    QString application;
};

// What the app ships as: D2S / d2ssoft.com / AIReader.
Identity current();

// Install the names and the one settings file. Call once at the top of
// main(), before QGuiApplication exists and before anything constructs a
// QSettings, reads a setting, or asks QStandardPaths for a directory.
void apply();

// The same, pointed somewhere else. For tests: a harness must never write
// over a real install's settings, so it passes throwaway names and a scratch
// directory of its own. An empty configRoot means the platform's usual place.
void apply(const Identity &id, const QString &configRoot = QString());

// The JSON format itself, registered once and cached. Only needed by code
// that has to open a settings file by name rather than by identity -- a test
// reading a copy of one, say. Everything in the app gets it for free by
// default-constructing a QSettings after apply().
QSettings::Format settingsFormat();

// Where that one file is, in full, so the app can tell a user exactly what
// to look at (and the startup log can record it). It answers for whichever
// identity apply() installed, not for the one the app ships as -- those are
// the same thing everywhere except in a test, and a path that named the real
// file while a test was writing somewhere else would be worse than useless.
// After apply(), that is:
//
//   macOS    ~/Library/Preferences/D2S/AIReader.json
//   Windows  C:\Users\<user>\AppData\Local\D2S\AIReader.json
//   Linux    ~/.config/D2S/AIReader.json
//
// Naming the file does not create it; it appears on the first write.
QString settingsFilePath();
QString settingsFilePath(const Identity &id);

// The identity itself, for anyone who needs to name it.
QString organization();
QString applicationName();

} // namespace StorageIdentity
