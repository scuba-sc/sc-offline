# Data files and environment variables

Everything the menu reads is plain text in `data/`. Most files explain their own format in a header comment.

## Shipped lists

| File | Contents |
| --- | --- |
| `ships.txt` | 1102 spawnable vehicles |
| `npcs.txt` | 2239 NPC archetypes |
| `items.txt` | 1489 gear items, in ten slot sections |
| `loadout_slots.txt` | Optional catalogue: 344 public logical weapon/attachment/tool choices in fourteen explicit target sections; copy into an enabled data pack to use it |
| `plugins/build_catalog/lists/buildables.txt` | Default Build and Squadron 42 picker catalog, copied from the tracked source list |
| `outfits.txt` | 35 outfits: each is a `[name]` line, then one line per piece |
| `locations.txt` | Places for the Travel tab: system, name, entity, radius |
| `contract_scripts.txt` | The 796 contracts that run without any CIG mission script; see [features.md](features.md#contracts). |
| `plugins/quantum_drive/datacore/quantum_drive.toml` | The new quantum drive's game data, applied as the game loads (see [features.md](features.md#logging)) |
| `OfflineDB/default_1.xml` | The starting loadout. The launcher copies it to `user\client\0` |
| `plugins\discord\` | The blocked Discord status plugin: `plugin.ini`, `disabled` and `discord.dll`, off pending transport/field approval (see [launcher.md](launcher.md#discord-status)) |
| `plugins\creative\` | The creative plugin: `plugin.ini` and `creative.dll`, enabled by default (switch it off in the launcher's Plugins page; see [features.md](features.md#the-creative-plugin)). Other plugins go in their own folder here with `plugins = on` |

## Public picker catalog snapshots

Release packaging and the CMake `picker_catalog` component install copies of
`ships.txt` and `npcs.txt` in `data/plugins/picker_catalog/lists/`, with an ordinary
data manifest. The external Vehicles, Crew and NPCs panels read these enabled
startup snapshots through `sco.content`; original base files remain unchanged.
Copy existing user-edited lists into the pack while stopped; there is no implicit
base-file import or private-reader fallback. Restart after edits. Shared filter/
paging and bounded ownership rules: [pickers.md](pickers.md).

## Travel locations pack

The release also ships `data/plugins/locations_catalog/lists/locations.txt`, a copy of `data/locations.txt`, with an ordinary data manifest (`tools/package_release.py` makes the copy). The external Quantum/Travel plugin requires exactly one enabled `lists/locations.txt` and reads it once at startup; there is no base-file fallback. Copy your edits of `data/locations.txt` into the pack while the game is closed. Reaching a place outside the player's own zone chain is not in 0.8.0 ([features.md](features.md#travel-between-places)).

## Ordinary data packs and content access

The external Loadout Player panel reads curated weapon/tool choices from exactly
one enabled public `lists/loadout_slots.txt` List. The optional source catalogue
is not installed by the Loadout component or imported from the top-level file.
Copy it explicitly into an ordinary data pack while the host is stopped, then
restart. `loadout.reload_slots` re-reads the copied startup snapshot; it does not
refresh files on disk. Duplicate canonical Lists refuse as ambiguous. Logical
target sections request equipment; they do not certify parent/item compatibility
or engine ports. Format, attribution and unchanged-slot semantics:
[curated Loadout selectors](../plugins/loadout/README.md#curated-weapon-and-tool-selectors).

The external Quantum Travel consumer reads curated destinations from exactly
one enabled public `lists/locations.txt` record in an ordinary data pack.
Explicitly copy a user-owned curated list there while stopped; no automatic
base-file import or production catalogue staging is added. Duplicate matching
pack records refuse as ambiguous. The top-level `locations.txt` remains on disk
but is not this consumer's input. Scan-generated `locations_found.txt` and
bookmark storage keep their separate existing filesystem policy. Bounds and
restart behavior: [Quantum curated destinations](../plugins/quantum/README.md#curated-destinations-from-an-ordinary-data-pack).

The external Build module reads exactly one enabled public `lists/buildables.txt`
grouped list. Release staging and the CMake `build_catalog` component provide
an enabled `data/plugins/build_catalog` pack. The legacy top-level
`data/buildables.txt` is omitted from releases, preserving an existing user file.

To use your existing edited list, stop the host and launcher, create a separate
`data/plugins/my_build_catalog/lists/` directory, and copy your legacy file to
`lists/buildables.txt` there. Add `data/plugins/my_build_catalog/plugin.ini` with
`id = my_build_catalog`, `name = My Build catalog`, `version = 1.0.0`,
`api = 1.1`, and `kind = data` on separate lines. Create an empty `disabled`
file in `data/plugins/build_catalog/` to disable the bundled pack, then restart.
Keep your original as a backup. Two enabled lists are ambiguous and the picker
refuses them; there is no automatic import or raw-file fallback. Do not overwrite
the bundled pack with your edits. Format and limits: [Build picker](../plugins/build/README.md#grouped-build-and-sq42-picker).

`data/plugins/quantum_drive/plugin.ini` declares an ordinary `kind = data`
pack, separate from the native `quantum` plugin. It has no entry or code dependency.
`plugins = off`, a `disabled` marker, a missing/invalid manifest or discovery
refusal leaves that pack stock; there is no synthesized pack fallback.
Restart after installing, disabling or editing packs. The old
`data/builtin/quantum` path is no longer consumed; user edits there must be copied
explicitly to the new pack while stopped (back up both first). Do not load both
legacy and G1 host CryPak producers: the host composition adapter owns early
DataCore discovery/application once integrated.

The existing sco-core pipeline discovers folders in filename byte order and
indexes files in relative-name byte order. DataCore writes later in that order
win a field conflict, which core logs with both owners. `atomic = true` refuses
all operations in a broken TOML file; it is **file** atomicity, not a filesystem
transaction. A failed final splice plan passes stock bytes through CryPak. Core
refuses unsafe/unreadable indexed packs and propagates dependency refusal.
Startup packs must not depend on not-yet-loaded code providers. Do not edit files
while startup is indexing them.

G14 supplies public copied list/script bytes via `sco.content` (4096 items,
1 MiB/file, 16 MiB total). External spawn exposes `spawn.pack_list <ordinal>`
for `lists/ships.txt` using only that table; duplicate files remain independently
selectable. No plugin gets host paths, private ContentIndex access or script
execution from this route. It is additive: **the base list files above stay
in place**, unchanged, with no automatic import, deletion or merge. Other feature
pack formats are not newly claimed as supported.

Owner-local validation commands (not executed for this change):

```sh
sco-dcb lint data/plugins/quantum_drive
sco-dcb check /path/to/legally-held/Game2.dcb data/plugins/quantum_drive
```

Do not upload extracted DCB/game data. These commands do not certify Windows
CryPak timing or gameplay.

## Files the mod creates

| File | Contents |
| --- | --- |
| `storage\` | Your saves: `teleport.db` (the F7 / F8 position), `quantum.db` (your named Travel spots), each with SQLite's `-wal` and `-shm` files, and any plugin's own database. See [Saves](#saves) |
| `wallet.txt` | Your aUEC balance, the only wallet store today; you can edit it while the game is closed |
| `spawn.txt` | Your F7 / F8 position from before `storage\`; read once, then left as it is |
| `bookmarks.txt` | Your named Travel spots from before `storage\`; read once, then left as it is |
| `locations_found.txt` | The results of the Travel scan |
| `mod.log` | What the mod did this session |
| `launcher.log` | What `sc-offline.exe` did on its last run (written by the launcher) |
| `game-path.txt` | The game folder the launcher found last time; delete it to make it search again |
| `game-build.txt` | The game version from your last play, for the launcher's update check |
| `menu_background.png` / `.jpg` | Your menu background image, if you add one |

`asop_ships.txt` is reserved for the gated ASOP selection candidate. The current
build refuses before the host selection service starts, so it does not create or
read this file and the file does not change the terminal list. See the
[ASOP selection candidate](asop-selection-candidate.md).

## What persists between sessions

| What | Where | Written |
| --- | --- | --- |
| F7 saved spot | `storage\teleport.db` | when you save it |
| Travel bookmarks | `storage\quantum.db` | when you save or delete one |
| Discovered locations | `locations_found.txt` | by the Travel scan |
| Wallet (aUEC) | `wallet.txt` | when the balance changes, and at exit |
| Menu window size | `menu-window.ini` | when you finish resizing the menu |
| Plugin settings and enable markers | each plugin's folder | by the launcher, while the game is closed |

What does **not** persist today:

- Inventory and loadout: gear you equip is session only.
- Owned or stored ships and hangar contents: session only.
- Contract progress and reputation.
- Objects placed with build mode.
- World state.

## Saves

The F7 / F8 spot and the Travel tab's saved spots are kept by the `teleport` and `quantum` plugins in sco-core's `sco.storage` service: one SQLite database each in `data\storage\<plugin>.db`. A save there is all or nothing, so a crash keeps either the old save or the new one.

The old files are imported, not deleted:

- `spawn.txt` and `bookmarks.txt` are read into storage the first time the game starts with this version (`[storage] imported spawn.txt into teleport` in `mod.log`) and are not written after that. They stay readable as they were. If one changes later (you copy one in from an older install, say), it is newer than what storage has seen and is imported again, replacing the saved spot or the saved spot list.
- `wallet.txt` is the wallet's only store: no code opens `storage\contracts.db` today, so no such database is created. It is written when your balance changes and once more when the game closes, to a temporary file that then replaces it, so a crash leaves the old balance or the new one. Deleting `wallet.txt` takes you back to the starting amount from `OfflineDB\default_1.xml`.

If storage can't be used (the plugin didn't load, or a database can't be opened or written), `mod.log` says so with a `[storage]` line and that feature reads and writes its `.txt` file as before. Delete the three files of a database (`.db`, `.db-wal`, `.db-shm`) while the game is closed to forget what it holds; the `.txt` file, if there is one, is imported again on the next start.

## Developer test hooks

When one of these files is present, `src/contracts.cpp` or `src/spawner.cpp` changes its behavior. Git ignores all of them. Players don't need them.

| File | Effect |
| --- | --- |
| `contract_test.txt`, `phase_test.txt`, `spawn_test.txt` | Test hooks for contracts, mission phases and spawning |
| `list_first.txt` | Contracts named in this file are listed first |
| `ui_notify_off.txt` | New objectives aren't announced to mobiGlas |

## Environment variables

`sc-offline.exe` sets these before it starts the game. `sc-offline.ini` supplies the first four.

| Variable | Effect |
| --- | --- |
| `SC_OFFLINE_BOOT_MAP` | The boot map, from `boot_map` (default `PU_All`). |
| `SC_OFFLINE_START_SHIP` | Retained legacy startup ship request from `start_ship`; current external destination startup is unavailable |
| `SC_OFFLINE_START` | Retained legacy destination request from `start`; `Daymar` does not enable the current spawn module's deferred destination-startup route |
| `SC_OFFLINE_PLUGINS` | From `plugins`: `on` loads plugins from `plugins\` beside the ship list (`data\plugins`); anything else is off |
| `SC_OFFLINE_MULTIPLAYER` | From `multiplayer`: `off` switches the Multiplayer tab off; anything else (or unset) leaves it on |
| `SC_OFFLINE_MULTIPLAYER_ALLOW` | From `multiplayer_allow`, checked by the launcher: extra IPv4 ranges a session may use |
| `SC_OFFLINE_ASOP_FLEET_LIST` | From `asop_fleet_list`: `ships` lists `ships.txt` at the terminals; unset or anything else leaves the game's own list (with `asop` on) |
| `SC_OFFLINE_MINING` | Legacy import-only preference; ignored by the runtime. `SC_OFFLINE_MINING_DEBUG` likewise. Explicit `--environment` import is documented in [mining](../plugins/mining/README.md). |
| `SC_OFFLINE_MENU` | From `menu`: `rust` loads `sc_offline_menu.dll` and shows the Rust in-game menu; anything else (or unset) keeps the ImGui menu. Opt-in: the Rust menu has not run in game |
| `SC_OFFLINE_MENU_KEY` | The key that opens the Rust menu (default M). Read only when the Rust menu is on |
| `SC_OFFLINE_ASOP` | From `asop`: `off` (or `0`, `no`, `false`) leaves the ship terminals, hangars and ATC alone; unset or anything else is on |
| `SC_OFFLINE_SHIPS_FILE` | Where the ship list is read from |
| `SC_OFFLINE_REGISTRY_PROBE` | From `registry_probe`. `1` writes `registry-dump.txt` beside `mod.log` |
| `SC_OFFLINE_SPAWN_FILE` | Where the F7 position is stored |
| `SC_OFFLINE_MOD_LOG` | Where `mod.log` is written |
| `SC_USER` | The game's `user\client\0` folder, which is how loadouts reach the game |
