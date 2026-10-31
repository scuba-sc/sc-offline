# Architecture: sc-offline on sco-core

sc-offline is two things on top of [sco-core](https://github.com/scuba-sc/sco-core): the **host** that loads sco-core's kernel into Star Citizen, and the **reference consumer** of what sco-core publishes. The development host builds from unpublished sco-core #304 source `1b6782bc5c8b7d189cdff4c83e81957f09af35d7`. External plugins consume the checksum-pinned published `sco-sdk-2.4.0.zip` from published RC2: SDK **2.4.0**, plugin ABI **1.1** (unchanged), Star Citizen game pack **0.8.0**. The full Windows host builds from the pinned source, independently of the plugin archive. Candidate adoption does not make RC2 the final SDK 2.4.0 release or certify gameplay. How to build and what CI checks: [build.md](build.md). What nobody has run in game yet: [status.md](status.md).

## Loadout/SQ42 development boundary

`plugins/loadout` is the external G6/G7 consumer; see its [build and limitations](../plugins/loadout/README.md). The legacy host loadout registration, compilation and fallback tick are removed; inactive sources are retained in `legacy/loadout`. Four SQ42 settings use public real reads and unknown/refusal states, never the separate unapproved cvar-write service. Its G6/G7, content and widgets headers ship in the pinned published SDK 2.4.0 (`sdk-v2.4.0-rc.2`); loadout is not yet staged by the release workflow. Optional Player/SQ42 panels use `sco.widgets`, not product ImGui globals. Complete weapon/clothing/head/default parity and production typed engine adapters remain pending. Step 4C quantum migration is not approved; private boost/keeper paths are unchanged. Historical feature/default prose below predates this development boundary.

## Who owns what

| Layer | Owns | Lives in |
| --- | --- | --- |
| **sco-core kernel** | The plugin loader, `sco_api`, `sco.ui`, `sco.storage`, `sco.net`, `sco.ipc`, `sco.settings`, hot reload (`sco.reload`), the detour engine (`sco/hook.h`), the CryPak/DataCore pack loader | `external/sco-core` |
| **sco-core game pack** | The game's signature rows (`sco/game/*.h`, each address found once, by one unique pattern) and the `game.*` services built on them: `teleport.spatial`, `spawn.entities`, `game.actors`, `game.vehicles`, `game.entities`, `game.world`, `game.creative` | `external/sco-core` |
| **sc-offline** | The `dinput8.dll` proxy, the offline patches, the main-thread hook, the menu shell, the launcher, the external feature modules, supporting data packs and remaining legacy engine helpers | `src/`, `launcher/`, `plugins/`, `data/` |

Under `src/` nothing finds a game address by pattern: `tools/no-scans.sh` fails the build if anything there calls a raw scanner (`FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch`, `memchr`). Every address comes from a sco-core row, so a game patch that moves code is fixed in one place, sco-core, which is checked against each game build.

## Startup

The bootstrap prepares the game-pack host composition with live OS admission
evidence before engine mutation. The game pack owns startup, signature resolution,
required quit/DataCore hooks and plugin tick admission. `Platform::gameServices`
enables the public services; the shell supplies no built-in feature inputs.
External plugin folders load when `plugins = on`, subject to their manifests and
capabilities. See [host composition](host-composition.md) for refusal boundaries.

## What each feature runs on

| Feature | Runs on | Notes |
| --- | --- | --- |
| Quick save/return and travel | External `teleport` and `quantum` on `game.relocation` | Requires positive offline admission, resolved rows and a live player; no private movement fallback |
| Vehicles, Crew and NPCs | External `spawn`, `crew`, `npc` on public actor/vehicle/entity/spatial services and `sco.widgets` | Shared public picker catalogs; ownership and delayed operations remain consumer responsibilities |
| Read-only inspector | External `inspector` on public actor/entity reads | Explicit unknowns when providers are unavailable; [SDK workflows](sdk-workflows.md) |
| Build props | External `build` on public entity/spatial services | Interactive camera/aim controls still refuse unavailable providers |
| Noclip, god mode, infinite ammo | External `creative` on `game.creative` | Ships disabled |
| Discord presence | External `discord` refusal module | Ships disabled pending approved transport/privacy review |
| Natural mining | External `mining` on `game.mining` | Requested setting off by default; native adapter unavailable until verified |
| Contracts | External `contracts` consumer | Public contracts/provider source exist; production mutations remain gated on admitted-backend readiness |
| Gear and SQ42 settings | Development `loadout` consumer | Not staged in releases; unsupported slots and writes remain refused |
| Multiplayer | Separate [sc-mp](https://github.com/scuba-sc/sc-mp) consumer | Excluded from the standard release |
| Quantum drive and pickers | Ordinary DataCore/content packs | `data/plugins/quantum_drive`, `data/plugins/picker_catalog` |
| Titanfall 2 and voxel bridges | Independent external consumers of `sco.ipc` | Excluded from the standard release; [bridges.md](bridges.md) |

## Built-ins and optional plugins

The shell registers no feature built-ins. G1 early DataCore refuses built-in
inputs. Unbuilt legacy sources under `src/builtins/` and remaining shared engine
helpers do not provide a registration, linked consumer or feature tick fallback.

**Optional plugins** are folders under `data/plugins/<id>/` with a `plugin.ini`. They load only with `plugins = on` in `sc-offline.ini`. A plugin is off when its folder holds an entry named `disabled`; the launcher's Plugins page creates and deletes that marker, and a change applies at the next game start. The Plugins page also edits a plugin's `[settings]`. **Hot reload** (`sco.reload <plugin>`) works only inside the running game; the launcher has no channel into it.

The release stages 11 external feature folders: creative, discord, crew, spawn,
build, mining, teleport, quantum, inspector, npc and contracts. Creative and
Discord keep their disabled markers; the other nine ship enabled. Modules build
independently from the checksum-verified published SDK, with no host-private
include path. Loadout remains a development consumer. These assembly defaults do
not prove that required production services are available or features work in
game; see [status](status.md) and [candidate limits](sdk-candidate.md).

## The CI gates

The host gates and isolated SDK-plugin jobs run on every pull request and every push to `main` ([build.yml](../.github/workflows/build.yml)); `release` runs on `v*` tags only.

| Job | Checks |
| --- | --- |
| `check` | `tools/check.sh` (clang parse against mingw-w64 headers, MSVC C2712 screen, baseline in `tools/check-baseline.txt`), the release-manifest tests, and that `src/third_party/imgui` matches the ImGui headers the SDK ships |
| `build` | Release x64 host with MSVC; binaries checked with `dumpbin` and `mt`. NPC and inspector are built in separate SDK jobs against the checksum-verified published archive and staged for the release, not linked or registered in the host |
| Optional bridges | Independent configurations; excluded from the standard host and release |
| `no-scans` | No raw game scan under `src/` |
| `sdk-headers` | Every file under `src/builtins/` and `plugins/` includes only SDK headers, its own files, or a header named in `tools/sdk-headers-allow.txt` |

### What the `sdk-headers` allow-list means

The gate makes `src/builtins/` and `plugins/` consumers of the SDK zip's public headers. `plugins/creative` passes with **no** allow-list entry. The built-ins do not: the list names every non-SDK header a built-in includes, one reason per line, and it is long because **the built-ins still call sc-offline's own engines** (`src/spawner.cpp`, `build.cpp`, `teleport.cpp`, `loadout.cpp`, `quantum.cpp`, `contracts.cpp`, `npc.cpp`, `cvars.cpp`, the menu shell, the multiplayer session), or use the kernel directly where the SDK has no form yet (`sco/hook.h` and the `game.mining` rows for mining, `sco/net/session.h` for multiplayer). So the gate does **not** mean the built-ins are pure SDK plugins. It means nobody can add a new dependency unnoticed: a new include of a non-SDK header fails CI, the fix is a `game.*` service in sco-core and not a new line in the list, and a line whose includes are gone fails too, so the list only shrinks. The crew plugin still uses the built-in shell and shared NPC archetype picker. NPC and inspector sources live under `plugins/{npc,inspector}/`, with no host-header exceptions. Neither has host linkage or production built-in registration; `src/workflow_adapters.cpp` is removed. The external modules compile neither product UI nor new ImGui inputs. Legacy `src/npc.cpp` remains unchanged for remaining host consumers. See [SDK workflows](sdk-workflows.md).

## Migration boundary

The [owner decisions](decisions.md) retain **13** baseline feature units and **2** separately counted optional bridges. The eventual shell has zero feature built-ins and explicit folder enable semantics, not hidden fallbacks. Steps 1B/1C externalize inspector and NPC; nine built-ins still load with `plugins = off`. Four external features plus those nine preserve the 13-feature denominator, not 13/13 migration completion. Creative stays off. Step 1D separately makes Discord folder metadata off and refuses presence pending privacy/transport approval. The published SDK (2.4.0 release candidate 1 for the plugins; widgets since 2.3.0) carries renderer-neutral widgets (`sco_widgets.h`) and the Star Citizen host-source kit; the full host builds from its exact development submodule, and neither is game-verified here. The launcher and EAC/online refusal policy are unchanged. Normal consumer reload is not provider reload or gameplay certification; native faults require restart, not safe reload/recovery.

## Historical notes

sco-core's `docs/framework.md` ("Phase 4") and sc-offline's CHANGELOG entries up to 0.7 describe the move to built-in plugins one feature at a time, with `teleport.spatial`, `spawn.entities` and the cheats first living in this repository. That plan is **historical**: the services moved into sco-core's game pack and the cheats into the `creative` plugin. Read those pages for why a thing is shaped as it is, not for where it lives now.
