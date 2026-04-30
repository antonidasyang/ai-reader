#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// Persists the user's main-window layout choices: the pane order in
// the central SplitView, plus the per-paragraph view toggles
// (whether to show source text, translation text, or both). QML
// reads/writes via the "layoutSettings" context property registered
// in main.cpp.
class LayoutSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool showSource READ showSource WRITE setShowSource
               NOTIFY showSourceChanged)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation
               NOTIFY showTranslationChanged)

public:
    explicit LayoutSettings(QObject *parent = nullptr);

    // Comma-separated list of pane object names in the order they
    // should appear left-to-right. Empty string when nothing is saved
    // (callers should fall back to the QML-declared default order).
    Q_INVOKABLE QString paneOrder() const;
    Q_INVOKABLE void setPaneOrder(const QString &csv);

    bool showSource() const { return m_showSource; }
    void setShowSource(bool v);
    bool showTranslation() const { return m_showTranslation; }
    void setShowTranslation(bool v);

signals:
    void showSourceChanged();
    void showTranslationChanged();

private:
    QSettings m_qs;
    bool      m_showSource;
    bool      m_showTranslation;
};
