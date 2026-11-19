# Quantum: the Travel page and quantum drive controls

Quantum moves you to a place in your current star system and keeps a list of named saved spots. Its Quantum drive section adds boost controls, which are not available in 0.8.0.

**Travel to another planet is not in 0.8.0.** The game can name only the zone you are inside and the larger places around it, so a planet or moon outside that chain cannot be reached. Travel refuses such a destination and says why. This is a known gap in the game service, not something to retry.

## Travel page

Open **Travel** in the **Travel** section of the menu (key **M**). The sections follow the order you use them:

- **Choose a destination**: a search box, a list and **Add the places you are inside**. That button adds the zone you are in and the larger places around it. It is not a scan of everything the game has loaded.
- **Set the height and go**: **Arrival height (m)**, 100 to 20000, and **Travel to selected destination**. The button is disabled, with a sentence, for a destination the game cannot resolve from where you are.
- **Your saved spots**: the list, **Go to selected spot**, **Delete selected spot**, a name box and **Save this spot**. A spot remembers the game's zone id as well as its name, and goes back by id while the game still streams that zone.
- **Quantum drive**: shown only when the drive can be controlled. See below.
- **Status**.

Arrival is reported by the plugin: when your own position is within 2 m of the target it says you arrived, even if Core has not confirmed it yet, and it then cancels the Core request. A later timeout of that request is logged and does not change the message. If you never come within 2 m, Core's own result is reported. One move runs at a time. Cancelling cannot undo a move that already started.

## Commands

| Command | What it does |
| --- | --- |
| `quantum.travel <place> <altitude>` | Moves you to a point above the named zone, `altitude` metres (100 to 20000) above its surface along that zone's own up axis. |
| `quantum.bookmark <name>` | Goes to a saved spot. The first case-insensitive match wins. |
| `quantum.save_bookmark <name>` | Saves where you stand under a name. |
| `quantum.delete_bookmark <name>`, `quantum.inspect_bookmark <name>` | Delete a spot, or check that it can be reached without moving. |
| `quantum.scan` | Adds the zones you are inside to the destination list (at most 256 zones, 64 a page). |
| `quantum.status` | Counts destinations and spots and says whether moving is allowed. |
| `quantum.enable <on>` | Turns the drive controls on or off. They are never on at load. |
| `quantum.hold <milliseconds>` | Holds boost for 1 to 1000 ms. |
| `quantum.release` | Ends your hold. |
| `quantum.boost` | Starts or stops a boost hold, at most 1000 ms without a refresh. Bound to **Ctrl+Alt+B**. |
| `quantum.control_status` | Shows the drive's state, or that it is unknown. |

A move needs offline admission, a live player, a known star system and the capability `game.relocation` (on in Core). It stays inside the current star system. The page buttons and these commands go through Core's `game.relocation` and report its result: arrival, refusal or failure. The quick-save and go-back keys, **F7** and **F8**, belong to the [teleport plugin](../teleport/README.md).

## Quantum drive section

The section appears only when the capability `game.quantum.control` is on. Core ships it off and the launcher has no switch for it, so the section does not appear in 0.8.0. When it is on, after `quantum.enable on`, holding **Caps Lock** while the game window is in front refreshes a 150 ms boost hold every 50 ms. The mouse button does the same only if you turn on `mouse_input`; it does not detect NAV mode and does not read the game's own engage key, so leave it off while firing weapons. The provider checks admission for every boost. When the game reports its rules (the capability `game.gamerules.read`, off in 0.8.0), the section shows whether quantum travel and boost are allowed and pauses boost when they are unknown or forbidden. Without that capability the rules do not hold boost back.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `relocation` | on | Allow moving you (offline only). Off keeps you in place. Boost and saving spots still work. |
| `caps_input` | on | Hold Caps Lock to boost. |
| `mouse_input` | off | Hold the left mouse button to boost. |
| `data_root` | empty | Folder for places and saved spots. Empty means the folder of the launcher's `SC_OFFLINE_SPAWN_FILE`, else `data`. |
| `spawn_file` | empty | Where the quick-saved spot is kept when there is no database. |

The host setting `relocation = on|off` in `sc-offline.ini` applies to all plugins and cannot bypass offline admission. Saved spots live in `data/storage/quantum.db` (in `bookmarks.txt` when no database is available). Scan results go to `locations_found.txt`. Spots saved by an older version are imported the first time, and `pose x y z w` lines keep your facing.

### Curated destinations from an ordinary data pack

The destination list comes from exactly one enabled data pack file named `lists/locations.txt`. The `locations_catalog` pack ships it, copied from `data/locations.txt`. Zero matches leave the curated list empty. Two matches refuse as ambiguous. To use your own list, copy it over that pack's file with the game closed, or add a pack of your own, and restart.

Rows are `system | display name | entity name | radius`. Blank lines and lines starting with `#` are ignored. The entity name is required and must match the game's name exactly. An omitted radius means 0. Limits: 1 MiB, 512 bytes per row, 3000 destinations, and 31, 63 and 95 bytes for system, display name and entity name. A malformed row refuses the whole list. For repeated entity names the first row wins. Scan results load afterwards and never replace a curated entry.

`data/plugins/quantum_drive/` is a separate data pack with the new quantum drive's game data. It has no code and no link to this plugin.

## What it needs

- Services: `game.relocation` 1.0, `game.spatial` 1.0, `teleport.spatial` 1.0, `sco.storage`, `sco.settings`, `sco.content` 1.0, `sco.widgets` 1.0, `sco.ui` (the hotkey).
- Capabilities: `game.relocation`; optional `game.quantum.control` and `game.gamerules.read`.

Not run in game. Nothing on this page has run in game, including `quantum.travel`. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_quantum.h`, `sc_game_spatial.h`, `sc_relocation.h`, `sc_gamerules.h`, `sco_widgets.h` and `sco_content.h`.

```sh
cmake -S plugins/quantum -B build-quantum -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_QUANTUM_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-quantum
ctest --test-dir build-quantum -R "^quantum-" --output-on-failure
cmake --install build-quantum --prefix <stage> --component quantum
```

The tests (`quantum-workflow`, `quantum-content`, `quantum-gamerules` and, with Python, `quantum-concurrency`) load the real module against stand-in services. `python tools/check_quantum_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs them from a verified SDK archive. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). `tools/test_quantum_core_integration.sh <core-source> <core-build> <quantum-module>` connects the module to Core's quantum service with a stand-in backend.

The module shares its travel code with the teleport plugin (`plugins/spatial/`). The old host quantum hooks are retained inactive and there is no fallback when plugins are off. Services are reopened on every operation and every callback extent is checked. Callbacks from an older panel or an earlier load are ignored. Unload removes the hotkey and asks Core to end this plugin's hold; Core's owner release stays authoritative.
