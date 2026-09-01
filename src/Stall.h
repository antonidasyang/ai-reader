#pragma once

#include <QByteArray>

// What the GUI thread is doing right now, in a few words.
//
// The watchdog in main.cpp notices when the thread was blocked; on its own
// it cannot say what blocked it, and "the window froze for four seconds
// doing something" is not a bug report anyone can act on. Marking the
// handful of paths that are known to be expensive costs a pointer store
// each and turns that line into "...during: opening a paper".
//
// A phase that is never marked stays "idle", which is itself an answer: it
// means the time went somewhere no marker covers -- in QML, in the scene
// graph, or inside Qt.
namespace Stall {

void setPhase(const char *what);
const char *phase();

// The longest-lived Mark since this was last taken, and how long it lived.
// By the time an event returns, every Mark inside it has been destroyed and
// the current phase is whatever it was before -- so an event probe that
// reads phase() afterwards always says "idle". This is what it should read
// instead. Returns an empty name when nothing was marked.
QByteArray takeLongestMark(qint64 *ms);

// Scoped, and restores whatever was current before it -- so a marked path
// that calls another marked path reports the inner one while it is inside
// it, and the outer one again afterwards.
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
};

} // namespace Stall
