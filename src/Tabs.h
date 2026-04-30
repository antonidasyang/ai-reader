#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QVector>

class PaperController;

// Multi-PDF tab list, modelled after the editor tabs in VS Code.
//
// Tabs is the new authoritative source for "which papers are open and
// which one is showing." PaperController still owns the *current*
// paper's loaded state (document, blocks, translation status); Tabs
// just maintains the URL list, the active index, and the persistence
// layer.
//
// To keep callers in sync without funnelling every code path through
// Tabs, this class also reacts to PaperController::pdfSourceChanged:
// any URL the controller starts showing gets added/promoted to the
// active tab automatically. That way the legacy restoreLast() flow
// and any direct paperController.openPdf() still produce a tab.
class Tabs : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY tabsChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)

public:
    explicit Tabs(PaperController *paper, QObject *parent = nullptr);

    int count() const { return m_papers.size(); }
    int activeIndex() const { return m_activeIndex; }

    Q_INVOKABLE QUrl   urlAt(int idx) const;
    Q_INVOKABLE QString nameAt(int idx) const;

    // Open `url` as a tab and make it active. If it's already in the
    // list, just switch to it.
    Q_INVOKABLE void openPaper(const QUrl &url);
    // Close the tab at `idx`. If it's the active one, switch to a
    // neighbour (preferring the tab that was to its right). Closing
    // the last tab clears the PaperController.
    Q_INVOKABLE void closePaper(int idx);
    // Switch the active tab without changing the list.
    Q_INVOKABLE void activatePaper(int idx);
    // Restore the saved tab list and re-open the active one. Returns
    // true if at least one tab was restored.
    Q_INVOKABLE bool restoreSession();

signals:
    void tabsChanged();
    void activeIndexChanged();

private:
    void persist();
    int  indexOf(const QUrl &url) const;
    void onPaperSourceChanged();

    QVector<QUrl>     m_papers;
    int               m_activeIndex = -1;
    PaperController  *m_paper;
    QSettings         m_qs;
};
