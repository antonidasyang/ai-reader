#include "Stall.h"

#include <QElapsedTimer>

namespace {
// A bare pointer to a string literal, or into a Mark's own copy: GUI thread
// only, which is the whole point -- the watchdog reading it lives there too.
const char *g_phase = "idle";

// Since the process started. One clock for every Mark, so their durations
// are comparable without each carrying a timer of its own.
QElapsedTimer &markClock()
{
    static QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t;
}

QByteArray g_longestName;
qint64 g_longestMs = 0;
} // namespace

namespace Stall {

void setPhase(const char *what) { g_phase = what ? what : "idle"; }
const char *phase() { return g_phase; }

QByteArray takeLongestMark(qint64 *ms)
{
    if (ms)
        *ms = g_longestMs;
    QByteArray out = g_longestName;
    g_longestName.clear();
    g_longestMs = 0;
    return out;
}

Mark::Mark(const char *what) { begin(what); }

Mark::Mark(const QByteArray &what) : m_own(what) { begin(m_own.constData()); }

void Mark::begin(const char *what)
{
    m_prev = phase();
    m_what = what;
    m_startedAt = markClock().elapsed();
    setPhase(what);
}

Mark::~Mark()
{
    const qint64 lived = markClock().elapsed() - m_startedAt;
    if (lived > g_longestMs) {
        g_longestMs = lived;
        g_longestName = QByteArray(m_what);
    }
    setPhase(m_prev);
}

} // namespace Stall
