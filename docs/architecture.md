# Architecture: sc-offline on sco-core

sc-offline is two things on top of [sco-core](https://github.com/scubamount/sco-core): the **host** that loads sco-core's kernel into Star Citizen, and the **reference consumer** of what sco-core publishes. The host builds from the published `sdk-v2.0.0` tag, pinned to sco-core `cd9a686bda4184f1befbdf0b74b503a1565540dd`. SDK **2.0.0**, plugin ABI **1.1** (unchanged), and Star Citizen game pack **0.3.0** are separate versions. SDK 2.0.0 changes packaging and release profiles, not the frozen plugin ABI; the full Windows host still builds from tagged source, not a plugin SDK archive. How to build and what CI checks: [build.md](build.md). What nobody has run in game yet: [status.md](status.md).

## Loadout/SQ42 development boundary

`plugins/loadout` is the external G6/G7 consumer; see its [build and limitations](../plugins/loadout/README.md). The legacy host loadout registration, compilation and fallback tick are removed; inactive sources are retained in `legacy/loadout`. Four SQ42 settings use public real reads and unknown/refusal states, never the separate unapproved cvar-write service. This requires a newer local SDK/host than the unchanged published pin. Optional Player/SQ42 panels use `sco.widgets`, not product ImGui globals. Complete weapon/clothing/head/default parity and production typed engine adapters remain pending. Step 4C quantum migration is not approved; private boost/keeper paths are unchanged. Historical feature/default prose below predates this development boundary.

## Who owns what

| Layer | Owns | Lives in |
| --- | --- | --- |
| **sco-core kernel** | The plugin loader, `sco_api`, `sco.ui`, `sco.storage`, `sco.net`, `sco.ipc`, `sco.settings`, hot reload (`sco.reload`), the detour engine (`sco/hook.h`), the CryPak/DataCore pack loader | `external/sco-core` |
| **sco-core game pack** | The game's signature rows (`sco/game/*.h`, each address found once, by one unique pattern) and the `game.*` services built on them: `teleport.spatial`, `spawn.entities`, `game.actors`, `game.vehicles`, `game.entities`, `game.world`, `game.creative` | `external/sco-core` |
| **sc-offline** | The `dinput8.dll` proxy, the offline patches, the main-thread hook, the menu shell, the launcher, the nine remaining built-in plugins, the data lists, and the optional `creative` plugin | `src/`, `launcher/`, `plugins/`, `data/` |

Under `src/` nothing finds a game address by pattern: `tools/no-scans.sh` fails the build if anything there calls a raw scanner (`FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch`, `memchr`). Every address comes from a sco-core row, so a game patch that moves code is fixed in one place, sco-core, which is checked against each game build.

## Startup

On the main-thread hook's first tick, `dllmain.cpp` calls `sco::app::Start`. In order: capabilities (`SetFeatureCaps`, which turns each feature's capability on from its resolved rows), the `sco_api` table, the game services (`Platform::gameServices`; `mod.log` prints `[game] game services published: ...`), the built-in plugins in the order of `kBuiltins` in `src/builtins/builtins.h`, then the plugins in `data/plugins/` if `plugins = on`. That happens whether or not a feature resolved on this game build: a feature whose rows are missing loses its capability and its commands, and the rest still starts.

## What each feature runs on

