#pragma once

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

private:
    QSettings m_qs;
};
