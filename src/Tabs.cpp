#include "Tabs.h"

#include "PaperController.h"

#include <QFileInfo>
#include <QRegularExpression>

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
    if (m_titleFor) {
        const QString title = m_titleFor(u).trimmed();
        if (!title.isEmpty())
            return title;
    }
    const QString file = u.isLocalFile() ? QFileInfo(u.toLocalFile()).fileName()
                                         : u.fileName();
    // A paper opened out of a project plays from the content-addressed cache,
    // where its file is named after its checksum. The library is what turns
    // that back into a title, and it cannot always answer -- no project is
    // selected, nobody has signed in yet, the sync has not brought the item
    // down. Sixty-four hex characters in the tab and in the window caption is
    // not a name; it is the app admitting it does not know, in the least
    // useful way available. Say that instead.
    static const QRegularExpression checksum(
        QStringLiteral("^[0-9a-f]{32,}(\\.[A-Za-z0-9]+)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (checksum.match(QFileInfo(file).completeBaseName().isEmpty()
                           ? file : QFileInfo(file).completeBaseName())
            .hasMatch())
        return tr("Untitled paper");
    return file;
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
    const QUrl closed = m_papers.at(idx);

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
    emit paperClosed(closed);
}

void Tabs::closeOthers(int idx)
{
    if (idx < 0 || idx >= m_papers.size()) return;
    if (m_papers.size() == 1) return;   // nothing else to close

    const QUrl keep = m_papers.at(idx);
    const bool keepWasActive = (idx == m_activeIndex);
    QVector<QUrl> closed;
    for (const QUrl &u : std::as_const(m_papers)) {
        if (u != keep)
            closed.append(u);
    }

    m_papers = { keep };
    const bool activeChanged = (m_activeIndex != 0);
    m_activeIndex = 0;
    emit tabsChanged();
    if (activeChanged) emit activeIndexChanged();
    persist();

    // Same ordering rule as closePaper(): settle the tab list first, then
    // hand the controller its new paper. Nothing to load when the kept
    // tab was already the one on screen.
    if (!keepWasActive)
        m_paper->openPdf(keep);
    for (const QUrl &u : std::as_const(closed))
        emit paperClosed(u);
}

void Tabs::closeAll()
{
    if (m_papers.isEmpty()) return;

    const QVector<QUrl> closed = m_papers;
    m_papers.clear();
    const bool activeChanged = (m_activeIndex != -1);
    m_activeIndex = -1;
    emit tabsChanged();
    if (activeChanged) emit activeIndexChanged();
    persist();
    m_paper->clear();
    for (const QUrl &u : std::as_const(closed))
        emit paperClosed(u);
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
