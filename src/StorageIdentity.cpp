#include "StorageIdentity.h"

#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <limits>

namespace {

// The brand and the product. Everything QStandardPaths hands out is
// <base>/<organization>/<application>, so these two are the directory names
// a user sees; the domain is what QSettings on macOS keys its file on.
constexpr auto kOrg = "D2S";
// Deliberately empty. QSettings names its directory after the organization
// DOMAIN when one is set and the NAME only when it is not -- and only on
// Apple platforms, where it would have put the settings file under
// "d2ssoft.com/" while every other directory the app owns sits under "D2S/".
// One brand directory on every platform is worth more than a domain nobody
// reads; the domain lives in the update and sync URLs, which is where it
// means something.
constexpr auto kDomain = "";
constexpr auto kApp = "AIReader";

// ── The JSON settings format ────────────────────────────────────────
//
// QSettings hands a format handler a flat map keyed by '/'-separated paths
// -- llm/model, panes/folder/visible, reading/<sha256> -- and takes one back
// the same way. Nothing above these two functions knows or cares: the ~58
// keys the app reads go through default-constructed QSettings objects and
// are untouched by this.
//
// JSON rather than INI because INI is a lossy round trip. Qt's INI writer
// turns every value into text, and the *next launch* reads text back: a bool
// returns as the string "true", an int as "8192". The accessors paper over
// it -- QVariant("false").toBool() is false, by special case -- but nothing
// else does, and a comparison against a QVariant is then quietly wrong. JSON
// has real types, so what comes back is what went in.

QJsonValue variantToJson(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return QJsonValue(v.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QJsonValue(v.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return QJsonValue(v.toDouble());
    case QMetaType::QString:
        return QJsonValue(v.toString());
    case QMetaType::QStringList: {
        QJsonArray array;
        for (const QString &s : v.toStringList())
            array.append(s);
        return array;
    }
    case QMetaType::QVariantList: {
        QJsonArray array;
        for (const QVariant &e : v.toList())
            array.append(variantToJson(e));
        return array;
    }
    default:
        // A QUrl, a QDate, a QByteArray: JSON has no notion of any of them,
        // so the string form goes in the file and the accessor puts it back
        // -- value().toUrl() reparses a URL string, which is how
        // paper/lastUrl survives. The one lossy corner is a QByteArray
        // holding real binary rather than text: its bytes are not valid
        // UTF-8, and what comes back out of toByteArray() will not be what
        // went in. Nothing in the app stores one today (layout/splitterState
        // has the type but no caller); if something ever does, this is the
        // line to give a base64 encoding to.
        return QJsonValue(v.toString());
    }
}

QVariant jsonToVariant(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool();
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Double: {
        // JSON has one number type, so this is the line where an int would
        // quietly become a double -- and then window/width, the font sizes
        // and maxTokens would all be doubles on the next launch.
        //
        // toInteger() hands back the fallback when the number is not exactly
        // an integer, so asking twice with different fallbacks says which it
        // is, with no floating-point guesswork and no rounding of a value
        // too big for a double to hold exactly.
        const qint64 whole = v.toInteger(0);
        if (whole != v.toInteger(1))
            return v.toDouble();                  // genuinely fractional
        if (whole >= std::numeric_limits<int>::min()
            && whole <= std::numeric_limits<int>::max())
            return int(whole);                    // what an int was written as
        return whole;
    }
    case QJsonValue::Array: {
        // tabs/urls is a QStringList and has to come back as one. A list
        // that is all strings is exactly that; anything else keeps its
        // elements as a QVariantList.
        const QJsonArray array = v.toArray();
        QStringList strings;
        bool allStrings = true;
        for (const QJsonValue &e : array) {
            if (!e.isString()) {
                allStrings = false;
                break;
            }
            strings.append(e.toString());
        }
        if (allStrings)
            return strings;
        QVariantList list;
        for (const QJsonValue &e : array)
            list.append(jsonToVariant(e));
        return list;
    }
    default:
        return QVariant();
    }
}

// Put one '/'-separated key into the tree, a segment per level. Returns
// false when the shape of the tree will not take it -- QSettings is happy to
// hold both "a/b" and "a/b/c", and JSON cannot have "b" be a value and an
// object at once -- leaving the caller to put it somewhere flatter. Nothing
// is modified on the way out of a failure: each level only commits its child
// once the level below has agreed.
bool insertNested(QJsonObject &object, const QStringList &parts, int depth,
                  const QJsonValue &leaf)
{
    const QString &head = parts.at(depth);
    if (depth == parts.size() - 1) {
        if (object.value(head).isObject())
            return false;                   // a branch already lives there
        object.insert(head, leaf);
        return true;
    }
    const QJsonValue existing = object.value(head);
    if (!existing.isUndefined() && !existing.isObject())
        return false;                       // a value blocks the way down
    QJsonObject child = existing.toObject();
    if (!insertNested(child, parts, depth + 1, leaf))
        return false;
    object.insert(head, child);
    return true;
}

// ...and take one back out. A '/' joins the segments again, so a key written
// flat -- the fallback above -- reads back identical to a nested one.
void flatten(const QJsonObject &object, const QString &prefix,
             QSettings::SettingsMap &map)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = prefix + it.key();
        if (it.value().isObject())
            flatten(it.value().toObject(), key + QChar('/'), map);
        else
            map.insert(key, jsonToVariant(it.value()));
    }
}

