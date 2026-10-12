/* builtins_result_test: the result a built-in command answers once it has run (sco-core #12).
 *
 * A command's capability gate is the host's: without the capability the host answers
 * SCO_UNAVAILABLE before the command's fn runs, so a plugin or a menu can tell "teleport isn't on
 * this build" from "teleport ran and couldn't do its job". Inside the command every failure is the
 * second kind and answers SCO_FAILED (7), with the reply saying why.
 *
 * Built and run on Linux (the check job): the decision is a pure function, so the handlers don't
 * need the game. Run: tools/check.sh, or c++ -std=c++20 -I src/builtins -I external/sco-core/include tools/builtins_result_test.cpp
 */
#include "builtin_result.h"
#include <stdio.h>

static int g_failed = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

// The host's gate, as sco-core's RunNow does it: capability checked before fn, so fn only runs
// when the capability is there. What fn answers is the second half of the decision.
static sco_result Gate(bool capability, bool ok) {
    if (!capability) return SCO_UNAVAILABLE;         // never reaches the command
    return sco::builtins::Ran(ok);                   // the command's own answer
}

int main() {
    // The codes are what sco-core's ABI pin says they are (tests/abi_v1.c: SCO_FAILED == 7).
    CHECK(SCO_FAILED == 7);
    CHECK(SCO_UNAVAILABLE == 1);
    CHECK(SCO_FAILED != SCO_UNAVAILABLE);

    // teleport.save / teleport.go: the spot was saved or reached.
    CHECK(sco::builtins::Ran(true) == SCO_OK);
    // The command ran and couldn't: no saved spot yet, the spawn file it couldn't write.
    CHECK(sco::builtins::Ran(false) == SCO_FAILED);
    CHECK(sco::builtins::Ran(false) != SCO_UNAVAILABLE);
    // multiplayer.leave with no session: ran, nothing to leave.
    CHECK(Gate(true, false) == SCO_FAILED);

    // The capability isn't on this build: the host answers SCO_UNAVAILABLE and fn never runs,
    // whatever the command would have done.
    CHECK(Gate(false, true) == SCO_UNAVAILABLE);
    CHECK(Gate(false, false) == SCO_UNAVAILABLE);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("builtins_result: all checks passed\n");
    return 0;
}
