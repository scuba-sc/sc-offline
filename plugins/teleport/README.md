# Teleport / quick-save and return

`teleport.save` (F7) is designed to capture where you are, read-only: the full pose and its observed zone ancestry through `game.spatial`. The external consumer uses sco-core's public **`game.relocation` 1.0** (`sc_relocation.h`) for return moves, but the current offline product refuses native host activation pending real reader/worker exclusion and drain evidence. Therefore the product does not currently load this plugin or perform moves in game. The consumer's synthetic SDK workflows remain useful for contract checks and do not change that gate. `teleport.inspect` reads the saved exact/partial restoration candidate without moving; root-only and stale matches refuse. `teleport.status` reports the saved state, the move switch, whether `game.relocation` is on and any move in progress when the module is run in a suitable test host.

## How a move works

1. The plugin's `relocation` setting must be on, then the capability `game.relocation` is checked fresh, before anything else.
2. The saved spot is resolved to a live zone id and a pose local to that zone; unloaded, expired or root-only matches refuse.
3. One request (zone id, local position, rotation, 30 s timeout) goes to `game.relocation`. One move at a time; the ticket is never persisted.
4. The plugin follows the ticket every game tick and reports the end once: arrived, or failed/cancelled with the provider's reason (timeout; session, player or system changed; offline admission or switch withdrawn; different star system; the game did not start the move; provider fault).

When active in an explicitly qualified host, `game.relocation` only moves within the current star system and requires certified offline admission, a verified engine backend, a live player and a known star system. The current product host does not reach this path: native activation is refused until the real reader/worker safety barrier is implemented and verified. F8 therefore remains unavailable in the product; the plugin has no private fallback.

## Switches

- Plugin setting `relocation` (bool, default true, "Allow moving the player (offline only)"): off makes `teleport.go` answer unavailable with that reason. Saving and inspecting keep working.
- Host: `relocation = on|off` in `sc-offline.ini` (default on) is the host policy switch for all plugins when a host is active. It cannot override the current native-startup refusal or make moves available. Neither switch bypasses offline admission.

## Storage

Retains owner ID/database `teleport`, key `spot`, import marker `spawn.txt.time` and original `spot x y z zone` syntax (plus additive `pose x y z w` lines). Existing `spawn.txt` imports transactionally once per changed Windows FILETIME. `spawn_file` overrides its path; empty inherits launcher `SC_OFFLINE_SPAWN_FILE`, otherwise `data_root/spawn.txt`. Without `sco.storage`, F7 writes the spawn file instead.

## Build and install

1. Extract the published `sdk-v2.4.0` archive (it contains `sc_relocation.h` and `sc_game_spatial.h`) and verify it against the release `SHA256SUMS`.
2. `cmake -S plugins/teleport -B build-teleport -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/include [-DSCO_TRAVEL_TEST=ON]`, then build `sc-offline-teleport` (and `travel-model-test`).
3. `cmake --install build-teleport --prefix /path/to/product --component teleport` gives `data/plugins/teleport/` with the native module and its enabled manifest.

`tools/check_teleport_sdk.py --archive sco-sdk-2.4.0.zip --sums SHA256SUMS --work <new dir>` does all of this outside the checkout, runs the synthetic travel model test, traces compiler dependencies, runs boundary negatives and a relocation red arm (POSIX), and runs the SDK's `sco-plugin-check`.

Native modules are trusted code. GPL-3.0-only. Calls run on the game thread; copied metadata is never treated as valid across commands/ticks. Optional `sco.widgets` panel; an absent renderer leaves command-only operation. Unload removes panels, hotkeys and its tick subscription; `game.relocation` cancels and forgets the plugin's tickets on unload.

**Evidence: development machine only.** Built against the published SDK 2.4.0 with synthetic providers and the SDK checker with no game services. Windows/MSVC, the real renderer, storage import twice, and every in-game move: **NOT VERIFIED; not run in game.**
