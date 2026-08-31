#pragma once

// What the GUI thread is doing right now, in a few words.
//
// The watchdog in main.cpp notices when the thread was blocked; on its own
// it cannot say what blocked it, and "the window froze for four seconds
// doing something" is not a bug report anyone can act on. Marking the
// handful of paths that are known to be expensive costs a pointer store
// each and turns that line into "...during: opening a paper".
//
// A phase that is never marked stays "idle", which is itself an answer: it
// means the time went somewhere in QML or the scene graph rather than in
// any of the C++ work below.
namespace Stall {

void setPhase(const char *what);
const char *phase();

// Scoped, and restores whatever was current before it — so a marked path
// that calls another marked path reports the inner one while it is inside
// it, and the outer one again afterwards.
class Mark
{
public:
    explicit Mark(const char *what) : m_prev(phase()) { setPhase(what); }
    ~Mark() { setPhase(m_prev); }
    Mark(const Mark &) = delete;
    Mark &operator=(const Mark &) = delete;

private:
    const char *m_prev;
};

} // namespace Stall
