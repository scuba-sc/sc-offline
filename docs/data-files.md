# Data files and environment variables

Everything the menu reads is plain text in `data/`. Most files explain their own format in a header comment.

## Shipped lists

| File | Contents |
| --- | --- |
| `ships.txt` | 1102 spawnable vehicles |
| `npcs.txt` | 2239 NPC archetypes |
| `items.txt` | 1489 gear items, in ten slot sections |
| `loadout_slots.txt` | Catalogue of 344 logical weapon, attachment and tool choices in fourteen target sections. The release carries a copy in the `loadout_lists` pack. |
| `outfits.txt` | 35 outfits: each is a `[name]` line, then one line per piece |
| `locations.txt` | Places for the Travel page: system, name, entity, radius |
| `contract_scripts.txt` | The 796 contracts that run without any CIG mission script ([Features](features.md#contracts)) |
| `plugins/build_catalog/lists/buildables.txt` | The default Build picker catalogue, copied from the source list |
| `plugins/quantum_drive/datacore/quantum_drive.toml` | The new quantum drive's game data, applied as the game loads ([Features](features.md#logging)) |
| `OfflineDB/default_1.xml` | The starting loadout. The launcher copies it to `user\client\0`. |
| `plugins\discord\` | The disabled Discord plugin: `plugin.ini`, `disabled` and `discord.dll` ([launcher.md](launcher.md#discord-status)) |
| `plugins\loadout_lists\` | The lists the Gear page reads: copies of `items.txt`, `outfits.txt` and `loadout_slots.txt` ([Features](features.md#plugins)) |
| `plugins\creative\` | The creative plugin: `plugin.ini` and `creative.dll`, enabled by default ([Features](features.md#the-creative-plugin)) |

Other plugins go in their own folder under `plugins\`, with `plugins = on`.

## Data packs

A data pack is a plugin folder with `kind = data` in its `plugin.ini` and no code. The plugins read their lists from packs through sco-core's `sco.content`, never from the files in `data/` directly. Edit a list while the game is closed and restart; there is no automatic import of an old file, no merge and no raw-file fallback. Disabling a pack's folder (an empty `disabled` file) or `plugins = off` removes it. The base files above stay in place, unchanged.

| Pack | Holds | Read by |
| --- | --- | --- |
| `picker_catalog` | Copies of `ships.txt` and `npcs.txt` in `lists/` | The Vehicles, Crew and NPCs pages ([pickers.md](pickers.md)) |
| `locations_catalog` | A copy of `locations.txt` in `lists/locations.txt` (made by `tools/package_release.py`) | The quantum plugin, which needs exactly one enabled `lists/locations.txt` |
| `build_catalog` | `lists/buildables.txt` | The build plugin, which needs exactly one enabled `lists/buildables.txt` |
| `quantum_drive` | `datacore/quantum_drive.toml` | sco-core's DataCore loader |

Two enabled packs holding the same list are ambiguous, and the picker refuses them. Copy your edited list into the matching pack, and keep a backup.

**To use your own Build list.** With the game and launcher closed:

1. Create `data/plugins/my_build_catalog/lists/` and copy your old `buildables.txt` there.
2. Add `data/plugins/my_build_catalog/plugin.ini` with these lines: `id = my_build_catalog`, `name = My Build catalog`, `version = 1.0.0`, `api = 1.1`, `kind = data`.
3. Create an empty `disabled` file in `data/plugins/build_catalog/`, so only one list is enabled. Do not overwrite the bundled pack with your edits.
4. Start the game. An older `data/buildables.txt` that you already have is left alone; releases no longer ship one.

Format and limits: [Build picker](../plugins/build/README.md#build-page).

**Loadout slots.** The loadout plugin reads curated choices from exactly one enabled `lists/loadout_slots.txt`. The release already carries it in the `loadout_lists` pack. If you edit `data/loadout_slots.txt`, copy it into the pack yourself; `loadout.reload_slots` re-reads the pack copy, not the file on disk. Target sections request equipment but do not certify that a part fits its parent or exists in your game build. Format and attribution: [curated Loadout selectors](../plugins/loadout/README.md#curated-weapon-and-tool-selectors).

**Travel.** The quantum plugin reads its destinations once at startup from the one enabled `lists/locations.txt`. Your edits of `data/locations.txt` go into the pack. Travel outside the player's own zone chain is not in 0.8.0 ([Features](features.md#travel-between-places)). Scan results (`locations_found.txt`) and bookmarks keep their own files ([Saves](#saves)). More: [Quantum curated destinations](../plugins/quantum/README.md#curated-destinations-from-an-ordinary-data-pack).

**Game data.** `quantum_drive` is separate from the quantum plugin and has no code dependency. sco-core discovers pack folders in filename byte order and indexes files in relative-name byte order. For DataCore edits, the later write wins a field conflict and core logs both owners. `atomic = true` refuses all operations in a broken TOML file (file atomicity, not a filesystem transaction). If the final splice plan fails, the stock bytes pass through unchanged. Startup packs cannot depend on code that has not loaded yet. Do not edit files while startup indexes them. The old `data/builtin/quantum` path is no longer read; copy edits from it to the new pack while the game is closed, and back up both. To check or lint a pack against a game build you legally hold, without starting the game (do not upload extracted game data):

```sh
sco-dcb lint data/plugins/quantum_drive
sco-dcb check /path/to/Game2.dcb data/plugins/quantum_drive
```

**Content limits.** `sco.content` hands plugins copies of list and script bytes: up to 4096 items, 1 MiB per file and 16 MiB in total. Plugins get no host paths and no script execution through it. The spawn plugin's `spawn.pack_list <ordinal>` serves `lists/ships.txt` this way, and duplicate files stay independently selectable.

## Files the mod creates

| File | Contents |
| --- | --- |
| `storage\` | Your saves: `teleport.db` (the F7 / F8 spot), `quantum.db` (your named Travel spots), each with SQLite's `-wal` and `-shm` files, and any plugin's own database ([Saves](#saves)) |
| `wallet.txt` | Your aUEC balance, the only wallet store. You can edit it while the game is closed. |
| `spawn.txt` | Your F7 / F8 position from before `storage\`; read once, then left as it is |
| `bookmarks.txt` | Your named Travel spots from before `storage\`; read once, then left as it is |
| `locations_found.txt` | The results of the Travel scan |
| `menu-window.ini`, `menu-view.ini` | The menu's window size, and its open page and Show developer pages switch |
| `mod.log` | What the mod did this session |
| `launcher.log` | What `sc-offline.exe` did on its last run |
| `game-path.txt` | The game folder the launcher found last time; delete it to make it search again |
| `game-build.txt` | The game version from your last play, for the launcher's update check |
| `menu_background.png` / `.jpg` | Your menu background image, if you add one |

`asop_ships.txt` is reserved for the ASOP saved-selection editor, which is not enabled in this build. The build neither creates nor reads this file ([details](asop-selection-candidate.md)).

## What persists between sessions

| What | Where | Written |
| --- | --- | --- |
| F7 saved spot | `storage\teleport.db` | when you save it |
| Travel bookmarks | `storage\quantum.db` | when you save or delete one |
| Discovered locations | `locations_found.txt` | by the Travel scan |
| Wallet (aUEC) | `wallet.txt` | when the balance changes, and at exit |
| Menu window size | `menu-window.ini` | when you finish resizing the menu |
| Plugin settings | `storage\<id>.db` | by the launcher or the plugin |
| Plugin on/off | each plugin's `disabled` file | by the launcher |

These do **not** persist: gear you equip, owned or stored ships and hangar contents, contract progress and reputation, objects placed with build, and world state.

## Saves

The F7 / F8 spot and the Travel page's saved spots are kept by the `teleport` and `quantum` plugins in sco-core's `sco.storage` service: one SQLite database each in `data\storage\<plugin>.db`. A save is all or nothing, so a crash keeps either the old save or the new one.

The old files are imported, not deleted:

- `spawn.txt` and `bookmarks.txt` are read into storage the first time the game starts with this version (`[storage] imported spawn.txt into teleport` in `mod.log`) and are not written after that. If one changes later (you copy one in from an older install, say), it counts as newer and is imported again, replacing the saved spot or list.
- `wallet.txt` is written when your balance changes and once more when the game closes, to a temporary file that then replaces it, so a crash leaves the old balance or the new one. No code opens `storage\contracts.db`, so no such database exists. Deleting `wallet.txt` takes you back to the starting amount from `OfflineDB\default_1.xml`.

If storage cannot be used (the plugin did not load, or a database cannot be opened or written), `mod.log` says so with a `[storage]` line and that feature reads and writes its `.txt` file as before. To forget what a database holds, delete its three files (`.db`, `.db-wal`, `.db-shm`) while the game is closed. The `.txt` file, if there is one, is imported again on the next start.

## Developer test hooks

When one of these files is in `data/`, `src/contracts.cpp` or `src/spawner.cpp` changes its behavior. Git ignores them. Players do not need them.

| File | Effect |
| --- | --- |
| `contract_test.txt`, `phase_test.txt`, `spawn_test.txt` | Test hooks for contracts, mission phases and spawning |
| `list_first.txt` | Contracts named in this file are listed first |
| `ui_notify_off.txt` | New objectives are not announced to mobiGlas |

## Environment variables

`sc-offline.exe` sets these before it starts the game, mostly from `sc-offline.ini`. The capability switches (`SCO_*`) are in [launcher.md](launcher.md#capability-switches-in-sc-offlineini).

| Variable | Effect |
| --- | --- |
| `SC_OFFLINE_BOOT_MAP` | The boot map, from `boot_map` (default `PU_All`). |
| `SC_OFFLINE_START_SHIP` | Legacy startup ship request, from `start_ship`. The spawn plugin does not act on it. |
| `SC_OFFLINE_START` | Legacy destination request, from `start`. `Daymar` does not enable destination startup. |
| `SC_OFFLINE_PLUGINS` | From `plugins`: `on` loads plugins from `plugins\` beside the ship list (`data\plugins`); anything else is off. |
| `SC_OFFLINE_MULTIPLAYER` | From `multiplayer` (`off` in the shipped `sc-offline.ini`): `off` removes the Multiplayer tab and adds no multiplayer firewall rule; anything else, or unset, leaves it on. |
| `SC_OFFLINE_MULTIPLAYER_ALLOW` | From `multiplayer_allow`, checked by the launcher: extra IPv4 ranges a session may use as firewall exceptions. They do not authorize a session by themselves. |
| `SC_OFFLINE_ASOP_FLEET_LIST` | From `asop_fleet_list`: `ships` lists `ships.txt` at the terminals; unset or anything else leaves the game's own list (with `asop` on). |
| `SC_OFFLINE_ASOP` | From `asop`: `off` (or `0`, `no`, `false`) leaves the ship terminals, hangars and ATC alone; unset or anything else is on. |
| `SC_OFFLINE_MENU` | From `menu`: `rust` loads `sc_offline_menu.dll` and shows the new menu, when the installed `dinput8.dll` is the one with the menu hook (`dinput8-rust-menu.dll`). Anything else, or unset, keeps the classic menu. |
| `SC_OFFLINE_MENU_KEY` | The key that opens the new menu (default M): a letter or `F1` to `F12`. Read only when the new menu is on. You set this one yourself. |
| `SC_OFFLINE_MINING` | Legacy, import only; the runtime ignores it, and so it does `SC_OFFLINE_MINING_DEBUG`. Explicit import: [mining](../plugins/mining/README.md). |
| `SC_OFFLINE_SHIPS_FILE` | Where the ship list is read from. |
| `SC_OFFLINE_REGISTRY_PROBE` | From `registry_probe`: `1` writes `registry-dump.txt` beside `mod.log`. |
| `SC_OFFLINE_SPAWN_FILE` | Where the F7 position is stored. |
| `SC_OFFLINE_MOD_LOG` | Where `mod.log` is written. |
| `SC_USER` | The game's `user\client\0` folder, which is how loadouts reach the game. |
