# Architecture

sc-offline is two things on top of [sco-core](https://github.com/scuba-sc/sco-core): the host that loads sco-core's kernel into Star Citizen, and the reference consumer of what sco-core publishes.

sc-offline 0.8.0 targets sco-core SDK 2.4.1 (not yet published): plugin ABI 1.1, Star Citizen game pack 0.8.1. The host builds from the exact commit in `external/sco-core`. Plugin proofs use a verified SDK archive; see [SDK coordinates](sdk-candidate.md). Neither a passing check nor a Windows build shows gameplay; see [status.md](status.md). Build steps are in [build.md](build.md).

## Who owns what

| Layer | Owns | Lives in |
| --- | --- | --- |
| sco-core kernel | The plugin loader, `sco_api`, `sco.ui`, `sco.storage`, `sco.net`, `sco.ipc`, `sco.settings`, hot reload (`sco.reload`), the detour engine (`sco/hook.h`), the CryPak and DataCore pack loader | `external/sco-core` |
| sco-core game pack | The game's signature rows (`sco/game/*.h`, each address found once, by one unique pattern) and the `game.*` services built on them: `teleport.spatial`, `spawn.entities`, `game.actors`, `game.vehicles`, `game.entities`, `game.world`, `game.creative` | `external/sco-core` |
| sc-offline | The `dinput8.dll` proxy, the offline patches, the main-thread hook, the menu shell, the launchers, the external feature modules, data packs and remaining legacy engine helpers | `src/`, `launcher/`, `launcher-rs/`, `plugins/`, `data/` |

Nothing under `src/` finds a game address by pattern. `tools/no-scans.sh` fails the build if anything there calls a raw scanner (`FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch`, `memchr`). Every address comes from a sco-core row, so a game patch that moves code is fixed in one place, checked against each game build.

## The two game DLLs

The mod ships as two builds of the same sources:

- `dinput8.dll`, built with `SCO_HOST_MENU_CLIENT=OFF`. This is the build that has run in game. The launcher installs it by default.
- `dinput8-rust-menu.dll`, built from the target `dinput8_rust_menu` (option `SCO_BUILD_RUST_MENU_HOST=ON`) with the menu client on. It adds the host menu client and the Rust menu hook (`src/menu_widgets.cpp`, `src/menu_rust.cpp`) and changes how the ImGui menu gets its pages. Not run in game.

The Rust launcher copies the second file into the game folder as `dinput8.dll` only when `sc-offline.ini` says `menu = rust` and the file sits beside the launcher. In every other case, and always for the C++ launcher, it installs the first. The launcher replaces the installed DLL on the next Play when its bytes differ from the selected one. With `menu = rust` the C++ launcher shows the classic menu, and the first DLL writes one `mod.log` line saying the new menu needs the Rust launcher. See [build.md](build.md#build-the-two-game-dlls-windows-x64-msvc) and [Rust menu](rust-menu.md).

## Startup

The bootstrap prepares the game-pack host composition with live operating-system admission evidence before it changes the engine. The game pack owns startup, signature resolution, the quit and DataCore hooks, and plugin tick admission. `Platform::gameServices` enables the public services. The shell supplies no built-in feature inputs. Plugin folders load when `plugins = on`, subject to their manifests and capabilities.

Native startup needs a Core with the process-retained composition. See [host composition](host-composition.md) for that and for the refusal rules.

## What each feature runs on

| Feature | Runs on | Notes |
| --- | --- | --- |
| Quick save and return | External `teleport` and `quantum` on `game.relocation` | Needs positive offline admission, resolved rows and a live player. No private movement fallback. Travel to another planet is not in 0.8.0. |
| Vehicles, Crew and NPCs | External `spawn`, `crew`, `npc` on public actor, vehicle, entity and spatial services and `sco.widgets` | Shared picker catalogues. Ownership and delayed operations are the consumer's job. |
| Read-only inspector | External `inspector` on public actor and entity reads | Reports unknown values when providers are unavailable. See [SDK workflows](sdk-workflows.md). |
| Build props | External `build` on public entity and spatial services | Camera and aim controls still refuse when a provider is unavailable. |
| Noclip, god mode, infinite ammo | External `creative` on `game.creative` (repository `scuba-sc/sc-creative`) | Ships enabled. |
| Discord presence | External `discord` refusal module | Ships disabled pending a transport and privacy review. |
| Natural mining | External `mining` on `game.mining` | The setting is off by default. The native adapter is unavailable until verified. |
| Contracts | External `contracts` consumer | Production changes stay gated on a ready backend. See [contracts](contracts-sdk.md). |
| Gear and SQ42 settings | Development `loadout` consumer | Not staged in releases. Unsupported slots and writes are refused. |
| Multiplayer | Separate [sc-mp](https://github.com/scuba-sc/sc-mp) consumer | Not in the release. |
| Quantum drive and pickers | Data packs | `data/plugins/quantum_drive`, `data/plugins/picker_catalog` |
| Titanfall 2 and voxel bridges | Independent external consumers of `sco.ipc` | Not in the release. See [bridges](bridges.md). |

The loadout plugin (`plugins/loadout`, [README](../plugins/loadout/README.md)) is a development consumer. The host does not register, compile or tick loadout code; the old sources are in `legacy/loadout`. Four SQ42 settings use public real reads and report unknown or refused states. They never use the separate cvar-write service. Optional Player and SQ42 panels use `sco.widgets`, not product ImGui globals.

## Plugins

The host registers no built-in feature plugins. Unbuilt legacy sources under `src/builtins/` and remaining shared engine helpers provide no registration, linked consumer or feature tick.

Optional plugins are folders under `data/plugins/<id>/` with a `plugin.ini`. They load only with `plugins = on` in `sc-offline.ini`. A plugin is off when its folder holds an entry named `disabled`. The launcher's Plugins page creates and deletes that marker and edits a plugin's `[settings]`; a change applies at the next game start. Hot reload (`sco.reload <plugin>`) works only inside the running game. The launcher has no channel into it.

The release stages 11 external feature folders: creative, discord, crew, spawn, build, mining, teleport, quantum, inspector, npc and contracts. Discord keeps its disabled marker. The other ten ship enabled. Each module builds on its own from the checksum-verified SDK, with no host-private include path. A folder that ships enabled does not show that its services exist or that the feature works in game; see [status.md](status.md).

The [owner decisions](decisions.md) keep 13 baseline feature units and 2 optional bridges. Eleven of the 13 are staged. Loadout is a development consumer and multiplayer is the separate sc-mp repository. The launcher and the EAC and online refusal policy are unchanged. Reloading a plugin is not evidence that a provider reloads or that the game works. Native faults need a restart.

## CI jobs

[build.yml](../.github/workflows/build.yml) runs on pushes and pull requests to `main`, and on `v*` tags. `release` runs on tags only. [build.md](build.md#ci) lists every job. The ones that guard this architecture:

| Job | Checks |
| --- | --- |
| `check` | `tools/check.sh` (clang parse against mingw-w64 headers, MSVC C2712 screen, baseline in `tools/check-baseline.txt`), the release-manifest tests, and that `src/third_party/imgui` matches the ImGui headers the SDK ships |
| `build` | Release x64 host with MSVC, binaries checked with `dumpbin` and `mt`. Modules are built in their own SDK jobs and are not linked into the host. |
| `launcher-mcp` | `tools/check_launcher_mcp.py`: protocol tests, also run with `--exe` against the built launcher |
| `no-scans` | No raw game scan under `src/` |
| `sdk-headers` | Every file under `src/builtins/` and `plugins/` includes only SDK headers, its own files, or a header named in `tools/sdk-headers-allow.txt` |

### What the `sdk-headers` allow-list means

The gate makes `src/builtins/` and `plugins/` consumers of the SDK's public headers. The Creative plugin (the `external/sc-creative` submodule) needs no entry, and its own repository runs the equivalent proof.

The list names each non-SDK header the legacy sources under `src/builtins/` still include, one reason per line. Those sources call sc-offline's own engines (`src/build.cpp`, `teleport.cpp`, `quantum.cpp`, `travel.cpp`, `npc.cpp`, the menu shell, the multiplayer session) or the kernel directly where the SDK has no form yet (`sco/net/session.h`). The host compiles only `build_ui.cpp` of them.

Passing the gate therefore does not make the legacy code pure SDK code. It means nobody can add a dependency unnoticed. A new include of a non-SDK header fails CI. The fix is a `game.*` service in sco-core, not a new line in the list, and a line whose includes are gone fails too, so the list only shrinks. The NPC and inspector sources under `plugins/` have no host-header exceptions and no host linkage. Legacy `src/npc.cpp` stays for the remaining host consumers.

## Background

sco-core's `docs/framework.md` ("Phase 4") and the sc-offline CHANGELOG entries up to 0.7 describe the move to built-in plugins one feature at a time, with `teleport.spatial`, `spawn.entities` and the cheats first living in this repository. That plan is finished: the services moved into sco-core's game pack and the cheats into the `creative` plugin. Read those pages for why a thing is shaped as it is, not for where it lives now.
