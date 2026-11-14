# Native write census (static)

`tools/native_write_census.json` classifies every place in `src/*.cpp` and `src/*.h` that writes
into the game process or registers a callback with the OS or the Core SDK. `tools/test_native_write_census.py`
scans the sources and fails when a write is added without a census entry, when an entry has no
matching write, or when a class, admission or required field is invalid. Entries match by file,
enclosing function and call label (with counts); there are no line numbers.

This is a static source census, not evidence of runtime behavior. It does not show that any site
runs, in which order, or that a hook works in the game. Native activation is available only on a Core with
the process-retained composition (retain until exit, stop is closure); the census does not
establish that it works.

## Classes

| Class | Meaning |
|---|---|
| `gate_code` | Multi-byte code write (detour, jump patch, byte/NOP patch) made on the startup worker (`ModThread` -> `StartOffline`) before `ReleaseStartupGate()` on the success path, while the game's main thread is parked at the entry gate. |
| `late_code` | Multi-byte code write made after the gate is released (a game-thread tick, a hook body, or lazily on first use). |
| `slot` | A single pointer-sized store into a vtable slot or object field, not code bytes, whenever it happens. |
| `per_request` | A write repeated per game call. |
| `os_hook` | OS-level registration (`SetWindowsHookExW`, `SetUnhandledExceptionFilter`). |
| `core_registration` | Registration with the Core SDK (`sco::Subscribe`, `sco::SetLogSink`, callbacks placed in `Platform` or `Options`). |
| `entry_gate` | The entry-point jump and its restore in `src/dllmain.cpp`. |
| `unreachable` | The write exists in source but no caller in `src/` reaches it; the entry's note gives the evidence, and the test rejects the class if a direct caller appears. |

## Counts

| Class | Sites |
|---|---|
| `gate_code` | 60 |
| `late_code` | 0 |
| `slot` | 7 |
| `per_request` | 2 |
| `os_hook` | 2 |
| `core_registration` | 7 |
| `entry_gate` | 3 |
| `unreachable` | 4 |
| total | 85 |

A table-driven site counts once: `InstallHooks` has one `Detour` call over the five-row `kHooks`
table (the census records and the test checks the row count), and two patch functions make one
write call inside a loop over several signature sites.

## late_code

None. Every code write (`InstallDetour` through `HookFunction`, `EnsureHook`, `EnsureHangarHook`,
`Detour`, and `WriteCode` on code bytes) is reached only from `StartOffline`, directly or through
`ApplyOfflinePatches`, `InstallHooks`, `ResolveContractsApi`, `ResolveFleetApi` and
`ResolveHangarsApi`. Each of those is called synchronously before `ReleaseStartupGate()`.

## per_request

- `src/fleet.cpp` `MarkRetrieved` (object field store): a pointer-sized vehicle handle written into
  the game's ship-list entry from `Hook_SetDataList` -> `MarkEntries`, on every refresh of the list. No code bytes.
- `src/services.cpp` `PrepareHub` (`SwapHubSlot`): repoints the services-hub object at a
  product-owned vtable copy from `RequestInstanceHook` and `PermissionsHook`. It writes again
  whenever the hub's vtable is not already the product's, and runs on every wrapped game call.

## slot

- `src/contracts.cpp` `HookPhaseEnds`, `HookHaulingEnds`, `HookAbandon`: product hooks stored into the
  offline mission service's vtable at the gate (8-byte stores through `WriteCode`).
- `src/contracts.cpp` `ResolveContractsApi`: `AcceptHook` stored into the accept slot at the gate.
- `src/patches.cpp` `PatchSocialGroupQueries`: a pointer stored into a product-owned cave that the
  patched sites read; not game memory. At the gate.
- `src/fleet.cpp` `InstallCallerLookup` (`SwapSlot`): the game object's caller-lookup vtable slot.
  Deferred until that object exists, so it first runs from a tick (`ProcessFleet`, under the owner read) after the gate. Once only.
- `src/missions.cpp` `AddOurScripts` (`WriteCode`): the game's XML system vtable slot. Lazy: first
  from `ProcessMissions` once the local player exists, or earlier from `StartMissionNearPlayer`
  (the contracts fight start, under `ProcessContracts`). Published once through the mission script route.

## Other timings worth knowing

- `os_hook`: `SetWindowsHookExW` and `SetUnhandledExceptionFilter` run on the startup worker in
  `RunMainThreadService`, after `ReleaseStartupGate()`, once the game window exists. Neither is removed.
- `core_registration`: `SetLogSink` is set in `ModThread` before `StartOffline`. The
  `Platform`/`Options` callbacks are set in `PrepareHostKit` and `offline_host::Prepare` at the gate.
  `sco::Subscribe` runs from `StartHostKit` on a tick (under the hook callback lease), after the host kit reaches Prepared.
- `entry_gate`: the entry jump is written in `DllMain` (under the loader lock) by
  `InstallStartupGate`; it is restored on the game thread once the gate is released.
- `unreachable`: the four detours in `HookBoostInput` (`src/quantum.cpp`). Nothing calls
  `HookBoostInput`, `ResolveQuantumApi` only logs, and `quantum.cpp` is not in the `dinput8` sources.

## Not covered

Writes made inside sco-core (for example the game pack's own hooks), subdirectories of `src/`,
writes into product-allocated buffers, game-object value writes that are not pointer-sized
(for example a drive or mission-settings field), and game functions called to mutate state are
outside this scan. The test header lists the scan's other limits.
