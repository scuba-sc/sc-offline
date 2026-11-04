# Menu worker ownership checkpoint

Local implementation based on published product-lifetime scaffold
`d0a86c088d2eb06971b6d8d8967a1762c88764b2`. Native activation remains
unconditionally refused. This controller/core-owner follow-up is not exercised
by product startup. **No builds, tests or game execution were run for these
changes.**

## Actual product integration

`src/menu.cpp` now keeps the actual `MenuThread` handle instead of closing it
immediately. `Menu_Start` is one-shot and serialized with drain by a product
control lock. It acquires a real DLL reference with `GetModuleHandleExW` before
publishing the worker or window callback. That reference is intentionally
retained through process exit, including creation failure after acquisition.
There is no destructor that joins or releases this reference in `DllMain`.
Every worker return, including initialization failure, closes frame admission
and prevents window teardown messages from entering the ImGui backend.

`src/menu_callback_admission.h` supplies the admission counter used directly
by `Menu_GameThreadFrame`. The lease covers the entire callback, including its
final `SetEvent`. Observing a frame state of `Built` alone is insufficient to
close the event: the callback can still be about to signal it. Admission closes
permanently and atomically with respect to entering callbacks.

`Menu_RequestStop` in `src/menu_worker.h` closes frame admission and sets the
worker's stop flag. It never waits or joins. `RequestLifecycleStop` closes it
before asking the core owner to stop. The menu thread cancels a frame not yet taken and keeps
pumping sent window messages while a game-thread frame is building. Once every
accepted frame has returned, it hides/destroys its own window and exits.
If a callback or graphics call hangs, the worker can remain pending; it is not
terminated, detached, or declared drained.

`GetMsgProc` takes a separate lease through `CallNextHookEx`. Its first product
operation races atomically with `CallbackAdmission::Close`: if `Enter` wins, it
increments the count and the controller waits for that callback to leave before
draining the core owner; if `Close` wins, the callback only calls
`CallNextHookEx` and cannot reach core, menu, or other product callbacks. The
controller cannot observe a callback that entered the OS hook but has not yet
executed `Enter`. Therefore it deliberately does not call
`UnhookWindowsHookEx`: the hook remains registered, its handle stays stable,
and the module/backing remain process-retained. Later rejected callbacks
continue to forward until process exit. This closes product access after the
admission boundary; it does not prove that every hook invocation returned or
authorize unload.

The stop request signals `g_controlWakeEvent`, so `ModThread` wakes without
waiting for core to reach a terminal phase. Terminal phase polling remains a
fallback. The controller waits for admitted hook callbacks before draining the
shared core owner. It then calls `Menu_Drain(1000)` separately.
The menu drain closes its own admission even for a core-owned terminal path.
Only the original control thread may wait; calls from the game/menu thread or
another controller are refused. Concurrent control calls return Busy without
waiting. A start already reserved while stop arrives remains covered by that
control lock, so drain cannot report completion before its HANDLE is published.
The wait budget is capped at 60 seconds; the product call uses one second.
Scheduling and OS calls do not provide a hard wall-clock completion guarantee.

After successful join and callback drain, only the worker and frame-event
handles are closed. Timeout/wait failure preserves them for a later same-thread
drain attempt. `MenuDrainReport` reports worker observation, retained backing,
and control result separately. Even `Joined` + `Complete` means
`ProcessRetained` after module acquisition. `NotAcquired` refers only to this
worker owner's DLL reference; other product callbacks may already be exposed.
A Busy report uses `UnknownRetainRequired` because it cannot inspect ownership.
No result means successful host retirement, permitted restart, safe DLL unload
or release of renderer state.
Handle-close failure reports `WaitFailed` and preserves the unclosed handle.
The product currently makes one drain attempt, then remains in its existing
resident control-thread loop; it does not automatically retry/reap a timeout.

The ImGui context, D3D objects, window-class callback registration, optional
menu-client state, and module reference remain retained. Their other consumers
have not been closed under a complete product/core contract. The registered
window callback remains backed by the retained DLL. A malformed/crashed native
process is outside this local census; no authenticity or recovery guarantee is
made for trusted in-process plugins.

## ASOP terminal callback ordering

`StopHostKit` calls `offline::asop::Stop()` synchronously after requesting
product/core stop. In the inspected sibling core source at
`a9a99f2d831d40f64e38ced8bb59f1575491b87b`, `asop_host::Stop` closes ASOP
admission, updates status, disables fleet admission, shuts down the handoff
queue, and clears feature capabilities. Handoff shutdown wakes untaken calls;
it does not join taken callbacks or engine workers. The implementation also
takes internal mutex/SRW locks without a deadline. The public contract says
this is terminal closure and requires callers to join engine workers before
unload. It does not expose a nonblocking request paired with an owner drain.

The registered ASOP `game.exit` callback is ordered before core provider
withdrawal. Moving ASOP closure to `ModThread` or making it asynchronous would
need a core API that preserves this ordering and reports completion. That API
is absent from the selected source. Keep the current synchronous closure,
describe its lock-wait limitation, and retain all backing. Do not count it as a
drain or safe-unload proof.

