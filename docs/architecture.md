# Architecture: sc-offline on sco-core

sc-offline is two things on top of [sco-core](https://github.com/scuba-sc/sco-core): the **host** that loads sco-core's kernel into Star Citizen, and the **reference consumer** of what sco-core publishes. The host builds from the exact Core gitlink (`external/sco-core`, currently `4ba313d`, development SDK `2.4.1-dev.6`: plugin ABI **1.1**, Star Citizen game pack **0.8.1**). External plugin proofs use the published SDK 2.4.0 archive (plugin ABI 1.1, game pack 0.8.0). Neither package checks nor a Windows build show gameplay. See [build.md](build.md) and [status.md](status.md).

## Loadout/SQ42 development boundary

`plugins/loadout` is the external G6/G7 consumer; see its [build and limitations](../plugins/loadout/README.md). The host no longer registers, compiles or ticks loadout code; the old sources are kept in `legacy/loadout`. Four SQ42 settings use public real reads and unknown/refusal states, never the separate unapproved cvar-write service. Its G6/G7, content and widgets headers ship in the pinned published SDK 2.4.0 (`sdk-v2.4.0`); loadout is not yet staged by the release workflow. Optional Player/SQ42 panels use `sco.widgets`, not product ImGui globals. Weapon, clothing, head and default parity and the production typed engine adapters are pending. Step 4C quantum migration is not approved; private boost/keeper paths are unchanged.

## Who owns what

| Layer | Owns | Lives in |
| --- | --- | --- |
| **sco-core kernel** | The plugin loader, `sco_api`, `sco.ui`, `sco.storage`, `sco.net`, `sco.ipc`, `sco.settings`, hot reload (`sco.reload`), the detour engine (`sco/hook.h`), the CryPak/DataCore pack loader | `external/sco-core` |
| **sco-core game pack** | The game's signature rows (`sco/game/*.h`, each address found once, by one unique pattern) and the `game.*` services built on them: `teleport.spatial`, `spawn.entities`, `game.actors`, `game.vehicles`, `game.entities`, `game.world`, `game.creative` | `external/sco-core` |
| **sc-offline** | The `dinput8.dll` proxy, the offline patches, the main-thread hook, the menu shell, the launcher, the external feature modules, supporting data packs and remaining legacy engine helpers | `src/`, `launcher/`, `plugins/`, `data/` |

Under `src/` nothing finds a game address by pattern: `tools/no-scans.sh` fails the build if anything there calls a raw scanner (`FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch`, `memchr`). Every address comes from a sco-core row, so a game patch that moves code is fixed in one place, sco-core, which is checked against each game build.

## Startup

The bootstrap prepares the game-pack host composition with live OS admission
evidence before it changes the engine. The game pack owns startup, signature resolution,
the quit and DataCore hooks, and plugin tick admission. `Platform::gameServices`
enables the public services; the shell supplies no built-in feature inputs.
Plugin folders load when `plugins = on`, subject to their manifests and capabilities.
Native startup needs a Core with the process-retained composition; see
[host composition](host-composition.md) for that and the refusal boundaries.

## What each feature runs on

| Feature | Runs on | Notes |
| --- | --- | --- |
| Quick save/return and travel | External `teleport` and `quantum` on `game.relocation` | Requires positive offline admission, resolved rows and a live player; no private movement fallback |
| Vehicles, Crew and NPCs | External `spawn`, `crew`, `npc` on public actor/vehicle/entity/spatial services and `sco.widgets` | Shared public picker catalogs; ownership and delayed operations remain consumer responsibilities |
| Read-only inspector | External `inspector` on public actor/entity reads | Explicit unknowns when providers are unavailable; [SDK workflows](sdk-workflows.md) |
| Build props | External `build` on public entity/spatial services | Interactive camera/aim controls still refuse unavailable providers |
| Noclip, god mode, infinite ammo | External `creative` on `game.creative` (maintained in `scuba-sc/sc-creative`) | Ships enabled |
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
build, mining, teleport, quantum, inspector, npc and contracts. Discord keeps
its disabled marker; the other ten ship enabled. Each module builds on its own
from the checksum-verified published SDK, with no host-private include path.
Loadout is a development consumer and is not staged. Staging a folder enabled does
not show that its services exist or that the feature works in game; see
[status](status.md) and [SDK coordinates](sdk-candidate.md).

## The CI gates

The workflow runs on pushes and pull requests to `main`, and on `v*` tags; `release` runs on tags only ([build.yml](../.github/workflows/build.yml)).

| Job | Checks |
| --- | --- |
| `check` | `tools/check.sh` (clang parse against mingw-w64 headers, MSVC C2712 screen, baseline in `tools/check-baseline.txt`), the release-manifest tests, and that `src/third_party/imgui` matches the ImGui headers the SDK ships |
| `build` | Release x64 host with MSVC; binaries checked with `dumpbin` and `mt`. The external modules are built in their own SDK jobs against the checksum-verified published archive and staged for the release; they are not linked into the host |
| `launcher-mcp` | `tools/check_launcher_mcp.py`: portable protocol tests; the `build` job also runs it with `--exe` against the built launcher |
| `no-scans` | No raw game scan under `src/` |
| `sdk-headers` | Every file under `src/builtins/` and `plugins/` includes only SDK headers, its own files, or a header named in `tools/sdk-headers-allow.txt` |

### What the `sdk-headers` allow-list means

The gate makes `src/builtins/` and `plugins/` consumers of the SDK's public headers. The creative plugin (the `external/sc-creative` submodule) needs no allow-list entry, and its own repository runs the equivalent proof. The list names each non-SDK header the legacy sources under `src/builtins/` still include, one reason per line: those sources call sc-offline's own engines (`src/build.cpp`, `teleport.cpp`, `quantum.cpp`, `travel.cpp`, `npc.cpp`, the menu shell, the multiplayer session) or the kernel directly where the SDK has no form yet (`sco/net/session.h` for multiplayer). Of these sources the host compiles only `build_ui.cpp`.

So passing the gate does not make the legacy code pure SDK code. It means nobody can add a new dependency unnoticed: a new include of a non-SDK header fails CI, the fix is a `game.*` service in sco-core and not a new line in the list, and a line whose includes are gone fails too, so the list only shrinks. NPC and inspector sources under `plugins/{npc,inspector}/` have no host-header exceptions and no host linkage. The external modules compile neither product UI nor new ImGui inputs. Legacy `src/npc.cpp` stays for the remaining host consumers. See [SDK workflows](sdk-workflows.md).

## Migration boundary

The [owner decisions](decisions.md) retain **13** baseline feature units and **2** separately counted optional bridges. The host registers no feature built-ins; each feature is an external folder with its own enable switch, and there are no hidden fallbacks. Eleven of the 13 units are staged in the release; loadout is a development consumer and multiplayer is the separate sc-mp repository. Discord is staged off and refuses presence pending privacy and transport approval. The published SDK 2.4.0 carries the renderer-neutral widgets (`sco_widgets.h`) and the Star Citizen host-source kit. The launcher and EAC/online refusal policy are unchanged. Reloading a consumer plugin is not provider reload or gameplay evidence; native faults need a restart.

## Historical notes

sco-core's `docs/framework.md` ("Phase 4") and sc-offline's CHANGELOG entries up to 0.7 describe the move to built-in plugins one feature at a time, with `teleport.spatial`, `spawn.entities` and the cheats first living in this repository. That plan is **historical**: the services moved into sco-core's game pack and the cheats into the `creative` plugin. Read those pages for why a thing is shaped as it is, not for where it lives now.
