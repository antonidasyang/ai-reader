#include "Stall.h"

#include <QElapsedTimer>
#include <QHash>

#include <algorithm>

namespace {
// A bare pointer to a string literal, or into a Mark's own copy: GUI thread
// only, which is the whole point -- the probe reading it lives there too.
const char *g_phase = "idle";

// One clock for every Mark, so their durations are comparable without each
// carrying a timer of its own.
QElapsedTimer &markClock()
{
    static QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t;
}

QHash<QByteArray, qint64> g_totals;
Stall::Mark *g_innermost = nullptr;
} // namespace

namespace Stall {

void setPhase(const char *what) { g_phase = what ? what : "idle"; }
const char *phase() { return g_phase; }

QList<QPair<QByteArray, qint64>> takeBreakdown()
{
    QList<QPair<QByteArray, qint64>> out;
    out.reserve(g_totals.size());
    for (auto it = g_totals.constBegin(); it != g_totals.constEnd(); ++it)
        out.append({it.key(), it.value()});
    g_totals.clear();
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    return out;
}

void resetBreakdown() { g_totals.clear(); }

Mark::Mark(const char *what) { begin(what); }

Mark::Mark(const QByteArray &what) : m_own(what) { begin(m_own.constData()); }

void Mark::begin(const char *what)
{
    m_prev = phase();
    m_what = what;
    m_startedAt = markClock().elapsed();
    m_parent = g_innermost;
    g_innermost = this;
    setPhase(what);
}

Mark::~Mark()
{
    const qint64 lived = markClock().elapsed() - m_startedAt;
    // Exclusive: whatever the marks nested inside this one already claimed
    // is theirs, not ours.
    const qint64 mine = qMax(qint64(0), lived - m_childMs);
    g_totals[QByteArray(m_what)] += mine;
    if (m_parent)
        m_parent->m_childMs += lived;
    g_innermost = m_parent;
    setPhase(m_prev);
}

} // namespace Stall
