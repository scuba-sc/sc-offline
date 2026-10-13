# What is not verified in game

The host is pinned to published sco-core `sdk-v2.0.0`, commit `cd9a686bda4184f1befbdf0b74b503a1565540dd`: SDK **2.0.0**, plugin ABI **1.1** (unchanged), Star Citizen game pack **0.3.0**. The game pack's signature evidence targets Star Citizen **4.10.196.36804**; it is not a game-session result for this product pin.

The release-pin smoke checks remain **NOT RUN** on Windows/in game: offline startup; EAC detection/refusal; services published before plugins; menu; clean exit. CI build success does not close these owner-run checks.

**Nothing from the SDK conversion has run in a game session.** CI proves the code compiles, the gates hold, and the binaries have the right shape. It does not prove a feature works. The last in-game run of sc-offline itself was 0.6.1, before the conversion ([README](../README.md)). The current owner decision is CI-gated PR merge and user testing of official
releases; owner Windows/in-game acceptance remains **NOT RUN**. No synthetic
fixture or checker result certifies gameplay. Policy: [decisions.md](decisions.md).

sco-core keeps the matching table for the `game.*` services, with the evidence level of each capability and what a run has to show: [docs/game/status.md](https://github.com/scubamount/sco-core/blob/cd9a686bda4184f1befbdf0b74b503a1565540dd/docs/game/status.md) at the pinned commit. Read it for the service side (for example `game.actors.state`, `game.entities.query_radius`, `game.entities.watch` and `game.events.vehicle_seat` are held **off** until a run decides them). This page lists the sc-offline side: what each converted feature still needs checked. The steps are the ones in the bodies of [#95](https://github.com/scubamount/sc-offline/pull/95), [#97](https://github.com/scubamount/sc-offline/pull/97) and [#98](https://github.com/scubamount/sc-offline/pull/98); `mod.log` lines are quoted as those PRs wrote them.

## Step 7B: tutorial evidence ledger

[Author-owned tutorial copies](../examples/sdk-mcp/README.md) compile as native
macOS development modules from an isolated checksum-verified local `integ/r1`
SDK archive. This is not published-archive or gameplay certification. Checker,
dynamic-load fixture, MCP transcript and stale/session negatives: **NOT RUN**
(owner chose compile-only speed). Windows/MSVC, runtime host/session acceptance,
installation, reload and actual provider cleanup: **NOT VERIFIED**. Published
SDK pin and 13 baseline + 2 optional feature inventory are unchanged.

## Steps 1D/2A/2B: external features — Windows/game acceptance NOT RUN

Crew and ordinary spawn ship as enabled external folders; their host registrations,
linked UI and tick fallbacks are removed. Creative stays off; Discord stays off and
refuses presence pending approved transport/privacy policy. The current host registry has two built-ins (contracts, mining); the release recipe
stages six external native IDs (spawn, crew, npc, inspector, creative, discord).
Teleport, quantum, build and loadout have external implementations not yet staged
there, and multiplayer is unregistered at this revision: 13 baseline IDs remain.
Two optional bridge modules and supporting data packs are counted separately.
Older command fixtures used published SDK 2.0.0, not engine evidence.

**Picker restoration (local integration SDK):** external spawn, crew and npc own
the Vehicles, Crew and NPCs panels through public `sco.widgets`. Shared public
`lists/ships.txt` and `lists/npcs.txt` catalogs retain pack/row order and duplicate
identity; filtered pages and manual classes are available. The supporting
`picker_catalog` data pack stages copies of the existing base lists; originals
are unchanged. The host renders neutral copied panels on its game-thread frame.
The one `picker-module-workflow` fixture dynamically loads all three actual
macOS modules and checks UI events against synthetic public providers; it is
not renderer/game evidence. The unchanged published `sdk-v2.0.0` lacks widgets
and content: publication/pin promotion and Windows rendering remain owed.
SQ42 ordinary ship shortcuts invoke `spawn.ship_options`; enemy-wing orchestration
is deferred. Destination startup refuses G2/G3 without a private fallback. Owner
checks still owe real classes/large seat lists, delayed streaming, board/power,
owned cleanup, disable/reload, offline/EAC refusal, rendering/DPI and MSVC.
Legacy `src/npc.cpp` and unrelated build/fleet spawner callers remain unchanged.

## Historical step 1C: external NPC acceptance — NOT RUN

NPC ships enabled in `data/plugins/npc/{plugin.ini,npc.dll}`, with no `disabled`
marker and no host linkage or built-in fallback. Current counts and release
staging are reconciled above, not the superseded step 1C nine/four split. The
external NPCs and Crew panels now use public widgets and the same catalog parser;
actor ownership remains separate. Legacy `src/npc.cpp` is untouched and is never
a fallback. These are owed checks, **not executed outcomes**:

- Record exact game build, host/SDK/plugin versions and chosen real archetype.
  Start through the offline launcher; confirm one external `npc` load and one
  registration per command and one external NPCs panel when widgets are available.
- Before player spawn or with missing capability/provider, commands report
  unavailable/unknown rather than guessed pose or private fallback. Fresh calls
  reopen services, with no retained provider table or stale position/zone.
- `npc.spawn <archetype> 3` logs session IDs immediately as pending. After stream-in,
  `npc.observe` reports visible transforms; missing/invalid transforms are unknown.
  A partial batch retains already spawned IDs and reports the failing result.
- `npc.remove <owned-id>` before stream-in and after visibility requests removal
  once. `SCO_OK` is only removal started; `alive=0` never certifies disappearance
  or recycles a ticket. `npc.clear` affects only its remaining owned IDs, not crew
  or foreign NPCs. Foreign/high-bit IDs retain their exact identity and cannot
  widen mutation authority. Capacity remains bounded per feature load.
- Normal unload withdraws commands, requests cleanup once, and releases remaining
  owner-scoped state. Repeated unload is inert. Check actual provider owner-release
  cleanup; a successful synthetic unload is not game cleanup evidence.
- Disable the NPC folder and restart; separately set `plugins = off`: NPC commands
  and its panel are absent and no host fallback or hidden tab runs. Restore the enabled folder
  and confirm commands register once. Crew's picker/seat behavior remains available
  but its separate ownership regression is not certified by this step.
- Normal `sco.reload npc` is an external-consumer route only, not provider hot
  reload or proven gameplay behavior. For a guarded native fault, expect disabled
  owner-scoped routes and **restart-required** state; do not claim safe reload,
  recovery after corruption or process-termination cleanup.

The [NPC tutorial](sdk-workflows.md#worked-tutorial-standalone-native-npc) separates
published-archive/checker/synthetic evidence from this Windows/in-game run.

## Step 1B: external inspector acceptance — NOT RUN

Inspector is now an enabled external native folder, not a production built-in:
`data/plugins/inspector/plugin.ini` and `inspector.dll`, no `disabled` marker.
The current counts are listed above; NPC is external too. The [tutorial](sdk-workflows.md) separates
published-archive/checker/synthetic proof from the following owed game checks:

- Record the exact Windows host revision, plugin version and game build; verify
  service publication precedes inspector load and no duplicate-ID refusal occurs.
- Invoke `inspector.read` before player spawn and after spawn. Unavailable
  providers/player fields stay unknown; zero maximum health is not a denominator.
- Confirm the command does not mutate game state or require rendering/UI.
- Run `sco.reload inspector`: old owner-scoped command callbacks are withdrawn,
  a fresh load registers the command once, and each read reopens services.
- Disable the folder and restart, then separately test `plugins = off`:
  inspector is unavailable and no hidden host fallback runs. `plugins = off`
  also prevents external NPC loading.
- Restore the enabled folder; confirm native inspector loads. If exercising Lua,
  use its non-default `inspector_lua` ID; do not count it as another baseline
  feature or release default.

Published SDK checks validate manifest/module behavior under a stand-in host;
synthetic module tests cover public-service branches. Neither proves native
Windows game loading, actual engine fields or owner-run reload safety.

The lists below preserve earlier acceptance requests, not executed outcomes.
Built-ins cannot be hot-reloaded. The old build reload request is a future
external-plugin acceptance case, not a current built-in capability. NPC now has
an external-consumer reload route; game acceptance is still NOT RUN.

## From #95 (the SDK conversion, merged)

Each converted feature has a deferred checklist in the PR's body. In short:

- **`teleport.spatial` (#82).** `mod.log` has `[game] game services published: ...` before the `[plugin]` lines and no `teleport.spatial` error. F7 saves a spot, F8 returns to it. `spawn_probe` Ctrl+Alt+3 logs your pose, zone name and `entity_alive`.
- **`spawn.entities` 1.2 (#89).** The `spawn` built-in loads with no error. `spawn_probe` Ctrl+Alt+1 spawns a ship (`spawn.ship`); Ctrl+Alt+2 walks the mover 3 steps, then 1 each after stream-in; a refusal for an entity you don't own is logged as `[game] warning: ...`. The Vehicles tab spawns a ship.
- **Rows (#85).** No failed signature rows (300/300 resolved on 4.10.196.36804 against sco-core at that time) and no `[missions] the mission manager's script library wasn't found`. A contract runs as before (the library comes from `sco::game::missions::ScriptLibrary`). A ship, an NPC, a build prop and a quantum jump each work as before.
- **NPCs on `game.actors` (#90, historical built-in request).** Superseded by the external lifecycle checklist above; the NPCs panel belongs to the external module, with no built-in reload or crew-wide clear route. The `game.actors` state probe (`state` is off) still requires observing downed/dead P1 and P2 values.
- **Crew on `game.vehicles` (#91).** On `AEGS_Javelin`, `spawn.ship` then `crew.fill <npc>`: one `interactable yes|no` line per seat, then `Seating U x <npc> ... (S seats skipped: not interactable)`, with no `couldn't seat` lines (the expected split on the Javelin is 10 seated, 5 skipped, if the seat-owner hypothesis holds). On a small two-seat ship every empty seat seats; `crew.stand_all`, `crew.clear`, `crew.sit pilot` and `crew.power_on` each answer OK.
- **Build props on `game.entities` (#94).** `spawn_probe` Ctrl+Alt+= logs spawn, `get_transform`, `set_transform`, `class_of`, `alive`, the refusal for an entity you don't own, and `despawn`. In build mode (F6) the preview follows your aim without a `[game] warning` per tick, left click places, Backspace undoes, Clear base removes everything; a `.socpak` and a guard still spawn through the spawner. In a future external-build reload acceptance case (the current built-in cannot be hot-reloaded), placed props must stay in the world, the preview goes, and Undo and Clear base still remove the kept props (`[game] game.entities: build kept <id>: ...` for each).
- **Natural mining (#93).** With `mining = on`: `[mining] natural mining active`, `mining.status` counts cells up with promoted above 0, natural Titanium (Ore) is highlighted in mining mode, scanning shows its composition, the mining laser responds, extraction puts ore in cargo. Leaving and returning, depletion and persistence across restarts were never verified, even by the fork this is ported from.
- **Launcher (#92).** The D3D11/ImGui window at 100% and 150% DPI; every command still works; the Plugins page against a real `data/plugins` (the `disabled` marker is honoured at game start; the `plugins = off` banner and **Turn on**); a plugin's **Save** writes `data/storage/<id>.db` and the plugin reads the value in game (the host accepts the launcher-created `sco_kv` table), **Reset** removes it; Settings, Logs and the update check; `mining`, `mining_debug` and the other ini keys are still read.

## From #97 (`game.world` consumers, merged)

- `mod.log` shows no `[build] game.world isn't available`, no `raycast isn't ready`, no `camera isn't ready`.
- F6: the free camera works and the preview follows the cursor from the `game.world.camera` position, without jitter or climbing toward you (the aim ray is unchanged).
- `build.place <object> 5` on a slope and indoors lands on the floor or ground, not floating or in a wall; `build.place <object> 0` lands under you.
- The Squadron 42 tab's Spawn lands on the ground; the restored external NPCs panel uses `npc.spawn` semantics with no ground ray; a contract that places things on the ground, and the voxel bridge if built, are unchanged.
- With `game.world` unavailable, the same actions work and `mod.log` says why, once.
- Not done in #97, so not a check: `game.events` (the timing of teleport's "player ready" wait and build's "not spawned yet" checks) is not used; the aim ray stays on the `build.*` rows.

## From #98 (SDK-headers gate and the creative plugin)

#98 is open at the time of writing. Nothing below has run in game or on MSVC; its CI gates (`sdk-headers`, `no-scans`) run on the PR.

1. Enable `creative` in the launcher's Plugins page (with `plugins = on`); `mod.log` shows `[plugin] loaded creative`.
2. `creative.god on`: no damage; `creative.god off` restores it.
3. `creative.noclip on` with `creative.noclip_speed 100`: you fly through geometry at that speed; off returns to walking.
4. `creative.ammo on`: a carried weapon's magazine doesn't drain.
5. `creative.ship_ammo on` aboard a ship: the ship's weapons don't drain.
6. Disable or unload the plugin and restart: all five are off and the fly speed is the game's own again.
7. The Player and Vehicles tabs' checkboxes drive the same commands, and revert with a status message ("Needs the creative plugin") when the plugin is off.
8. Verify launcher update behavior against saved enable choices. The earlier
   request expected the release's `disabled` marker to turn creative off again;
   this is not evidence of a tested result or approval to reset user choices.
   [Current decisions](decisions.md) leave updater/default reconciliation and
   preservation of enable choices open, separate from step 1C.

The local checks recorded for #98 were a compile of the built-ins and the plugin with zig/mingw, and `tools/sdk-headers.py`. MSVC (`/W4 /WX` on the plugin target) and `tools/check.sh` were not run for it by its author; CI runs them.

## From the older upstream list

ChrisWareOffline 0.9.0-rc1 flagged these as built after its last in-game test and they are still open: the energy-weapon top-up, NPC deletion through the entity handle, Stand up, the real-building prefab preview, and Pyro and Nyx names after a fresh scan. Not available offline at all: see [features.md](features.md#not-available-offline).
