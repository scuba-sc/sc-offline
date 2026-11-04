# Menu worker ownership checkpoint

Local implementation based on published product-lifetime scaffold
`d0a86c088d2eb06971b6d8d8967a1762c88764b2`. Native activation remains
unconditionally refused. This change is not exercised by the current product
startup. **No builds, tests or game execution were run for this change.**

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
worker's stop flag. It never waits or joins. `StopHostKit` calls it before the
shell's core-stop call. The menu thread cancels a frame not yet taken and keeps
pumping sent window messages while a game-thread frame is building. Once every
accepted frame has returned, it hides/destroys its own window and exits.
If a callback or graphics call hangs, the worker can remain pending; it is not
terminated, detached, or declared drained.

After observing core `Stopped` or `Failed`, `RunMainThreadService` unhooks its
message hook and calls `Menu_Drain(1000)` on `ModThread`. Drain closes admission
even for a core-owned terminal path that did not call shell `StopHostKit`.
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

## Exact core dependency for the next step

No core header, ABI, submodule pin, or activation policy changes in this slice.
The pinned interfaces remain `sco::OwnedShutdown::{try_read,start_worker}` and
`sco::game::host::{HostCapsule,Options,PrepareDetailed,StopDetailed,HostReport}`.
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
   runtime callout. Existing shell stop and terminal polling cover this menu
   slice, not the native entry barrier.
3. Enroll menu callbacks in the supported `OwnedShutdown::Read` domain for
   their whole accepted use. Enroll the actual worker with the supported
   worker owner (port to `start_worker`, or use a reviewed native-handle
   integration if core provides one). Do not substitute a synthetic worker,
   a no-op deleter, or a boolean drain callback for the real thread.
4. Preserve a control-thread phase that can join after game-thread callouts
   return, and retain the full dependency graph on timeout, fault, or uncertain
   shutdown. Joining a menu frame from its own game-thread callback deadlocks.
5. Aggregate reports conservatively: menu `Joined` cannot lower core
   `DependencyDisposition::ProcessRetained`, and core Stop success cannot
   authorize releasing product hooks, log sinks, crash handlers, image caches,
   IIM handoffs, plugin callbacks or renderer/client backing.

The broader inventory remains in
[`2026-11-03-product-lifecycle-closure.md`](2026-11-03-product-lifecycle-closure.md).
No spawner tick, picker behavior, plugin activation or other feature is added.
The bootstrap `ModThread` handle still has its earlier ownership gap, and the
message hook still lacks a complete entry/in-flight barrier. The menu's local
census does not close either obligation.

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
source/scan checks; build Windows/MSVC in both host-menu configurations. Add a
Windows synthetic harness around the actual worker with controlled startup,
frame completion, window creation and wait failures. Cover stop overlapping
start; duplicate start; wrong-thread and concurrent drain; failed creation;
timeout followed by completion; event wake after `Finish`; handle close failure;
and proof that no wait/join occurs inside `DllMain`. A signalled worker handle
does not prove healthy renderer initialization.

Only after the coordinated whole-product ownership contract is implemented and
reviewed should native/game acceptance be considered. This local callback census
does not exclude game detours, stop the engine, or make activation safe.
