# Host composition and startup rules

This page is for host developers. It says how `dinput8.dll` starts sco-core's game-pack host, which lifetime rules it relies on, and what it refuses. None of it has been run in a real game session beyond the startup runs in [status.md](status.md).

Native startup works only with a Core that provides the process-retained composition. CMake probes the selected Core's `include/sco/game/host.h` for `Composition::ProcessRetained` and defines `SCO_CORE_PROCESS_RETAINED_COMPOSITION`. Together with the non-draining stop API (`SCO_CORE_NON_DRAINING_STOP_API`), that makes `NativeActivationAvailable()` true. The Core at the `external/sco-core` gitlink provides both, so startup is on. With an older Core (for example one chosen with `SCO_HOST_SOURCE_DIR`), configure prints that native startup stays refused, and the product refuses: no entry hook, no native Prepare or Start, no product patch, no plugin activation.

The product selects `Options::composition = ProcessRetained`, which runs the normal app runtime inside the game process. Three rules apply. Neither Core nor this product proves them.

- **Retention.** The product module and every dependency stay mapped until the process exits. The module is pinned (`GET_MODULE_HANDLE_EX_FLAG_PIN`), nothing in `src/` calls `FreeLibrary`, and a refused start returns from `ModThread` without unloading.
- **Install quiescence.** The shell supplies `platform.gameReadersQuiesced`. Core calls it immediately before each native mutation: the quit hook and pak hook in `Prepare`, and the game services in `Start`. The callback returns true in two cases only:
  1. The startup gate is installed and not yet released, so the executable's main thread is still parked at the entry point. This is real quiescence of the executable's code.
  2. The calling thread is the one on which the product opened the start window around its own `Start()` call (the message-hook thread). The first `[startup] 7/8` line in `mod.log` says whether it is also the gate thread. This case is not quiescence: other engine threads run while the game services install their hooks. It matches how the last shipped versions started their services. It is a known deviation, to be revisited after in-game evidence.
- **Stop is closure.** Admission closes and `game.exit` is dispatched once. Nothing is drained, unloaded or restarted. The wait for hook callbacks after a stop request is bounded (5 s), and the module stays retained whether or not they drained.

Not established: engine-object lifetime inside hooks, real hook behavior, reader or worker drain, unload safety, and any in-game behavior. Startup and shutdown write numbered `[startup]` and `[shutdown]` lines to `mod.log` for the first owner-run session.

## Host source

The default host builds from the exact `external/sco-core` gitlink, a development revision of the SDK 2.4.1 line. The published SDK source is a separate reference; see [SDK coordinates](sdk-candidate.md).

For local development, select an extracted `host-source-sc` kit with `-DSCO_HOST_SOURCE_DIR=<extracted-source-root>`. The override changes neither the committed pin nor the external plugin archive. A kit must provide `sco_game_host`; a missing target is a configure error.

## Strict owned host

The host separates registry preparation from activation. The strict owned host keeps its registry private. `AcquireHostSignatureSnapshot` reads the separately installed convenience registry and does not return the private one. Strict activation currently rejects the required native hooks, game services and legacy product adapters, because the product cannot supply the image and resolver ownership or the native reader and worker exclusion and drain that would change that. A copied snapshot does not lease image memory or exclude readers.

Under the strict composition, quit interception, early DataCore and plugin startup are therefore not active. The process-retained composition above is the one that enables them. The product still contains Windows proxy forwarding, read-only OS inspection, message, window, input and render policy, paths and configuration, launcher consent and restoration, and legacy engine helpers.

A failed or uncertain Stop latches restart refusal. The process, module and backing must stay live until exit. The latch does not own or drain them. Core keeps its owned closure on its terminal paths, but retention by legacy consumers is not established. A terminal startup or stop failure in Core also requires process exit. No callback counts as drain proof.

## What the host refuses

- OS module, process and service failures stay unknown. They never count as anti-cheat absence. Detection is read-only, and there is no anti-cheat manipulation or bypass.
- The shell collects Windows Firewall rules for the current executable and its TCP table through `admission_probe.cpp`. `admission_evidence.h` combines those readings with target-process and anti-cheat inspection. Failed or incomplete readings refuse admission. Launcher flags and patch success do not replace them. The collectors have not been checked against a real session.
- Early DataCore rejects built-in plugin inputs. The shell registers none. Features are separately built external folders, and legacy source files add no registrations or fallback ticks.
- `data/plugins/quantum_drive` is an ordinary data pack. Loaded alone does not show that a specific patch was applied. A consumer must read the published outcome before advertising `quantum.drive`.
- Relocation and quantum controls keep Core's admission, capability and backend checks. A successful source build does not show movement, effects or restoration in game.
- Loadout staging, contracts providers, optional bridges and multiplayer stay outside the host. Native services that are default off or unavailable stay so.

## Portable compile target

```sh
cmake -S tools/offline-host -B <outside-build-root> \
  -DSCO_HOST_SOURCE_DIR="$PWD/external/sco-core"
cmake --build <outside-build-root> --target sc_offline_host_composition
```

This builds the portable product seam and its dependencies, not the Windows DLL. Use the root MSVC build for the DLL and launchers ([build.md](build.md)). Game, network, launcher-restoration and renderer behavior need separate evidence.

A portable capsule-bound adapter is qualified separately, in [`tools/offline-host`](../tools/offline-host/README.md#capsule-bound-non-native-consumer). It is a restricted non-native consumer. The product's own Prepare path does not use it. The native rules above still apply.

## Inventory filter, after the original call

The inventory filter detour calls the original function synchronously with its original arguments and no product read lease. For the two results the product post-processes, it then takes the same owner read the message callback uses. A refused read returns the original result without touching the borrowed filter or registry. An accepted read stays held through the final property and diagnostic log use. Without the host composition, or without the owner API, the post-processing refuses.

This guard does not show that the engine's borrowed arguments stay valid after the original returns. It does not resolve a quit that re-enters while the same thread holds the lease. It does not count other native callbacks or workers, authorize native activation, or establish safe teardown.

## Mission-script hook

The mission-script route loads all selected files before it publishes an immutable, process-retained snapshot and tries to install the XML hook.

- A missing, empty or unreadable selected file refuses the custom hook and its registrations for that session. Restart to retry. The original XML loader stays as the fallback.
- The hook takes an owner read before it examines its published selection and keeps it until a replacement engine call returns.
- Installing the hook also needs an owner admission. If shutdown closes admission first, the hook is not written. An installation that was already admitted can still overlap closure and needs a real drain protocol.

These rules protect product-owned backing and count product uses. They do not show that the game has stopped using engine-owned objects, or that the hook write is atomic with respect to game threads. The direct quit route may forward a re-entrant quit while a read admission is still on the same stack. The lifetime of borrowed engine arguments and structured-exception behavior need build-specific evidence. The retain-until-exit rules above are the only lifetime claim.

The synthetic mission fixtures exercise publication and admission on both sides of the owner boundary. They do not show engine-object lifetime or safe native teardown.

## Startup gate

The entry-point startup gate is installed only when the DLL was loaded as a static import of the process executable and the executable's in-memory import directory names the module. The check is a non-null `lpvReserved` in `DLL_PROCESS_ATTACH`. The loader holds such a module before `DllMain` runs, so nothing DLL-resident is published while it could still be released. A dynamic load (injection, late `LoadLibrary`) is refused before any entry patch, event, thread or callback exists. The later worker pin stays as a second layer.

This is a lifetime rule only. It does not establish reader or worker exclusion or drain. The synthetic `module_lifetime` fixture ([README](../tools/offline-host/module_lifetime/README.md)) checks the OS behavior.