| Feature | Runs on | Notes |
| --- | --- | --- |
| Save / go to a spot (F7, F8) | `teleport` built-in; capability `teleport` | `teleport.spatial` 1.0 is published by the game pack, not by this built-in |
| Spawn a ship (Vehicles tab, `spawn.ship`) | `spawn` built-in over the spawner engine (`src/spawner.cpp`) | `spawn.entities` 1.2 is published by the game pack; ASOP registers a retrieved ship with the game pack's mover (`RegisterPlayerVehicle`) |
| Crew commands (`crew.*`) | `game.vehicles` 1.0, `game.actors`, and `teleport.spatial` for the pose | Gated on `game.vehicles.seats`, `game.vehicles.seat`, `game.vehicles.flight_ready`, `game.actors.despawn`. The Crew **tab** still runs on the spawner's seat code |
| NPCs | External native SDK consumer: `game.actors` lifecycle, `teleport.spatial` pose and `game.entities` observation | `data/plugins/npc/` ships enabled; `npc.spawn`, `npc.observe`, `npc.remove`, `npc.clear`; removal is owned-only, never crew-wide; no NPC tab or fallback; [SDK workflows](sdk-workflows.md) |
| Read-only inspector | External native SDK consumer using `game.actors` and `game.entities` reads | `data/plugins/inspector/` ships enabled; `inspector.read` logs explicit unknowns. Command-only; restricted Lua uses the non-default `inspector_lua` ID and is not another baseline feature |
| Build props | `game.entities`: `spawn`, `set_transform`, `despawn`, and `keep()` (1.1) for placed props | Prefabs (`.socpak`) and NPC props still use the spawner. Without `keep()` (a 1.0 game pack) props stay owned by the plugin |
| Build: ground ray for placement, build camera | `game.world.raycast`, `game.world.camera` | The aim ray that moves the preview stays on the `build.*` rows: it must skip the preview prop and `raycast` 1.0 has no skip list |
| Noclip, god mode, infinite ammo | The optional `creative` plugin on `game.creative` 1.0 | Not in `dinput8.dll`; see [below](#built-ins-and-optional-plugins) |
| Natural mining | `mining` built-in on the `game.mining` row (`mining.cell`) and a `sco::hook` detour | Off unless `mining = on` |
| Gear, outfits, quantum travel, contracts | Built-ins gated on `loadout`, `quantum`, `contracts`; sco-core rows, no scans | Their engines are still sc-offline's own (`src/loadout.cpp`, `quantum.cpp`, `contracts.cpp`); no `game.*` service covers them yet |
| Multiplayer | `multiplayer` built-in on `sco.net`, `teleport.spatial`, `spawn.entities` | Gated on `sco.net` |
| Gladius quantum drive | A data pack applied by sco-core's DataCore loader | `data/builtin/quantum/` |
| Titanfall 2 and voxel bridges | `sco.ipc`, optional builds only | [bridges.md](bridges.md) |

## Built-ins and optional plugins

**Built-ins** are compiled into `dinput8.dll`: `teleport`, `spawn`, `crew`, `loadout`, `quantum`, `build`, `contracts`, `mining` and `multiplayer` (plus `titanlink` and `voxel_bridge` in the optional bridge builds). Each has a host-only `sco::plugins::Builtin` registration, a table of the three plugin functions instead of DLL exports, and talks through `sco_api` like any plugin. They load first, with `plugins` on or off, they own their id and command prefix (a plugin folder with one of those ids is refused), and the host cannot reload them (`sco.reload` answers that a built-in cannot be reloaded).

**Optional plugins** are folders under `data/plugins/<id>/` with a `plugin.ini`. They load only with `plugins = on` in `sc-offline.ini`. A plugin is off when its folder holds an entry named `disabled`; the launcher's Plugins page creates and deletes that marker, and a change applies at the next game start. The Plugins page also edits a plugin's `[settings]`. **Hot reload** (`sco.reload <plugin>`) works only inside the running game; the launcher has no channel into it.

The release ships four external feature folders: NPC, inspector, Discord and creative. **`npc`** (`plugins/npc/`, isolated CMake target `sc-offline-npc`) builds against the verified published SDK, ships **on** without a `disabled` marker, and has no host linkage, registration or fallback. Its four lifecycle commands are command-first; the old NPC tab is removed pending renderer-neutral widgets ([sco-core #139](https://github.com/scubamount/sco-core/issues/139)). Crew keeps its shared picker; legacy `src/npc.cpp` is untouched. **`inspector`** (`plugins/inspector/`, isolated CMake target `sc-offline-inspector`) uses only published public headers, ships **on** without a `disabled` marker, and has no host registration or fallback. See the [published SDK tutorial](sdk-workflows.md). **`discord`** (`plugins/discord/`, standalone CMake target `sc-offline-discord`) is blocked pending approved host transport and field review. Its folder metadata is **off**; manually enabling it exposes only refused `discord.status` and inactive `sco.settings` preferences, with no game reads or custom IPC/network. Shared release/CI wiring remains an integration task; see [Discord review](../plugins/discord/README.md). **`creative`** (`plugins/creative/`, CMake target `sc-offline-creative`) is `requires = game.creative` and ships with its `disabled` marker, so it is **off** until you turn it on. Its commands are `creative.god`, `creative.noclip`, `creative.noclip_speed`, `creative.ammo`, `creative.ship_ammo` and `creative.status`; the Player and Vehicles tabs' checkboxes run them. Details: [features.md](features.md#the-creative-plugin).

## The CI gates

The host gates and isolated SDK-plugin jobs run on every pull request and every push to `main` ([build.yml](../.github/workflows/build.yml)); `release` runs on `v*` tags only.

| Job | Checks |
| --- | --- |
| `check` | `tools/check.sh` (clang parse against mingw-w64 headers, MSVC C2712 screen, baseline in `tools/check-baseline.txt`), the release-manifest tests, and that `src/third_party/imgui` matches the ImGui headers the SDK ships |
| `build` | Release x64 host with MSVC; binaries checked with `dumpbin` and `mt`. NPC and inspector are built in separate SDK jobs against the checksum-verified published archive and staged for the release, not linked or registered in the host |
| `bridges` | The same build with both bridge options on, only so the bridges keep compiling |
| `no-scans` | No raw game scan under `src/` |
| `sdk-headers` | Every file under `src/builtins/` and `plugins/` includes only SDK headers, its own files, or a header named in `tools/sdk-headers-allow.txt` |

### What the `sdk-headers` allow-list means

The gate makes `src/builtins/` and `plugins/` consumers of the SDK zip's public headers. `plugins/creative` passes with **no** allow-list entry. The built-ins do not: the list names every non-SDK header a built-in includes, one reason per line, and it is long because **the built-ins still call sc-offline's own engines** (`src/spawner.cpp`, `build.cpp`, `teleport.cpp`, `loadout.cpp`, `quantum.cpp`, `contracts.cpp`, `npc.cpp`, `cvars.cpp`, the menu shell, the multiplayer session), or use the kernel directly where the SDK has no form yet (`sco/hook.h` and the `game.mining` rows for mining, `sco/net/session.h` for multiplayer). So the gate does **not** mean the built-ins are pure SDK plugins. It means nobody can add a new dependency unnoticed: a new include of a non-SDK header fails CI, the fix is a `game.*` service in sco-core and not a new line in the list, and a line whose includes are gone fails too, so the list only shrinks. The crew plugin still uses the built-in shell and shared NPC archetype picker. NPC and inspector sources live under `plugins/{npc,inspector}/`, with no host-header exceptions. Neither has host linkage or production built-in registration; `src/workflow_adapters.cpp` is removed. The external modules compile neither product UI nor new ImGui inputs. Legacy `src/npc.cpp` remains unchanged for remaining host consumers. See [SDK workflows](sdk-workflows.md).

## Migration boundary

The [owner decisions](decisions.md) retain **13** baseline feature units and **2** separately counted optional bridges. The eventual shell has zero feature built-ins and explicit folder enable semantics, not hidden fallbacks. Steps 1B/1C externalize inspector and NPC; nine built-ins still load with `plugins = off`. Four external features plus those nine preserve the 13-feature denominator, not 13/13 migration completion. Creative stays off. Step 1D separately makes Discord folder metadata off and refuses presence pending privacy/transport approval. Future renderer-neutral UI, host-source packaging and runtime diagnostics are tracked upstream, not claimed as available in SDK 2.0.0. The launcher and EAC/online refusal policy are unchanged. Normal consumer reload is not provider reload or gameplay certification; native faults require restart, not safe reload/recovery.

## Historical notes

sco-core's `docs/framework.md` ("Phase 4") and sc-offline's CHANGELOG entries up to 0.7 describe the move to built-in plugins one feature at a time, with `teleport.spatial`, `spawn.entities` and the cheats first living in this repository. That plan is **historical**: the services moved into sco-core's game pack and the cheats into the `creative` plugin. Read those pages for why a thing is shaped as it is, not for where it lives now.
