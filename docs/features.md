# Features

This page says what each feature is for and how to use it. What has been run in game is recorded once, in [Status](status.md). Only seven things have been seen working: startup with the classic menu, gear equip, placing an Outposts item, ship spawn with boarding and Flight Ready, character spawn, a saved-spot move and a seat event test. Every other feature below has not been run in game, and "works" on this page means "is designed to".

Press **M** in game to open the menu. Which pages appear depends on the enabled plugins. The window can be resized from its edges and corners. When you finish a resize, the outer size (normalised to 96 dpi) is saved in `data/menu-window.ini` and restored the next time the game starts, for the monitor it is on. The size is clamped between 720 x 520 and 90% of that monitor's work area, and a missing or bad file falls back to 1000 x 800 before clamping. Delete `menu-window.ini` while the game is closed to reset it. Long tab titles keep their width; use the tab bar's arrows when they overflow. The same menu shell runs under the classic menu and the new menu ([Which menu](../README.md#which-menu)); the new one is described in [Plugin UI](plugin-ui.md#the-new-menu-opt-in).

Most lists are plain text files in `data/`, and the counts below come from those files. Remove lines from a file and its list gets shorter ([data-files.md](data-files.md)).

## Player

- **Creative controls.** Noclip, god mode, infinite ammo and infinite ship ammo come from the [creative plugin](#the-creative-plugin), shipped on, on its **Creative** tab.
- **Gear.** The Gear page of the [loadout plugin](../plugins/loadout/README.md) equips items from the `items.txt` list in the `loadout_lists` pack (with `outfits.txt` and `loadout_slots.txt` beside it) (the `exp_loadout_async` switch, on by default). Equipping was seen working in game. Slots the game does not support are refused with a reason, and there is no fallback ([Plugins](#plugins)).
- **Wallet.** Your aUEC balance is loaded when you spawn and saved to `data/wallet.txt` when it changes (checked every 5 seconds) and once more when the game closes. That file is the only wallet store. It holds just the number; edit it while the game is closed to change your balance. Without the file you start with the `aUEC amount` in `data/OfflineDB/default_1.xml` (100,000,000). Once `wallet.txt` exists, that amount is no longer used, so delete the file to go back to it ([persistence](data-files.md#what-persists-between-sessions)).

## Travel

- **Teleport** to planets, moons, stations, Lagrange points and jump points, grouped by star system (`locations.txt`). F7/F8 and `quantum.travel` run through `game.relocation`. The Travel page works only within the star system you are in.
- **Scan** lists everything the game has loaded and writes it to `locations_found.txt`. Interiors and small zones are hidden unless you ask to see them.
- **Saved spots.** **F7** saves one quick position and **F8** takes you back to it (`data/storage/teleport.db`). F8 works while the saved zone is still loaded; otherwise it refuses with a plain sentence. The Travel page also keeps named spots (`data/storage/quantum.db`). Spots in `bookmarks.txt` and `spawn.txt` from an older version are imported the first time you start this one ([Saves](data-files.md#saves)).
- Pyro and Nyx can be reached only with the default `boot_map = PU_All`. Some Pyro places drop you in orbit, because `locations.txt` does not have their radius yet.
- **Quantum drive.** Hold Caps Lock (or B for NAV mode, then hold the left mouse button) to boost. This needs the quantum plugin's controls on and a game that allows them; the page pauses new boosts when the game's rules cannot be read or forbid them.
- **The Gladius' new quantum drive** is game data, not code: the `quantum_drive` data pack gives the Gladius' quantum drive (the Wetkah Beacon) the game's new drive parameters. sco-core applies it to the game's DataCore database (`Data\Game2.dcb`) as the game loads. See [Logging](#logging) for the `mod.log` lines.

### Travel between places

**Not in 0.8.0: travel to another planet.** Moving to a place needs the zone that place belongs to, and the zone lookup works only along the player's own chain (the star system, planet and zone the player is already in). A destination outside that chain cannot be resolved, so `quantum.travel` refuses it and the Travel page says so. The saved-spot commands (F7/F8) are unaffected. This is a documented gap, not a bug to retry. The places list ships as the `locations_catalog` data pack (`data/plugins/locations_catalog/lists/locations.txt`, a copy of `data/locations.txt`), and the quantum plugin requires one such list.

## Vehicles

- Filter and page the copied ship catalog (`lists/ships.txt` packs), and select a class or type one by hand. A class in the catalog may not exist in your game build ([pickers.md](pickers.md)).
- **Where you board:** the pilot seat, a seat by name, choose after spawning, or stay outside. The spawn plugin never removes another NPC from a seat; removing crew belongs to the Crew page.
- **Power on** sends the game's Flight Ready event. Some ship systems may still start off. It is offered only with the pilot seat; with any other seat the toggle is greyed out ("Flight Ready needs the pilot seat").
- **Infinite ship ammo** refills the magazines of the ship you are aboard ([creative plugin](#the-creative-plugin)).

## Ship terminals, hangars and ATC

With `asop = on` in `sc-offline.ini` (the default) the station's ship terminals (ASOP) are designed to work offline:

- **Open a terminal.** It lists the ships of `ships.txt` instead of staying on "Stand by". The saved-selection editor is not enabled in this build ([details](asop-selection-candidate.md)).
- **Deliver** stores a ship at the terminal's station. The ship is spawned 1000 m above you for about 3 seconds, then stored. The terminal shows "Awaiting Delivery" for a few seconds, then "Stored" with a Retrieve action.
- **Retrieve** gets you a personal hangar from the station's ATC. The station's elevators list it. The hangar's ship lift lowers, the ship is placed on it and the lift brings it up. Terminals then list the ship as "On Pad" in your hangar, with a Store action.
- **Store** at the hangar terminal lowers the lift with the ship, stores it and raises the empty lift. You can Retrieve it again afterwards.
- **ATC hails.** A hail from inside your hangar is a take-off: the doors open and close behind you. A hail from outside gets you a landing pad with its marker on your HUD. At another station the ATC makes you a hangar there, and Store there keeps the ship at that station.

The terminal offers Deliver only with the game's delivery system switched on. Online the server does that; here sc-offline sets `g_itemRecovery.deliverySystemSetup 1`. **Show local ships** lists only ships at this station, so it stays empty until you have delivered one there.

Limits: one ship out at a time. Stored and retrieved ships last for the session. A retrieved ship is a new ship of the same class, so damage, cargo and loadout changes do not carry over. Only small and medium hangars have been tried. Plugins can move your retrieved ship with `set_entity_transform` (`sc_spawn.h`).

Each part switches itself off when the game's code for it is not found on your game version. `mod.log` lists every part at startup, for example `[+] ship terminal open (rc1): ready (...)` or `[!] ship terminal Deliver (rc3): needs atc.store_vehicle (MISSING)`. With `asop = off` it reads `[-] ship terminals, hangars and ATC (ASOP): off`. Each Deliver, Retrieve and Store step is logged with `[asop]`, and hangar and ATC steps with `[iim]` and `[atc]`. The parts are also capabilities plugins can ask about: `asop.terminal`, `asop.caller`, `asop.list`, `asop.deliver`, `asop.claim_timeout`, `asop.retrieve`, `hangar.lift`, `atc.store`, `hangar.instance`, `atc.tokens` and `asop.diagnostics`.

## Crew

The Crew page shows up to 128 copied seats with occupant and usability fields, in 32-row pages, and says when the list is cut short. **Sit**, **Stand**, **Remove** and **Add** use fresh public providers. Only you and crew-owned NPCs can be moved, and only crew-owned NPCs can be removed. NPCs added here sit but do not fly the ship or operate turrets.

The page and the `crew.*` commands act on the ship you are aboard, or the one `crew.target` pinned. Some entries in a ship's seat list are not crew seats (turret items and remote-operated parts; the game's own seat picker skips them). `crew.fill` seats NPCs only in empty, usable seats and says how many it skipped: `Seating 10 x <npc> (5 seats skipped: not interactable)`. `mod.log` has one `interactable yes|no` line per seat, and `crew.sit` refuses a seat that is not usable.

## NPCs

`npc` ships enabled (`data/plugins/npc/{plugin.ini,npc.dll}`) and needs `plugins = on`. Disable its folder in the launcher for the next game start. The NPCs page uses the same handlers as the commands, which run through the host command registry on the game thread:

| Command | Does |
| --- | --- |
| `npc.spawn <archetype> <count>` | Requests 1 to 10 owned NPCs near a fresh player pose and logs the returned session IDs as pending streaming. `npcs.txt` is a catalogue, not a promise that every class exists in your game build. |
| `npc.observe` | Logs pending, visible, removing and unknown counts, and visible transforms. It is read-only. |
| `npc.remove <id>` | Requests removal of one NPC this plugin owns. Crew and foreign IDs are rejected. |
| `npc.clear` | Requests removal of only this plugin's NPCs, never a crew-wide clear. |

Spawn and removal use `game.actors`, the pose uses `teleport.spatial` and observation uses `game.entities`; each command reopens these services. There is no ground ray. `SCO_OK` means removal started, not that the NPC disappeared, and `alive=0` does not prove removal. Capacity per load is bounded by `SC_ACTORS_MAX_NPCS`, counting accepted removals. A native fault needs a restart, not a reload. See the [plugin README](../plugins/npc/README.md) and [SDK workflows](sdk-workflows.md).

## Build

- **F6 build mode** (free camera, aiming, preview) is not available. The build plugin offers `build.place`, `build.undo` and `build.clear` only. The historical mode had 3728 buildable objects in 18 groups (`buildables.txt`).
- `build.place` asks `game.entities` to spawn the object in your zone and `game.world` to ground it 0 to 100 m ahead of you. `build.undo` and `build.clear` remove what the plugin placed. Placements belong to the plugin, are removed when it unloads and are not kept across a reload. If `game.world` is not published, placement refuses and `mod.log` says why. See the [build plugin README](../plugins/build/README.md).

## Contracts

`contract_scripts.txt` lists the 796 contracts meant to run without any of CIG's mission scripts (hauling and similar). The mobiGlas list leaves out Pyro and Nyx contracts and anything named test, debug or tutorial. `mod.log` records the counts at startup: `[contracts] N contracts known; M run ...`. This repository ships none of CIG's mission scripts, so contracts that need them (bounty, delivery with combat, salvage and others) are not offered.

## Natural mining

The `mining` plugin owns `mining.status` and two settings, `enabled` and `debug`, both off by default. The old ini keys and `SC_OFFLINE_MINING*` switches no longer enable it. To import old settings deliberately, see the [mining plugin](../plugins/mining/README.md). The game pack owns the bounded `game.mining` service. Its native bridge stays unavailable until engine and admission checks pass, and a requested setting is not active gameplay.

## Squadron 42

The Squadron 42 page belongs to the loadout plugin, which ships with 0.8.0. It holds the Squadron 42 spoiler warning and selection state. The old built-in tab (35 outfits, four toggles, a 60-item buildable list, a ship list, a console) was removed from the host. Arbitrary console execution is refused, and nothing writes `USER.cfg` or Squadron 42 console variables.

## Menu

The menu background can be a picture saved as `data/menu_background.png` (or `.jpg`); restart the game to load it. The menu's own settings page sets how dark the background is and where the image sits.

## Logging

Everything the mod does is written to `data/mod.log`; its first line is the version. The startup lines include sco-core's signature report, `[core] signatures: N/N OK`, then one line per game address that was not found (`MISSING`, `AMBIG`, `FAILED` or `BLOCKED`, with the reason). Messages shown in the menu's status strip are logged as `[status] ...`.

For the new quantum drive the log says, in order: `[+] new quantum drive: game data loader hooked (pack quantum_drive applies as Game2.dcb loads)`; `[plugin] loaded quantum_drive <version> (data, 1 files)`; `[datacore] 1 pack: quantum_drive 508/508 applied`; and `[+] new quantum drive: game data patched as it loaded (pack quantum_drive: 508 operations), load ok in N ms`. The pack names records and fields, not byte offsets, so it survives a game update as long as those still exist. If an update removed or renamed something, none of it applies: the game loads its own data, the Gladius keeps its stock drive, and a `[!] new quantum drive: pack quantum_drive not applied (...)` line names what is missing. The capability `quantum.drive` is on only when the pack applied. To check the pack against a new game build without starting the game, run sco-core's `sco-dcb check <Game2.dcb> data/plugins/quantum_drive` (exit 0 means all of it applies).

## Plugins

sc-offline loads plugins built with the [sco SDK](https://github.com/scuba-sc/sco-core/blob/main/sdk/README.md): native DLLs, Lua scripts and data packs. Loading is **on by default** (`plugins = on`). The release holds 12 native plugins: `spawn`, `crew`, `build`, `mining`, `npc`, `inspector`, `teleport`, `quantum`, `contracts`, `creative` and `loadout` (all on), and `discord` (off). The Gear and Squadron 42 pages and the `loadout.*` commands belong to `loadout`. The host has no built-in fallback for any of them.

1. Keep `plugins = on` in `sc-offline.ini`.
2. Put each plugin in its own folder, `data/plugins/<id>/`, with a `plugin.ini` (for example `data/plugins/hello/plugin.ini` and `hello.dll`). The SDK's examples (`hello`, `greeter`, `travel_pack`) are ready to copy.
3. Start the game. After the startup lines, `mod.log` reports what it found:

   ```
   [plugin] <found> found, <loaded> loaded (plugins = on)
   [plugin] spawn <version> native loaded
   [plugin] discord <version> native disabled
   [plugin] greeter 1.0.0 lua loaded
   ```

   A plugin that cannot load is listed with the reason (`refused: built for api 2.0`, `missing capability 'teleport'`, ...), and the others still load. To switch one plugin off, put an empty file named `disabled` in its folder (the launcher's Plugins page does the same: [launcher.md](launcher.md#the-plugins-page)).

With `plugins = off` nothing loads: plugins, optional bridges and data packs are all skipped, and no hidden implementation takes their place. A plugin runs its own code in the game, so install only plugins you trust.

### Plugin commands

Each prefix is its plugin's id. Plugins call commands through the SDK's `invoke`. A command that ran but could not do its job answers `failed` with the reason. A fault in a plugin's per-tick work switches that plugin off (`[plugin] <id> ... crashed`) instead of crashing the game. A fault while its tab is drawn switches off only that plugin and its tabs. A plugin folder cannot reuse the id or command prefix of another.

| Command | Does | Key |
| --- | --- | --- |
| `teleport.save` | Saves where you stand (`data/storage/teleport.db`) and names the spot | **F7** |
| `teleport.go` | Teleports to the saved spot and says where you went, or why not | **F8** |
| `spawn.ship <class> <height>` | Spawns a ship `<height>` m (0 to 10000) above you and makes it the crew target. An unknown class answers `failed`. | |
| `crew.target` | Pins the ship you are in as the target of the `crew.*` commands | |
| `crew.sit <seat>` | Puts you in the target ship's first seat whose name has these words (`pilot`, `turret left`), removing a crew NPC in it | |
| `crew.stand_all` | You and the NPCs `crew.fill` added get out of their seats | |
| `crew.fill <npc>` | Puts an NPC of this archetype in every empty, usable seat and says how many seats it skipped | |
| `crew.clear` | Removes the NPCs `crew.fill` added | |
| `crew.power_on` | Sends the game's Flight Ready event to the target ship | |
| `quantum.travel <place> <altitude>` | Teleports you to a place from the Travel page, `<altitude>` m (100 to 20000) above the ground | |
| `quantum.bookmark <name>` | Teleports you to a saved spot | |
| `quantum.save_bookmark <name>` | Saves where you are as a named spot (`data/storage/quantum.db`); an empty name uses the zone's | |
| `quantum.scan` | Scans everything the game has loaded into `locations_found.txt` | |
| `build.toggle` | Build mode on or off (refuses: interactive mode is unavailable) | **F6** |
| `build.undo` | Removes the last object you placed | |
| `build.clear` | Removes everything you placed | |
| `build.place <object> <ahead>` | Places one buildable (`buildables.txt`) `<ahead>` m in front of you (0 is at your feet, up to 100) | |
| `contracts.status` | How many contracts are known, offered and running, and your wallet balance | |
| `mining.status` | Whether natural mining is on, and how many ecosystem cells it has seen and promoted | |
| `loadout.equip <items>` | Equips items from `items.txt` (separated by spaces or commas, one per slot; other slots stay empty). | |
| `loadout.wear <outfit>` | Wears a Squadron 42 outfit from `outfits.txt`. Outfit slots are off in this build. | |

Multiplayer commands (`multiplayer.status`, `multiplayer.leave`, `multiplayer.goto`) belong to [sc-mp](https://github.com/scuba-sc/sc-mp), which the release does not include.

Each command needs a capability: `game.vehicles.seats`, `game.vehicles.seat`, `game.vehicles.flight_ready` and `game.actors.despawn` for `crew.*`, `game.mining` for `mining.status`, and so on. One is missing when the addresses for that feature are not found in this game build; the rest of the plugin system still starts.

For plugin authors, sco-core's game pack publishes two services, each from the game thread only:

- `spawn.entities` 1.2 ([`sc_spawn.h`](../external/sco-core/include/sc_spawn.h)) spawns an entity class near you, looks up your entity and ship ids, asks whether an entity id still resolves (`entity_alive`, 1.1), and moves and turns an entity in the world frame or a zone's frame (`set_entity_transform`, 1.2). A plugin may move only what it spawned through `spawn_as`, or your own vehicle once ASOP registers it. What `spawn_near_player` and `spawn.ship` spawn belongs to nobody. A plugin built against 1.0 or 1.1 keeps working; one that uses a later function checks the table's `size` first.
- `teleport.spatial` 1.0 ([`sc_spatial.h`](../external/sco-core/include/sc_spatial.h)) gives your position and orientation in your zone, the zone an entity is in, a zone's name, and positions converted between a zone and the world or between two zones. Zones are the game's nested frames (star system, planet, city or station, ship, room). Ids are the game's own 64-bit ids and positions are in metres. An id that has streamed out answers 0.

### Menu pages and hotkeys for plugins

A plugin can add a menu page, a badge beside the page name and an overlay, and can bind a free key chord such as `ctrl+alt+9` to any command. sc-offline keeps **M** and build mode's keys for itself. [Plugin UI](plugin-ui.md) has the page order, how to draw and the key table.

When you quit the game (the menu's Quit, or the `quit` console command), the host sends `game.exit` once and closes admission, so no new work is accepted. Plugins stay loaded until the process exits, and `mod.log` shows one `[shutdown] game-thread closure` line. If the game crashes or is killed, plugins get no `game.exit`, so do not rely on it to save anything that matters. The wallet is written on change, so at most the last few seconds of a balance change are at risk.

### The inspector plugin

`data/plugins/inspector` ships enabled. `inspector.read` reads your actor and entity IDs, health and zone-local transform through public SDK services. Missing providers, no player, invalid data and unknown maximum health are reported as unavailable or unknown, never as zeroes. It changes nothing and binds no hotkey. The Lua example under its own `inspector_lua` ID is not a default. See [SDK workflows](sdk-workflows.md) and the [plugin README](../plugins/inspector/README.md).

### The creative plugin

Noclip, god mode and infinite ammo are not part of `dinput8.dll`. They are the `data/plugins/creative` plugin, built only from the sco SDK's headers on sco-core's `game.creative` service. Its source is the [`sc-creative`](https://github.com/scuba-sc/sc-creative) repository, carried here as the `external/sc-creative` submodule (CMake target `sc-offline-creative`). The folder ships **enabled**. Switch it off in the launcher's Plugins page; it needs `plugins = on`, and a change applies at the next game start. Unloading the plugin switches every toggle it turned on off again.

| Command | Does |
| --- | --- |
| `creative.god <on>` | God mode on or off (held every half second, so it also applies after you respawn) |
| `creative.noclip <on>` | Noclip on or off |
| `creative.noclip_speed <speed>` | Noclip speed in metres per second (1 to 10000; the slider is 1 to 500). The game's own speed returns when the plugin unloads. |
| `creative.ammo <on>` | Infinite ammo for the weapons you carry |
| `creative.ship_ammo <on>` | Infinite ammo for every weapon on the ship you are aboard |
| `creative.status` | Which toggles are on |

The **Creative** tab holds all of these controls; the Player and Vehicles pages have none. If you used the built-in versions before: the commands were `ammo.infinite` and `ammo.ship_infinite` (a plugin's commands must start with its id), god mode no longer starts on, and the ship toggle covers the ship you are aboard but not the Crew page's target ship. Each toggle needs its capability: `game.creative.god_mode`, `game.creative.fly`, `game.creative.ammo`, `game.creative.ship_ammo`.

## Multiplayer (LAN or VPN)

Multiplayer lives in [sc-mp](https://github.com/scuba-sc/sc-mp) and is not in the release. This section describes its design; co-presence is planned, not released ([SECURITY](../SECURITY.md)).

Everyone runs their own offline game. One player hosts a private session from the **Multiplayer** tab and the others join by address. Other players appear in your game as **ghosts**: stand-ins your own game spawns and moves to where they are. Nothing connects until you press **Host** or **Join**. The game's own netcode and servers are not used or changed; the session runs on sco-core's `sco.net`, between sc-offline games only.

- **Host.** Open the menu (**M**), then **Multiplayer**. Enter the name the others will see and a passphrase of at least 8 characters. Keep the UDP port (64091) unless something else uses it, and press **Host**. Give the others your LAN address (`ipconfig` lists it as IPv4 Address, like `192.168.1.20`), the port and the passphrase.
- **Join.** On the same tab, enter your name, the host's passphrase, address and port, then press **Join**. A wrong passphrase, a full session or no answer is shown under the session state. A session holds up to 8 players.
- **Where it works.** Your local network only: `10.x`, `172.16-31.x`, `192.168.x` and link-local addresses. For friends elsewhere, use a VPN that puts you on one network and add its range to `multiplayer_allow` in `sc-offline.ini` (Tailscale: `multiplayer_allow = 100.64.0.0/10`). Every address in that range can reach a session you host, so allow only your VPN's range. With `block_network` on, the firewall rule for `StarCitizen.exe` leaves those networks open and lets PCs on your subnet reach the game over UDP; the internet stays blocked ([PC changes](launcher.md#pc-changes)). Messages are signed with the passphrase, so nobody without it can join or change them, but they are **not encrypted**. They hold only your chosen name, the NPC and ship classes you show as, and where you are.
- **What you see.** The tab lists everyone in the session and how each ghost is doing: streaming in (a fresh spawn takes a few seconds), shown, **not shown** while that player is more than 10 km away, or **parked** when they are in a zone you have not loaded (another system, say). A parked ghost is moved out of sight and returns when you are in the same place. A player aboard a ship shows as that ship when its class can be read and spawns in your game (**Show other players' ships** turns this off). Otherwise they show as an NPC: the class they picked under **Others' ghosts** when your `npcs.txt` has it, else your own pick (default: an unarmed civilian). **Go to** teleports you next to a ghost.
- **Limits.** Ghosts are visual only: no shared physics or collisions you can rely on, no damage, no shared missions, contracts, inventory, wallet or loadouts, and no voice or chat. Each player's world (NPCs, ships, build pieces) stays their own; only the player and the ship they are aboard are shown to others. A ghost's pose follows about 0.1 s behind. Sessions use IPv4.
- **Settings.** `data/storage/multiplayer.db` keeps them. The passphrase is kept only when you tick **Remember the passphrase** (stored unencrypted) and is never written to `mod.log`. `multiplayer = off` in `sc-offline.ini` removes the tab. `mod.log` lines start with `[multiplayer]` and `[net]`.

## Bridges (optional)

Two plugins link the game to another game on the same PC through sco-core's `sco.ipc` (local shared memory for your Windows user, nothing over the network): **TitanLink** (`titanlink`: Titanfall 2 through Northstar, F9 starts pilot mode, the **Titanfall** tab) and the **voxel bridge** (`voxel_bridge`: a Minecraft-style voxel game whose solid blocks become crates, Ctrl+F9, the **Voxel** tab). They are **not in the release**. Build them separately from `plugins/titanlink` and `plugins/voxel_bridge`; the old `SCO_BRIDGE_TITANLINK` and `SCO_BRIDGE_VOXEL` host options are retired and fail configuration. A bridge does nothing until you open it, and each needs its other half in the other game, which this repository does not ship. See [bridges.md](bridges.md).

## Not available offline

These need RSI's servers, and the mod does not replace them:

- Character creation and customization. The main menu is skipped. Not planned ([#24](https://github.com/scuba-sc/sc-offline/issues/24)).
- The mobiGlas vehicle manager lists stored and retrieved ships but cannot select them. Use a ship terminal ([above](#ship-terminals-hangars-and-atc)) or the **Vehicles** page.
- Your account's ships, items and hangar.
- Choosing a spawn location. The spawn plugin refuses destination startup, and `start = Daymar` does not bring back the old timed arrival or boarding ([launcher.md](launcher.md#sc-offlineini)).
