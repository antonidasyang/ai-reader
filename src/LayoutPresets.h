#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Named pane arrangements: "Reading", "Writing", "Everything open" -- the
// panes the reader wants, at the widths they want them, in the order they
// want them, saved under a name and put back on demand.
//
// WHO DOES WHAT. Only QML knows what the SplitView is actually doing right
// now -- which pane sits where, how wide each one ended up after the last
// drag -- so QML takes the snapshot and QML applies it, through exactly the
// properties a toolbar click and a DockGrip drag write (`visible`,
// `SplitView.preferredWidth`, takeItem/insertItem). This class owns the
// three things QML should not: where the presets live, what a legal name
// is, and the shape of the JSON.
//
// WIDTHS ARE FRACTIONS, NEVER PIXELS. A layout saved on a 3840-wide desktop
// has to be usable on a 1366-wide laptop, and a pixel width saved on the
// first leaves nothing but the scrollbars of the second. Every width in the
// document is a fraction of the SplitView's content width; resolve() turns
// the fractions back into pixels for a given window, clamping so no pane is
// narrower than its minimum and the row still fits. Window geometry is
// deliberately NOT part of a preset: a position from another machine's
// monitor arrangement puts the window off-screen.
//
// THE DOCUMENT. One JSON string in one QSettings key ("layouts/presets"),
// so it rides the account-prefs payload as a value like any other and needs
// nothing of the sync layer:
//
//   {
//     "version": 1,
//     "presets": [
//       { "name": "Reading",
//         "order": ["folder","toc","pdf","blocks","chat"],
//         "panes": {
//           "folder": { "visible": true,  "width": 0.0625 },
//           "toc":    { "visible": true,  "width": 0.0573 },
//           "pdf":    { "visible": true,  "width": 0.4167 },
//           "blocks": { "visible": true,  "width": 0.4010 },
//           "chat":   { "visible": false, "width": 0.0938 }
//         } }
//     ]
//   }
//
// The array is kept sorted by name, the fractions rounded to four places
// and the JSON written compact, so two machines holding the same layouts
// produce the same bytes -- which is what lets the sync layer tell a real
// change from a re-save and skip the pointless write.
//
// A pane a preset does not mention is LEFT ALONE when the preset is
// applied, never hidden: a preset written by an older build simply has no
// opinion about a pane that build did not have, and hiding it would make
// every upgrade look like a pane had been taken away. For the same reason
// panes this build has never heard of survive a round trip through it --
// they are carried in the document and ignored on apply, so a preset saved
// on a newer build is not quietly emptied out by an older one.
class LayoutPresets : public QObject
{
    Q_OBJECT

    // The saved names, sorted, in the spelling they were saved with. This
    // is what the menu lists.
    Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)

    // The preset the arrangement on screen came from, or empty once the
    // reader has changed anything since. QML owns that judgement -- it is
    // the only side that sees a pane being toggled, dragged or resized --
    // so it writes this property: the name after applying one, an empty
    // string on the next change. Per machine, and deliberately not part of
    // the synced document: which layout this screen is showing is not a
    // fact about the account.
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)

public:
    explicit LayoutPresets(QObject *parent = nullptr);

    QStringList names() const;
    QString current() const { return m_current; }
    void setCurrent(const QString &name);

    // Save the arrangement QML just measured under `name`. The map is
    //   { "order": ["folder", ...],
    //     "panes": { "folder": { "visible": true, "width": 0.0625 }, ... } }
    // with every width a FRACTION of the content width. A name is trimmed
    // and must not be empty; saving over a name that already exists --
    // case-insensitively, so "reading" lands on "Reading" -- replaces that
    // preset rather than adding a second one, and the new spelling wins,
    // because the reader just typed it. Saving also makes it `current`.
    Q_INVOKABLE void save(const QString &name, const QVariantMap &snapshot);

    // The stored document for `name`, fractions and all:
    //   { "name": ..., "order": [...], "panes": {...} }
    // An empty map when nothing is saved under that name.
    Q_INVOKABLE QVariantMap load(const QString &name) const;

    // Renaming refuses a collision rather than silently swallowing the
    // preset that is already called that -- unlike save(), where replacing
    // is what the reader asked for. Trimmed, non-empty, false if either end
    // of the operation does not check out.
    Q_INVOKABLE bool rename(const QString &from, const QString &to);
    Q_INVOKABLE bool remove(const QString &name);

    Q_INVOKABLE bool contains(const QString &name) const;
    // The spelling `name` is actually stored under, or empty. The dialog
    // uses it to warn about the preset the reader is really about to
    // replace ("Reading") rather than the one they typed ("reading").
    Q_INVOKABLE QString existingName(const QString &name) const;

    // The preset in pixels, for a window this wide.
    //
    //   contentWidth  the SplitView's own width, in px
    //   minimums      pane name -> SplitView.minimumWidth, in px
    //
    // Returns
    //   { "found": true, "name": ..., "order": [...],
    //     "panes": { "folder": { "visible": true, "width": 240 }, ... } }
    // or { "found": false }.
    //
    // Widths come back as whole pixels, never below the pane's own minimum
    // and never wider than the window; when the fractions ask for more room
    // than there is (a wide layout on a narrow screen, or minimums that add
    // up), every pane keeps its minimum and what is left over is shared out
    // in proportion to what each pane asked for above it. Only visible
    // panes take part in that arithmetic -- a hidden pane still gets its
    // width back so that showing it later gives a sensible column rather
    // than a sliver. A pane the preset does not mention is simply absent
    // from "panes", which is how QML knows to leave it alone.
    Q_INVOKABLE QVariantMap resolve(const QString &name, qreal contentWidth,
                                    const QVariantMap &minimums) const;

    // Re-read the stored document. For the moment the account sync writes
    // a payload from another machine straight into the settings file: this
    // object is holding the old one and nothing else would tell it.
    Q_INVOKABLE void reload();

    // The one QSettings key the whole thing lives in. Settings names it in
    // accountSettingKeys() and carries its value in and out of the account
    // payload, so it must come from here rather than from a second copy of
    // the string.
    static QString settingsKey();

    // Is this text a presets document at all? The account payload arrives
    // from a server, written by another build or -- when something has gone
    // wrong -- by something else entirely, and the answer to rubbish is to
    // keep what is on this machine, not to overwrite it with nonsense.
    static bool isPresetDocument(const QString &json);

signals:
    void namesChanged();
    void currentChanged();
    // The stored document changed HERE, on this machine. main() relays it
    // to Settings so the account sync notices there is something to push;
    // reload() deliberately does not raise it, since a change that arrived
    // from the account is not one to send back.
    void presetsChanged();

private:
    int indexOf(const QString &name) const;
    void commit(const QJsonArray &next);
    void store();

    QSettings m_qs;
    QJsonArray m_presets;
    QString m_current;
};
