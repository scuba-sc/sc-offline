# Teleport / quick-save — G2 REFUSE consumer

`teleport.save` (F7) and `teleport.go` (F8) always return `SCO_UNAVAILABLE`. No relocation request, private pointer write, auto-placement or refinement mutation is attempted. This is intentional owner policy, not a signature readiness claim. `teleport.inspect` reads a saved exact/partial restoration candidate through G3; root-only and stale matches refuse. `teleport.status` reports retained state/refusal.

Retains owner ID/database `teleport`, key `spot`, import marker `spawn.txt.time` and original `spot x y z zone` syntax. Existing `spawn.txt` imports transactionally once per changed Windows FILETIME. `spawn_file` overrides its path; empty inherits launcher `SC_OFFLINE_SPAWN_FILE`, otherwise `data_root/spawn.txt`. Quick-save remains refused; read/import machinery cannot mutate the player.

## Build and install
1. Obtain/extract a core+game or combined SDK containing `sc_game_spatial.h`; the pinned published `sdk-v2.2.0` contains it (`sdk-v2.0.0` did not).
2. Configure `cmake -S plugins/teleport -B build-teleport -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/include`, then build `sc-offline-teleport`.
3. Install the teleport component into the product root (`cmake --install build-teleport --prefix /path/to/product --component teleport`). The resulting `data/plugins/teleport/` contains its native entry and manifest; enable through ordinary plugin-folder semantics. Built-in IDs/fallbacks are removed, so `plugins=off` really disables these features.

Native modules are trusted code. GPL-3.0-only. Calls run synchronously on the game thread; copied metadata is never treated as valid across commands/ticks. Optional `sco.widgets` panels avoid host/ImGui headers; absent renderer leaves command-only operation. Missing/readiness/expired tables refuse instead of opening private engine paths. Unload removes panels/hotkeys and retains databases. No unmanaged worker or tick mutation exists.

**Evidence: macOS AppleClang configure/compile/link only. Tests NOT RUN (owner speed order); Windows/MSVC, UI rendering, storage import-twice, dynamic load/reload, sanitizers/checker and game behavior NOT VERIFIED.**
