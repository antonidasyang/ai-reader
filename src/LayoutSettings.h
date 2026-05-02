#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>

// Persists the user's main-window layout choices (currently just the
// pane order in the central SplitView). QML reads/writes via the
// "layoutSettings" context property registered in main.cpp.
class LayoutSettings : public QObject
{
    Q_OBJECT

public:
    explicit LayoutSettings(QObject *parent = nullptr);

    // Comma-separated list of pane object names in the order they
    // should appear left-to-right. Empty string when nothing is saved
    // (callers should fall back to the QML-declared default order).
    Q_INVOKABLE QString paneOrder() const;
    Q_INVOKABLE void setPaneOrder(const QString &csv);

    // True once the user has finished or skipped the first-run
    // welcome wizard. Reset by deleting the QSettings entry; the
    // toolbar Help button re-launches the wizard regardless of flag.
    Q_INVOKABLE bool wizardSeen() const;
    Q_INVOKABLE void setWizardSeen(bool seen);

    // Opaque QByteArray returned by SplitView.saveState() — encodes
    // every handle position. Empty when nothing's saved yet (first
    // launch or post-uninstall reinstall).
    Q_INVOKABLE QByteArray splitterState() const;
    Q_INVOKABLE void setSplitterState(const QByteArray &state);

    // App version the user last saw the changelog for. We compare
    // settings.appVersion against this on launch and pop the
    // changelog dialog once on each version bump. Empty on a brand-
    // new install (the welcome wizard fires instead).
    Q_INVOKABLE QString lastSeenVersion() const;
    Q_INVOKABLE void setLastSeenVersion(const QString &v);

    // Returns the bundled CHANGELOG.md text for the given UI locale,
    // or an empty string if neither the localized file nor the
    // English fallback can be located. localeCode is the same string
    // Settings::uiLanguage() exposes (e.g. "zh_CN", "en", or empty
    // to follow QLocale::system()). Tries the Qt resource first,
    // then a sibling file beside the executable.
    Q_INVOKABLE QString readChangelog(const QString &localeCode = QString()) const;

    // Per-pane visibility (folder / toc / chat / summary / ...).
    // Each pane binds visible: layoutSettings.paneVisible("name", <default>)
    // and persists with onVisibleChanged: setPaneVisible("name", visible).
    Q_INVOKABLE bool paneVisible(const QString &name, bool defaultValue) const;
    Q_INVOKABLE void setPaneVisible(const QString &name, bool visible);

    // Per-pane width in px. Each pane binds
    //     SplitView.preferredWidth: layoutSettings.paneWidth("name", default)
    //     onWidthChanged: layoutSettings.setPaneWidth("name", width)
    // The setter is debounced via a 300 ms timer so a single drag
    // (which fires onWidthChanged at frame rate) becomes one disk
    // write rather than dozens.
    Q_INVOKABLE int  paneWidth(const QString &name, int defaultWidth) const;
    Q_INVOKABLE void setPaneWidth(const QString &name, int width);

private:
    void flushPendingWidths();

    QSettings m_qs;
    QHash<QString, int> m_pendingWidths;
    QTimer m_widthSaveTimer;
};
