#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>

// What the GUI thread is doing right now, in a few words -- and, once an
// event is over, where its time actually went.
//
// The watchdog and the event probe in main.cpp both notice when the thread
// was blocked; neither can say what blocked it. Marking the paths that are
// known to be expensive costs a clock read each and turns "the window froze
// for four seconds" into a breakdown.
namespace Stall {

void setPhase(const char *what);
const char *phase();

// Exclusive time per marked phase since this was last taken, biggest
// first. Exclusive: a marked path that calls another marked path is not
// charged for the inner one, so the numbers add up rather than nest. What
// they do not add up to is the whole event -- the difference is time no
// marker covers, and the probe reports that residual too, because it is the
// number that says "stop marking and go look at Qt".
QList<QPair<QByteArray, qint64>> takeBreakdown();

// Scoped, and restores whatever was current before it.
class Mark
{
public:
    explicit Mark(const char *what);
    // For a name that has to be built at run time (a request path, a file).
    // The copy lives as long as the Mark, which is what phase() points at.
    explicit Mark(const QByteArray &what);
    ~Mark();
    Mark(const Mark &) = delete;
    Mark &operator=(const Mark &) = delete;

private:
    void begin(const char *what);

    const char *m_prev = nullptr;
    const char *m_what = nullptr;
    QByteArray m_own;
    qint64 m_startedAt = 0;
    qint64 m_childMs = 0;      // time spent inside marks nested in this one
    Mark *m_parent = nullptr;
};

} // namespace Stall
