# What is not verified in game

## Inspector SDK 2.4.0 candidate

The candidate-only inspector expansion has local synthetic native tests and an
external hostd integration test. It does not change published SDK defaults.
Manual snapshots, inventory pages, actor stats and action/parameter metadata
remain provider-reported, not game-verified. The unchanged browser studio was
built and exercised through connected Edge at 1440x900 and 390x844, including
manual refresh, paging, lookup, parameter browsing and provider withdrawal.
No in-game renderer or gameplay acceptance is claimed. Official SDK publication and published-archive consumer
revalidation remain required before distribution pins move to 2.4.0.

## v0.7.8.1 offline startup

Observed once, on one Windows test PC (game 4.10.196.36804, launched through `sc-offline.exe play` with the game running as administrator): `[g1] evidence: session=Offline officialNetwork=Clear antiCheat=Clear target=Clear`, then every offline patch line as in v0.7.7 (including `offline-force (IsOnline = 0): patched` and `local handshake (accept frontend connection): patched`), `[core] signatures: 316/316 OK`, the game quit hook installed, and `bOnline[0]` in `Game.log`. That session had no desktop, so the game did not render or reach the menu.

Observed a second time by the maintainer, on a desktop session with the game running with **normal rights** (v0.7.8.1 release DLL, sha256 `47e89459...`): the same evidence line (`profiles=6 rules=734`), every offline patch, `[core] signatures: 316/316 OK`, eight plugins loaded, and `Game.log` reaching `Requesting game mode PU_All/SC_Default` and `Loading level megamap` with `bOnline[0]`. So the firewall read works without administrator rights on that PC.

**NOT VERIFIED:** play beyond loading in; contracts on the v0.7.8.2 host engine (listing, accept, completion, rewards); every feature below.

## v0.7.8.3 build plugin

`data/plugins/build` ships enabled from v0.7.8.3. `tools/check_build_sdk.py` builds it against the published `sdk-v2.3.0` archive, checks its compiler dependencies and boundary negatives, and runs the SDK's `sco-plugin-check` with no game services: place, undo, clear and status answer, and the interactive commands (`build.toggle`/F6, `build.aim`, `build.drop`) refuse. That is a development-machine check, not a game run.

**NOT VERIFIED:** the build plugin in game. Owed: `build.place` with a class and with a `.socpak` path actually grounding an object ahead of the player, `build.undo` and `build.clear` actually removing what was placed (and only that), the plugin loading from the release folder on Windows, unload removing placements, and F6 refusing cleanly. Interactive build mode and the free camera stay unavailable until a production G5 adapter exists; placements are not kept across reloads.

## 0.7.8.3: teleport and quantum travel

`data/plugins/teleport` and `data/plugins/quantum` are staged enabled. `tools/check_teleport_sdk.py` and `tools/check_quantum_sdk.py` build them against the published `sdk-v2.3.0` archive, run the synthetic travel-model test and the quantum actual-module workflow (player switch, `game.relocation` capability, one request, tick tracking to complete and failed), trace compiler dependencies, run boundary negatives and a relocation red arm, and run the SDK's `sco-plugin-check` with no game services, where every move answers unavailable. That is a development-machine check, not a game run.

