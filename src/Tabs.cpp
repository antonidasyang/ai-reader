#include "Tabs.h"

#include "PaperController.h"

#include <QFileInfo>

namespace {
constexpr auto kKeyUrls   = "tabs/urls";
constexpr auto kKeyActive = "tabs/active";
} // namespace

Tabs::Tabs(PaperController *paper, QObject *parent)
    : QObject(parent)
    , m_paper(paper)
{
    if (m_paper) {
        connect(m_paper, &PaperController::pdfSourceChanged,
                this, &Tabs::onPaperSourceChanged);
    }
}

QUrl Tabs::urlAt(int idx) const
{
    if (idx < 0 || idx >= m_papers.size()) return {};
    return m_papers.at(idx);
}

QString Tabs::nameAt(int idx) const
{
    const QUrl u = urlAt(idx);
    if (u.isLocalFile())
        return QFileInfo(u.toLocalFile()).fileName();
    return u.fileName();
}

int Tabs::indexOf(const QUrl &url) const
{
    for (int i = 0; i < m_papers.size(); ++i)
        if (m_papers.at(i) == url)
            return i;
    return -1;
}

void Tabs::openPaper(const QUrl &url)
{
    if (url.isEmpty() || !m_paper) return;
    // Delegate to the controller; the pdfSourceChanged handler will
    // promote the URL to a tab if it isn't one already.
    m_paper->openPdf(url);
}

void Tabs::closePaper(int idx)
{
    if (idx < 0 || idx >= m_papers.size()) return;
    const bool wasActive = (idx == m_activeIndex);

    m_papers.removeAt(idx);

    int newActive = m_activeIndex;
    if (m_papers.isEmpty()) {
        newActive = -1;
    } else if (wasActive) {
        // Prefer the tab that took the closed slot's index (i.e., the
        // former right neighbour). Falls back to the new last tab when
        // the rightmost tab was closed.
        newActive = qMin(idx, m_papers.size() - 1);
    } else if (idx < m_activeIndex) {
        newActive = m_activeIndex - 1;
    }

    const bool activeChanged = (newActive != m_activeIndex);
    m_activeIndex = newActive;
    emit tabsChanged();
    if (activeChanged) emit activeIndexChanged();
    persist();

    // Tell the PaperController about the new selection. We keep this
    // last so any side-effects (signals, blocks reset) see a settled
    // tab list. The pdfSourceChanged handler is a no-op here because
    // m_activeIndex is already correct.
    if (m_papers.isEmpty()) {
        m_paper->clear();
    } else if (wasActive) {
        m_paper->openPdf(m_papers.at(newActive));
    }
}

void Tabs::activatePaper(int idx)
{
    if (idx < 0 || idx >= m_papers.size()) return;
    if (idx == m_activeIndex) return;
    m_activeIndex = idx;
    emit activeIndexChanged();
    persist();
    m_paper->openPdf(m_papers.at(idx));
}

bool Tabs::restoreSession()
{
    const QStringList saved = m_qs.value(kKeyUrls).toStringList();
    if (saved.isEmpty()) return false;

    int savedActiveIdx = m_qs.value(kKeyActive, 0).toInt();
    if (savedActiveIdx < 0 || savedActiveIdx >= saved.size())
        savedActiveIdx = 0;
    // Track the active URL (not the index) so it survives entries
    // being filtered out by the deleted-file check below.
    const QString activeUrlStr = saved.at(savedActiveIdx);

    QVector<QUrl> urls;
    urls.reserve(saved.size());
    int newActive = -1;
    for (const QString &s : saved) {
        const QUrl u(s);
        // Drop entries whose local file has been moved/deleted since
        // the last session — better than a broken tab that errors on
        // every click.
        if (u.isLocalFile() && !QFileInfo::exists(u.toLocalFile()))
            continue;
        if (s == activeUrlStr)
            newActive = urls.size();
        urls.append(u);
    }
    if (urls.isEmpty()) return false;
    if (newActive < 0)
        newActive = 0;  // active was filtered out — fall back to first.

    m_papers = urls;
    m_activeIndex = newActive;

    emit tabsChanged();
    emit activeIndexChanged();
    persist();
    m_paper->openPdf(m_papers.at(newActive));
    return true;
}

void Tabs::onPaperSourceChanged()
{
    if (!m_paper) return;
    const QUrl u = m_paper->pdfSource();

    if (u.isEmpty()) {
        // Controller was cleared from outside Tabs (e.g., paper failed
        // to reload). Don't touch the tab list — the user might still
        // want the entry to be retry-able from the bar. Just drop the
        // active highlight if the cleared paper was the active one.
        if (m_activeIndex != -1) {
            m_activeIndex = -1;
            emit activeIndexChanged();
            persist();
        }
        return;
    }

    int idx = indexOf(u);
    if (idx < 0) {
        m_papers.append(u);
        idx = m_papers.size() - 1;
        emit tabsChanged();
    }
    if (m_activeIndex != idx) {
        m_activeIndex = idx;
        emit activeIndexChanged();
    }
    persist();
}

void Tabs::persist()
{
    QStringList ss;
    ss.reserve(m_papers.size());
    for (const QUrl &u : m_papers)
        ss.append(u.toString());
    m_qs.setValue(kKeyUrls, ss);
    m_qs.setValue(kKeyActive, m_activeIndex);
    m_qs.sync();
}