// A corrupt file is the user's settings, damaged. Returning false is not on
// its own enough to protect them: QSettings reports FormatError and then
// carries on, and the app's first setValue() writes a fresh document
// straight over the top. So the bytes are put aside first, next to the file
// that is about to be replaced, where they can be read, repaired by hand, or
// sent to us.
void keepCorruptFileAside(QIODevice &device, const QByteArray &raw)
{
    const auto *file = qobject_cast<QFile *>(&device);
    if (!file || file->fileName().isEmpty())
        return;
    QFile aside(file->fileName() + QStringLiteral(".corrupt"));
    if (aside.open(QIODevice::WriteOnly | QIODevice::Truncate))
        aside.write(raw);
}

bool readJson(QIODevice &device, QSettings::SettingsMap &map)
{
    const QByteArray raw = device.readAll();
    // A file with nothing in it is an install with no settings yet, not a
    // damaged one. Our own writes never produce one -- they are atomic --
    // so this only comes up when something outside the app made it.
    if (raw.trimmed().isEmpty())
        return true;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        keepCorruptFileAside(device, raw);
        return false;                       // QSettings reports FormatError
    }
    flatten(doc.object(), QString(), map);
    return true;
}

bool writeJson(QIODevice &device, const QSettings::SettingsMap &map)
{
    QJsonObject root;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QJsonValue leaf = variantToJson(it.value());
        if (!insertNested(root, it.key().split(QChar('/')), 0, leaf))
            root.insert(it.key(), leaf);    // flat, and reads back the same
    }
    // Indented and UTF-8 by Qt; sorted because QJsonObject keeps its keys
    // that way. All of which is the point: someone has to be able to open
    // this file, read it, and see what is wrong.
    QByteArray out = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!out.endsWith('\n'))
        out.append('\n');
    // One write of the whole document. QSettings hands us a QSaveFile, so
    // this lands in a temporary file that is renamed over the real one only
    // if we return true -- a crash or a full disk half way through leaves
    // the previous file exactly as it was.
    return device.write(out) == out.size();
}

// registerFormat hands back a NEW format id on every call, so this is cached
// rather than called from apply(): two ids for one extension would mean the
// app quietly reading one file and writing another.
QSettings::Format jsonFormat()
{
    static const QSettings::Format format = QSettings::registerFormat(
        QStringLiteral("json"), readJson, writeJson, Qt::CaseSensitive);
    return format;
}