**NOT VERIFIED:** any move in game. Owed: F7 then F8 returning to the saved spot; `quantum.travel` arriving at the destination radius plus the altitude (placed along the destination zone's own +Z axis, never checked); `quantum.bookmark`; the arrival, failure and cancellation messages; the plugin `relocation` setting and `sc-offline.ini` `relocation = off` refusing moves; both plugins loading from the release folder on Windows. Moves need this release's host (sco-core main e616ed6 or later) with offline admission, resolved engine rows, a live player and a known star system; without them they answer unavailable.

**NOT VERIFIED:** the host switch in game. It is compiled in with this pin (CMake defines `SCO_OFFLINE_RELOCATION_SWITCH` because the pinned sco-core declares `SetRelocationEnabled`). It was syntax-checked with the define but not run in game.

## Optional co-presence module

Its source is maintained in [scubamount/sc-mp](https://github.com/scubamount/sc-mp);
sc-offline does not build or ship it.

## Admission evidence

Since v0.7.8.1 the shell reports offline-session and network
evidence from live firewall and connection reads (see above); it still never infers
safety from flags or patch success.

The host is pinned to sco-core `main` at commit `e616ed69d8f9f77b0f4827881c86d91e4a7d1689`, after the published `sdk-v2.3.0` (it adds the `game.relocation` production path and the `game.quantum` backend; the external plugins build against the published `sdk-v2.3.0` archive): SDK baseline **2.3.0**, plugin ABI **1.1** (unchanged), Star Citizen game pack **0.6.0** (the same version the published `sdk-v2.3.0` archive carries). The game pack's signature evidence at this pin targets Star Citizen **4.10.196.36804**; it is not a game-session result for this product pin.

**Not adopted by this product yet:** sco-core `main` has moved past this pin. It now records signature rows resolved on Star Citizen **4.10.197.2878** and has merged the `game.actor_stats` read and write services (the write service is off pending in-game confirmation; stat units, range and direction are not run in game). sc-offline neither pins nor ships those, so nothing here claims support for 4.10.197.2878 or for actor stats.

The release-pin smoke checks: offline startup was **observed once** for v0.7.8.1 (above); EAC detection/refusal, services published before plugins, menu and clean exit remain **NOT RUN** in game. CI build success does not close these owner-run checks.

**The only game session reported since the SDK conversion is one maintainer run of v0.7.7** (sco-core `2197dfb`, game pack 0.2.0, NVIDIA Smooth Motion off), recorded in the [v0.7.7 release notes](https://github.com/scubamount/sc-offline/releases/tag/v0.7.7). It was a single run, not a per-feature check, and the newer pins (`main` on `sdk-v2.0.0`, PR #126 on `sdk-v2.1.0`) have not run in game. CI proves the code compiles, the gates hold, and the binaries have the right shape. It does not prove a feature works. The current owner decision is CI-gated PR merge and user testing of official
releases; owner Windows/in-game acceptance remains **NOT RUN**. No synthetic
fixture or checker result certifies gameplay. Policy: [decisions.md](decisions.md).

sco-core keeps the matching table for the `game.*` services, with the evidence level of each capability and what a run has to show: [docs/game/status.md](https://github.com/scubamount/sco-core/blob/e616ed69d8f9f77b0f4827881c86d91e4a7d1689/docs/game/status.md) at the pinned commit. Read it for the service side (for example `game.actors.state`, `game.entities.query_radius`, `game.entities.watch` and `game.events.vehicle_seat` are held **off** until a run decides them). This page lists the sc-offline side: what each converted feature still needs checked. The steps are the ones in the bodies of [#95](https://github.com/scubamount/sc-offline/pull/95), [#97](https://github.com/scubamount/sc-offline/pull/97) and [#98](https://github.com/scubamount/sc-offline/pull/98); `mod.log` lines are quoted as those PRs wrote them.

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
stages nine external native IDs (spawn, crew, build, npc, inspector, teleport, quantum, creative, discord).
Loadout has an external implementation not yet staged
there, and multiplayer is unregistered at this revision: 13 baseline IDs remain.
Two optional bridge modules and supporting data packs are counted separately.
Older command fixtures used published SDK 2.0.0, not engine evidence.

**Picker restoration (SDK 2.3.0):** external spawn, crew and npc own
the Vehicles, Crew and NPCs panels through public `sco.widgets`. Shared public
`lists/ships.txt` and `lists/npcs.txt` catalogs retain pack/row order and duplicate
identity; filtered pages and manual classes are available. The supporting
`picker_catalog` data pack stages copies of the existing base lists; originals
are unchanged. The host renders neutral copied panels on its game-thread frame.
The one `picker-module-workflow` fixture dynamically loads all three actual
macOS modules and checks UI events against synthetic public providers; it is
not renderer/game evidence. The pinned published `sdk-v2.3.0` ships widgets
and content (`sdk-v2.0.0` did not); Windows rendering and game acceptance remain owed.
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
