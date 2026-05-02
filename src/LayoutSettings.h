#pragma once

#include <QByteArray>
#include <QObject>
#include <QSettings>
#include <QString>

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

    // Returns the bundled CHANGELOG.md text or an empty string if it
    // can't be located. Tries the Qt resource (:/CHANGELOG.md) first,
    // then a copy beside the executable -- so it works in dev runs
    // from build/, in packaged installs, and in build configurations
    // that for whatever reason didn't compile the resource in.
    Q_INVOKABLE QString readChangelog() const;

    // Per-pane visibility (folder / toc / chat / summary / ...).
    // Each pane binds visible: layoutSettings.paneVisible("name", <default>)
    // and persists with onVisibleChanged: setPaneVisible("name", visible).
    Q_INVOKABLE bool paneVisible(const QString &name, bool defaultValue) const;
    Q_INVOKABLE void setPaneVisible(const QString &name, bool visible);

private:
    QSettings m_qs;
};