## Core owner integration and current dependency

When the selected core headers declare `OwnedShutdown::request_stop()` and
`host::RequestStopDetailed()`, `CMakeLists.txt` defines
`SCO_CORE_NON_DRAINING_STOP_API`. The product root creates one shared owner and
pins the product module before publishing the core logger. `AttachShutdownOwner`
puts that same object in `Options`, holds a real module reference for resolver
code, and pins the executable module containing the supplied image. The
admitted `OnMainThreadTick` path holds a `Read` from that exact owner.
`offline_host::RequestStop` closes its admission directly, then calls
`RequestStopDetailed` for the core report. `DrainRequestedStop` calls
`drain_until` on that same object from `ModThread` after callback return.
Every drain result keeps the root retained. The menu report remains distinct.

The named core commits are not present in this product checkout: the submodule
is `6106fa3639cbcc28f78a1ded8de2707de019f4d3`, and the sibling
`/workspace/sco-core` checkout is `a9a99f2d831d40f64e38ced8bb59f1575491b87b`.
The supplied newer core heads were not imported. With the current pin, header
detection disables this path, `AttachShutdownOwner` refuses, and core prepare,
start and tick stay unavailable. Native activation remains false. Header-name
detection does not validate the newer report's drain-state semantics; the
product must be reviewed against the exact core source before enabling it.

## Remaining core/product closure

No core source, ABI, submodule pin, or activation policy changes in this slice.
The current pin does not expose the non-draining stop pair. On compatible
source, the product uses `sco::OwnedShutdown::{try_read,request_stop,drain_until}`
and `sco::game::host::RequestStopDetailed`.
The pinned `Options::shutdownOwner` contract explicitly excludes the product's
plugins, Lua, DataCore, native services and host-menu/service adapters. A local
menu report must not be passed off as proof satisfying that contract.

Coordinate the following with the core foundation worker rather than adding a
second host ABI here:

1. An adopted product root must own the image/resolver, module references and
   every accepted callback context before core copies or publishes Options.
   The menu's module reference owns only this DLL; it does not own plugin DLLs,
   engine mappings or core/provider lifetimes.
2. Core terminal shutdown must close product callback admission before
   retiring providers, including native quit, admission loss and startup
   failure. The notification must not join on the game thread or inside a
   runtime callout. The shell event covers this menu slice, not the native
   entry barrier.
3. Keep the menu worker's separate drain report distinct from the core owner
   drain. If the whole-product contract requires one owner to join it, use a
   reviewed native-handle integration or port the actual loop to
   `OwnedShutdown::start_worker`; do not substitute a synthetic worker, a
   no-op deleter, or a boolean drain callback for the real thread.
4. Preserve a control-thread phase that can join after game-thread callouts
   return, and retain the full dependency graph on timeout, fault, or uncertain
   shutdown. Joining a menu frame from its own game-thread callback deadlocks.
5. Aggregate reports conservatively: menu `Joined` cannot lower core
   `DependencyDisposition::ProcessRetained`, and core Stop success cannot
   authorize releasing product hooks, log sinks, crash handlers, image caches,
   IIM handoffs, plugin callbacks or renderer/client backing.

The newer core report's drain state and read-only owner snapshot have not yet
been integrated against their source because that commit is not present here.
Treat `Drained` as historical cooperative evidence only. If the control path
later observes a completed owner drain, reuse it; do not begin a second
one-shot drain attempt.

The broader inventory remains in
[`2026-11-03-product-lifecycle-closure.md`](2026-11-03-product-lifecycle-closure.md).
No spawner tick, picker behavior, plugin activation or other feature is added.
The bootstrap `ModThread` handle still has its earlier ownership gap. Hook
admission now prevents callbacks that lose the close race from entering product
code, while the registered hook and its backing remain retained because the
entry-to-admission interval cannot be counted. The menu's local census does not
close either obligation.

## Deferred acceptance

Prepared `tools/menu_worker_lifetime_test.cpp` uses the same admission class as
the actual menu and the existing frame protocol. Cases cover stop before start,
idempotent close, an unstarted request, a built frame whose callback has not
returned, a held callback, and admission/close races. It has **not been compiled
or executed**. A future standalone invocation is:

```sh
c++ -std=c++20 -pthread -Isrc tools/menu_worker_lifetime_test.cpp -o /tmp/menu-worker-lifetime-test
/tmp/menu-worker-lifetime-test
```

Before acceptance, run existing native-preflight, menu-frame, host-menu and
source/scan checks; build Windows/MSVC against the current pin and the exact
newer core source. Add a
Windows synthetic harness around the actual worker with controlled startup,
frame completion, window creation and wait failures. Cover stop overlapping
start; duplicate start; wrong-thread and concurrent drain; failed creation;
timeout followed by completion; event wake after `Finish`; handle close failure;
message-hook wake after callback return; owner close and same-owner control
thread drain; and proof that no wait/join occurs inside `DllMain`. A signalled worker handle
does not prove healthy renderer initialization.

Only after the coordinated whole-product ownership contract is implemented and
reviewed should native/game acceptance be considered. This local callback census
does not exclude game detours, stop the engine, or make activation safe.
