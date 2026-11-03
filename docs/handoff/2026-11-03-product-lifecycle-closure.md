# Product lifecycle closure inventory

Status: developer handoff with an attach-time log-sink deferral on the isolated
branch. Inventory review was against sc-offline base
`38066870c58f73ccd7751c4fd6d1a44936cdcb32` with sco-core
`6106fa3639cbcc28f78a1ded8de2707de019f4d3`. No product startup or activation
was enabled. No private signature patterns, binary bytes, addresses, or
reverse-engineered offsets are reproduced here.

## Current boundary

`offline_host::NativeActivationAvailable()` returns `false` unconditionally.
`InstallStartupGate`, `StartOffline`, `offline_host::{Prepare,Start,Tick}` and
the first work in `ModThread` all stop at that gate. Thus this inventory maps
compiled/intended product paths; it does not claim they ran in this build.
DLL attach still initializes product logging and starts `ModThread`, but the
false gate now returns before the core log sink is installed. If the gate ever
admits startup, `ForwardCoreLog` is registered only after anti-cheat refusal and
its explicit unload path, immediately before the first core host work. The
pinned logger stores an atomic callback pointer but offers no in-flight drain;
this ordering closes the known early unload path, not arbitrary external DLL
unload. See [`native_activation_policy.h`](../../src/native_activation_policy.h#L1),
[`dllmain.cpp`](../../src/dllmain.cpp#L279),
[`dllmain.cpp`](../../src/dllmain.cpp#L628),
[`dllmain.cpp`](../../src/dllmain.cpp#L672), and
[`sco_log_status.cpp`](../../external/sco-core/src/sco_log_status.cpp#L11).

The restricted core `HostCapsule` is not a drop-in owner for the current
product configuration. Its detailed contract requires external quit/DataCore
hooks and rejects DataCore, game services, plugins, scripts, host service/menu
adapters, capability callbacks, reserved chords, private sessions, and other
legacy consumers. It also requires real image, resolver, and shutdown owners.
The product currently builds an `Options` with a borrowed `ModuleImage()`
descriptor and does not assign those owner fields; the product wrapper calls
legacy `gh::Prepare`, not `PrepareDetailed`. Do not remove the activation gate
or pass this configuration to `HostCapsule` as if it were supported. See
[`host.h`](../../external/sco-core/include/sco/game/host.h#L33),
[`host.cpp`](../../external/sco-core/src/game/host.cpp#L88),
[`dllmain.cpp`](../../src/dllmain.cpp#L405), and
[`offline_host.cpp`](../../src/offline_host.cpp#L51).

## Consumer and backing inventory

“Last access” below means the last access that a future shutdown must prove;
where there is no close/drain path, it is deliberately described as
unbounded. Optional CMake compositions are OFF by default (`SCO_HOST_MENU_CLIENT`,
`SCO_ASOP_HOST_COMPOSITION`, `SCO_ASOP_SELECTION_COMPOSITION`).

| Surface | Backing and last access | Current shutdown evidence / gap |
|---|---|---|
| **DLL attach, log sink, control thread** | `DllMain` initializes the logging lock and starts `ModThread` (`src/dllmain.cpp:672-680`; `src/common.cpp:5-49`). The worker handle is discarded; with the current gate the thread logs and returns, but the later startup path can run indefinitely (`src/dllmain.cpp:628-670`). `ForwardCoreLog` is now installed by that worker only after the gate and anti-cheat checks, immediately before host startup (`src/dllmain.cpp:601`, `src/dllmain.cpp:660-663`). | The fail-closed path publishes no core callback. The existing explicit `FreeLibraryAndExitThread` path precedes sink registration. The sink setter is atomic but there is no in-flight callback drain; a future external unload path after registration still needs a core drain contract or a process-lifetime module owner. Logger file/console state has no product close path. |
| **Early entry gate** | Dormant because `InstallStartupGate` returns at the policy check (`src/dllmain.cpp:81-82`). If enabled, it retains the executable entry pointer and original bytes, allocates a nearby stub, creates an event, and calls `StartupGateWait` from the entry path (`src/dllmain.cpp:58-113`). | `StartupGateWait` restores the entry bytes before continuing, but no matching free of the stub or close of the event is present. Restoration is not evidence that the waiter has returned or that the module can unload. Add explicit one-shot resource retirement only with a proven post-wait point; otherwise retain through process exit. |
| **Admission inspector and host options** | `InspectOfflineEnvironment` reads `g_evidence` and returns OS admission data; the callback code and evidence/probe state live in the DLL (`src/dllmain.cpp:261-269`, `src/admission_evidence.h`, `src/admission_probe.cpp`). `PrepareHostKit` also supplies `SetFeatureCaps`, static Lua runtime `kLua`, static reserved chord strings, plugin/storage paths, and a static `sco::Image` descriptor (`src/dllmain.cpp:330-348`, `405-448`). | None of these host callbacks runs while the gate is false. No product owner currently pins the executable image, resolver implementation/state, inspector code, or callback context. The static image descriptor does not own mapped image bytes; core's copied signature snapshot is not a lease. Build an actual root before any options copy/exposure, then retain it on every uncertain/terminal result. |
| **Plugins, Lua, DataCore, and capability adapter** | Product setup sets `pluginsEnabled`, `scripts`, `setCapabilities`, `gameServices`, `dataCore`, and data roots; it registers the game Lua extension and provides the script runtime callbacks (`src/dllmain.cpp:405-442`). Built-ins and external plugin modules can add their own callbacks and workers. | No loader is reached under the current gate. Detailed `HostCapsule` explicitly rejects these fields. The product has no inventory/lease covering every plugin module, script extension, service table, callback context, or plugin-created worker. These require a compatible core contract and a plugin lifecycle that participates in the same owner/admission model; a non-null owner token is not enough. |
| **Game-thread message hook and tick callbacks** | `RunMainThreadService` finds a window, installs `GetMsgProc`, and optionally starts the menu (`src/dllmain.cpp:606-626`). `GetMsgProc` can call `StopHostKit` on `WM_QUIT` or invoke `OnMainThreadTick`, which calls core tick, hangar/ASOP processing, menu frame, hotkeys, and legacy feature ticks (`src/dllmain.cpp:473-585`). | No hook is installed today. Future `UnhookWindowsHookEx` is attempted only after a terminal core phase; its own comment preserves the handle for in-flight callbacks but does not wait for them. `StopHostKit` does not unregister all feature hooks or prove every callback returned. Add callback-entry exclusion and an in-flight drain before releasing any callback code or contexts. |
| **Runtime event subscriptions** | Successful `StartHostKit` subscribes static callbacks `OnContractActorDied` and, under the ASOP composition flag, `StopAsopBeforeProviders`, using static owner tokens (`src/dllmain.cpp:487-529`). They can access contract/ASOP state and callback code in the DLL. | These subscriptions are not reached today, and product source has no explicit `sco::Unsubscribe` call. The runtime contract says unsubscribe prevents later dispatch; when called off the game thread, the caller must also arrange a game-thread task after unsubscribe before freeing context (`external/sco-core/include/sco/runtime.h:168-181`). Tie unsubscription and in-flight dispatch completion to the host shutdown contract; do not infer it from a Stop result. |
| **Native patches and detours** | `ApplyOfflinePatches` obtains borrowed `.text`/`.rdata` section views and writes product patches; statuses retain target pointers (`src/common.cpp:51-69`, `src/patches.cpp:324-345`). Generic and feature detours use `sco::hook::InstallDetour`, with original/trampoline pointers in static globals (`src/hooks.cpp:17-25`, `237-273`; feature installs in `src/contracts.cpp`, `src/services.cpp`, and `src/fleet.cpp`). Fleet also has a direct code patch and near stub (`src/fleet.cpp:1377-1400`). | All installation is behind `StartOffline`, so none is active today. No product uninstall/restore call was found. Detoured functions can enter DLL code at any later game call; feature hooks call cached original pointers. `g_hooksInstalled` is only a boolean, not a ledger or owner. Either add a reviewed reverse-install protocol plus native entry/in-flight exclusion, or keep all detour code, trampolines, backing, and module resident until process exit. |
| **Resolved engine addresses and feature state** | `g_text`/`g_rdata` point into the executable mapping. `StartOffline` resolves teleport, spawn, NPC, legacy placement, CVars, missions, contracts, and (unless ASOP host composition is selected) fleet and hangars (`src/dllmain.cpp:289-303`). These modules retain raw function pointers, slots, and object/state pointers in globals; see resolver entrypoints in `src/teleport.cpp:14`, `src/spawner.cpp:95`, `src/npc.cpp:30`, `src/build.cpp:24`, `src/cvars.cpp:103`, `src/missions.cpp:208`, `src/contracts.cpp:2141`, `src/fleet.cpp:1445`, and `src/services.cpp:682`. | Resolver entrypoints are gated and have no common cache-clear routine. Main-thread tick stops when `offline_host::Running()` is false, but patched game entrypoints can still invoke detours independently. A signature report/snapshot only copies metadata. Track every cached address and its last caller under the same native-reader protocol; clear it only after the corresponding hook/readers are demonstrably drained. |
| **Hangar/IIM continuation handoff** | Hangar hooks keep original function pointers and engine pointers in `src/services.cpp`. A loading-thread callback can put a pointer to its stack `IimCall` into `g_iimPending`, then wait while the game-thread `ProcessHangars` takes and runs it (`src/services.cpp:261-345`, `739-750`). The call carries borrowed continuation arguments and an event handle. | The handoff timeout only permits fallback before the main thread takes the call; after take, the loader thread waits indefinitely for completion. There is no stop-time queue flush or census of these game-owned worker threads. Drain both queued and taken calls and their producers before stopping the message service or releasing backing. |
| **Menu, widget client, and renderer** | `Menu_Start` discards the created thread handle; `MenuThread` runs an unbounded loop and owns the window procedure, window, D3D COM objects, ImGui context, frame event and frame protocol (`src/menu.cpp:125-165`, `658-665`, `728-755`, `808-919`). Game-thread frame construction invokes UI/plugin draw callbacks. The optional host-menu client is a static `g_widgetMenu`; its descriptor points to `Load`, `Frame`, and `Stop`, and widget event cookies retain a `Client*` (`src/menu.cpp:22-28`, `src/menu_widgets.h:15-36`, `src/menu_widgets.cpp:22-89`). | Menu options are OFF by default and no menu starts today. There is no `Menu_Stop`, loop exit, worker join, window/device destruction, or event close in this path. Host-menu `Stop` only marks the client inactive; callback/cookie/resource completion depends on the host/widgets contract. Add cancellation, frame/callback closure, join, and resource destruction before considering release. |
| **Spawner input worker** | `PressFlightReadyKey` creates a thread that sends a key-down, sleeps briefly, then sends key-up; it closes the thread handle immediately (`src/spawner.cpp:1235-1248`). The callback code and a possible pending input effect live outside the game-thread tick. | No owned handle, join, cancellation, or shutdown wait exists. Include this worker in the census and ensure any key-up completes before closure; do not treat its short duration as a join proof. |
| **Crash filter** | `InstallCrashLine` installs `CrashFilter` as the process top-level exception callback and stores the previous filter (`src/common.cpp:104-164`). | Installed only if the gated main-thread service runs. There is no restoration call. A process-wide callback can occur after ordinary host stop; restore it only after proving no invocation is active, or retain the DLL until process exit. |
| **Optional ASOP service adapters** | `SCO_ASOP_SELECTION_COMPOSITION` supplies `StartSelection`/`TickSelection`/`StopSelection` with static `g_selectionFile` context (`src/offline_host.cpp:9-44`, `51-64`; `src/asop_selection_file.h:6-18`). File handles are scoped per operation, but the callback context and configured path must live through `selection::Stop` and all accepted work. `SCO_ASOP_HOST_COMPOSITION` parses config and passes it to `sco::game::asop_host::Start`; the provider copies the catalogue before returning (`src/asop_shell.cpp:28-54`, `src/asop_platform.cpp:28-49`). | Both CMake options default OFF (`CMakeLists.txt:58-60`). The selection host service is rejected by detailed HostCapsule. The ASOP provider's engine hook ownership is in sco-core and is not evidence that the rest of product callbacks, features, or workers are owned. Preserve the provider's stop/retention contract and add both adapters only under a compatible whole-host contract. |

The current `dinput8` target compiles `services.cpp` and `fleet.cpp` when the
default ASOP-host option is OFF, plus the feature modules listed in
`CMakeLists.txt:117-137`. It does not compile `src/quantum.cpp`, `src/travel.cpp`,
or the built-in plugin implementation files; only `src/builtins/build_ui.cpp`
from that subtree is listed. `ResolveQuantumApi` and the travel helpers in those
unlisted files are not part of this DLL's current callback/address closure.

## Existing evidence and its limit

`tools/offline-host` already exercises the pinned core `HostCapsule` with
synthetic image bytes, resolver tokens, owner/read leases, failure injection,
reports and workers (`tools/offline-host/CMakeLists.txt:15-75`; details in
[`README.md`](../../tools/offline-host/README.md#L1)). Those fixtures do not
wire product callbacks or plugins, map the real executable, enumerate actual
game readers/workers, establish a native execution barrier, test Windows/MSVC
failure behavior, or prove successful game-host retirement. The refusal and
source-wiring checks establish fail-closed behavior only. No native or game
proof was produced by this inventory. The log-sink ordering is guarded by the
source-level `test_native_preflight_wiring.py`; it is not a runtime callback
drain test.

## Minimal ordered implementation plan

1. Keep `NativeActivationAvailable()` false. Resolve the architecture gap with
   sco-core first: either extend its reviewed ownership contract for product
   plugins/Lua/DataCore/services/adapters, or split those consumers behind a
   host path whose callback and shutdown contract is explicit. Do not weaken
   `DetailedConfiguration` by assertion or no-op owner.
2. Produce a per-feature inventory/owner graph. Preserve the log-sink deferral:
   current fail-closed startup publishes no callback, and a future sink remains
   borrowed code with no logger drain API. Root every dependency before Prepare,
   options copy, callback registration, or native write. Include the DLL/code
   module, image and resolver backing, Platform callback code/context, plugin
   modules, cached engine targets, hook allocations, event subscriptions, menu
   resources, IIM calls and every worker.
3. Add one real entry-admission protocol used by every detour, callback,
   provider operation and worker. Stop must close new entry, establish the
   game-thread/native quiescent point, drain existing leases and copied output,
   resolve IIM handoffs, unsubscribe and join tracked workers. The current
   `OwnedShutdown` tests do not supply this native census or barrier.
4. Choose a truthful terminal policy. Reverse hooks and release only after
   callback entry and instruction execution are excluded and all dependencies
   have drained. Otherwise pin/retain the complete dependency closure until
   process exit; the current `offline_host::retained` atomic is only a restart
   latch, not such an owner (`src/offline_host.cpp:49-85`).
5. Once the product actually calls the supported detailed host path, preserve
   its real `HostReport` through control transitions and persist/read it as an
   observation only. `NeverExposed` and `ProcessRetained` are ownership facts,
   not permission to unload or restart (`external/sco-core/include/sco/game/host.h:67-108`).
6. Add product-seam synthetic failure tests for partial owner adoption,
   callback-vs-drain races, every owned worker class, IIM taken/untaken shutdown,
   event unsubscribe races, and report mapping. Then do separate Windows/MSVC
   native barrier and exact-build acceptance. Existing core synthetic tests do
   not substitute for either stage.
