#pragma once
// The result a built-in command answers once it has run (sco-core #12: SCO_FAILED = 7, "the
// command or service ran, but failed; see reply or error").
//
// A command's capability gate is the host's: when the capability isn't on this game build the
// host answers SCO_UNAVAILABLE before the command's fn is called, so a plugin or a menu can tell
// "teleport isn't on this build" from "teleport ran and couldn't do its job". Inside the command
// every failure is the second kind, and the reply already says why. Nothing here answers
// SCO_UNAVAILABLE.
#include "sco_api.h"

namespace sco::builtins {

// SCO_OK when the command did its job (a spot saved, a session left), SCO_FAILED when it ran and
// couldn't: no saved spot yet, the spawn file it couldn't write, no session to leave.
inline sco_result Ran(bool ok) { return ok ? SCO_OK : SCO_FAILED; }

}  // namespace sco::builtins
