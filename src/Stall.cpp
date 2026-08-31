#include "Stall.h"

namespace {
// A bare pointer to a string literal: every caller passes one, so there is
// nothing to own and nothing to copy. GUI thread only, which is the whole
// point — the watchdog reading it lives there too.
const char *g_phase = "idle";
} // namespace

namespace Stall {

void setPhase(const char *what) { g_phase = what ? what : "idle"; }
const char *phase() { return g_phase; }

} // namespace Stall