// Which of the two names QSettings puts under the root directory. A
// default-constructed QSettings asks QCoreApplication this question itself
// and answers it differently per platform -- the domain wins on Apple
// platforms, the name wins everywhere else. The rule is spelled out here so
// the same answer can be given for an identity that is not the one currently
// installed, which is what a test needs.
QString settingsOrganization(const StorageIdentity::Identity &id)
{
#ifdef Q_OS_DARWIN
    return id.domain.isEmpty() ? id.organization : id.domain;
#else
    return id.organization.isEmpty() ? id.domain : id.organization;
#endif
}

// Where the settings file goes. Left to itself Qt would put a non-native
// format in ~/.config on a Mac -- a hidden directory nobody thinks to look
// in and no Mac user expects -- and in roaming AppData on Windows, away from
// the rest of the app's data. GenericConfigLocation is each platform's own
// answer to "user configuration lives here": ~/Library/Preferences,
// %LOCALAPPDATA%, ~/.config. QSettings appends
// <organization>/<application>.json to it.
QString defaultConfigRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

// Which identity is actually installed. settingsFilePath() has to answer for
// that one rather than for the one the app ships as: they are the same thing
// in the app and deliberately not the same thing in a test, and a path that
// quietly named the shipping file would send a test's writes -- or a user --
// to the wrong place.
StorageIdentity::Identity &installed()
{
    static StorageIdentity::Identity id = StorageIdentity::current();
    return id;
}

} // namespace

namespace StorageIdentity {

Identity current()
{
    return {QString::fromLatin1(kOrg), QString::fromLatin1(kDomain),
            QString::fromLatin1(kApp)};
}

QSettings::Format settingsFormat() { return jsonFormat(); }

void apply(const Identity &id, const QString &configRoot)
{
    QCoreApplication::setOrganizationName(id.organization);
    QCoreApplication::setOrganizationDomain(id.domain);
    QCoreApplication::setApplicationName(id.application);

    // From here on every default-constructed QSettings in the app -- one
    // each in Settings, Library, Tabs, PaperController, ProjectController,
    // AuthController, CompareService, LayoutSettings, and a couple in main()
    // -- resolves to the same JSON file instead of the registry or a plist.
    //
    // setDefaultFormat reaches exactly the default-constructed ones, which is
    // precisely the set we own. A QSettings built with an explicit
    // organization elsewhere stays native, which is what we want: QtKeychain's
    // insecure fallback (Linux with no keyring) builds a QSettings(service)
    // for the service string "ai-reader" and must keep its secrets out of
    // this file. It gets its own native store, ~/.config/ai-reader.conf,
    // nowhere near ours.
    const QSettings::Format format = jsonFormat();
    // Belt and braces: registering can only fail on a malformed extension or
    // a null handler, neither of which is reachable from here, but setting an
    // invalid default format would leave the app unable to read anything at
    // all. Better to stay on the platform's own store than to do that.
    if (format == QSettings::InvalidFormat)
        return;

    QSettings::setDefaultFormat(format);
    const QString root = configRoot.isEmpty() ? defaultConfigRoot() : configRoot;
    if (!root.isEmpty())
        QSettings::setPath(format, QSettings::UserScope, root);
    installed() = id;
}

void apply() { apply(current(), QString()); }

QString settingsFilePath(const Identity &id)
{
    // Ask QSettings instead of assembling the path by hand: it is the thing
    // that decides, so this cannot drift from where the app actually writes.
    // Constructing one names a file without creating it.
    return QSettings(jsonFormat(), QSettings::UserScope,
                     settingsOrganization(id), id.application)
        .fileName();
}

QString settingsFilePath() { return settingsFilePath(installed()); }

QString organization() { return QString::fromLatin1(kOrg); }
QString applicationName() { return QString::fromLatin1(kApp); }

} // namespace StorageIdentity
