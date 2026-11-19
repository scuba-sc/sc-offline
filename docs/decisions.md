# Decisions

Maintainer: **scubamount**. This page records product decisions with their dates. A decision here is policy, not a claim that the work is finished or that anything was run in game; [Status](status.md) records that. Nothing here tags, publishes or certifies a release.

## 2026-11-18: multiplayer is off in the shipped settings

The shipped `sc-offline.ini` sets `multiplayer = off`. With it on, the launcher changes two firewall rules while you play: the game's block rule leaves your local network open, and an inbound UDP allow rule is added for the local subnet. The multiplayer plugin (sc-mp) is not in the 0.8.0 package, so the package should not open an inbound rule for a feature it does not contain. The code default of both launchers is unchanged (`on` when the key is missing), so the parity goldens stay valid. The tradeoff: people who add the co-presence plugin must turn the key on.

## 2026-11-18: The loadout plugin ships in 0.8.0

The loadout plugin (the Gear page, the Squadron 42 page and the `loadout.*` commands) ships on in 0.8.0, with the list pack it needs. Gear equip is the one switch-gated feature run in game, and it worked, and the packages the maintainer ran had this plugin.

The release plugin list is therefore 12 native plugins: spawn, crew, build, mining, npc, inspector, teleport, quantum, contracts, creative and loadout (all on), and discord (off). See [Features](features.md#plugins).

## 2026-11-18: Two game DLLs

The 0.8.0 package ships two game DLLs. Every build the maintainer ran in game used the CMake option `SCO_HOST_MENU_CLIENT=OFF`. Turning it on, which the new menu needs, changes how the classic menu gets its pages at about a dozen places in the source, and that configuration has never run in game. So the package carries the configuration that has run as the default `dinput8.dll`, and the new-menu configuration as a separate file the player must choose.

- **`dinput8.dll`** is built with `SCO_HOST_MENU_CLIENT=OFF`. It is installed by default.
- **`dinput8-rust-menu.dll`** is the same sources built with `SCO_HOST_MENU_CLIENT=ON` and the Rust menu hook. It has never run in game.
- **`sc-offline.exe`** (the Rust launcher) installs `dinput8-rust-menu.dll` into the game folder under the name `dinput8.dll` only when the ini says `menu = rust` and that file is beside the launcher. In every other case it installs `dinput8.dll`.
- **`sc-offline-classic.exe`** always installs `dinput8.dll`. With `menu = rust` it shows the classic menu, and the mod writes one `mod.log` line saying the new menu needs the Rust launcher.
- **Changing the `menu` key** takes effect on the next Play. The launcher replaces the installed DLL when its bytes differ from the chosen source, as it already does for a new version.

See [Which game DLL](launcher.md#which-game-dll), [The new menu](plugin-ui.md#the-new-menu-opt-in) and [Build](build.md#cmake-options-that-change-the-menu).

## 2026-11-18: sc-offline 0.8.0 release decisions

The maintainer will do no more testing before 0.8.0, so these close the questions that were still open.

1. **Launcher names.** The Rust launcher is `sc-offline.exe` in the package. The C++ launcher ships beside it as `sc-offline-classic.exe`, the documented fallback. It can undo PC changes from the same `%ProgramData%\sc-offline\pc-changes.txt` record ([the classic launcher](launcher.md#the-classic-launcher)).
2. **In-game menu.** The new (Rust) menu ships opt-in: ini key `menu = rust`, default `imgui`. It has never run in game. How the package carries it: [Two game DLLs](#2026-11-18-two-game-dlls).
3. **Capability switches.** `exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native` and `exp_audio_native` default **on**. `exp_entities_diag` (log-only diagnostics) defaults **off**. Only the loadout switch has been run in game and worked.
4. **Travel.** Travel to another planet, which needs a zone lookup beyond the player's own chain, is **not** in 0.8.0. It is a documented gap ([Features](features.md#travel-between-places)).
5. **Core defaults.** sco-core's capability defaults are unchanged. What the maintainer saw in game is recorded as evidence text in [Status](status.md), not as a changed default.
6. **Plugin catalogue.** A signed index. First-party and reviewed native plugins are allowed, and everything lands disabled. No production signing key exists yet, so the shipped trust root is empty and the catalogue works from a local folder (`plugins catalogue ... --source <folder>`). Adding a key is a release-time act of the maintainer.
7. **Versions.** sc-offline **0.8.0**, sco-core SDK **2.4.1**, game pack **0.8.1**, game build **4.10.200.58746**.
8. **Publishing.** Nothing is pushed, tagged or published by an agent. The maintainer tags the release.

Decisions made while preparing the release:

- **Travel places list.** The package ships `data/plugins/locations_catalog`, a copy of `data/locations.txt`, as an ordinary data pack. The quantum plugin requires one enabled `lists/locations.txt`, and `tools/check_package_tree.py` fails a package without it. The list serves same-chain destinations only; decision 4 stands.
- **Release script.** The release job runs `tools/package_release.py`, which also builds a package by hand ([Build](build.md#build-a-release-package-without-ci)). The release manifest stays format 1.
- **Release job guard.** The first step of the `release` job still refuses to package an unpublished development Core. It goes when the maintainer publishes SDK 2.4.1 and the pin matches it.
- **Updating from 0.7.x.** The ini and data files are kept, and the updater swaps the exe ([launcher.md](launcher.md#updating-from-07x)).

## 2026-11-17: Rust launcher and in-game menu

The decisions for the Rust launcher plan are recorded in full in [Rust launcher design](rust-launcher.md):

1. Rust covers the launcher and the in-game menu. The in-game host (`dinput8.dll`, hooks, Core host kit) stays C++. The Rust menu reads `sco.widgets` pages through a new versioned C interface in sco-core.
2. sco-core 2.4.1 is held until the Rust launcher, the Rust menu, the catalogue and the reference launcher are in it.
3. Catalogue: a signed index. First-party and reviewed native plugins are allowed. Every pulled plugin lands disabled with a native-code warning. Local plugins are always allowed.
4. Capability switches are on by default in all release packages. Each is a visible setting the player can turn off, and its text says it has not been run in game.

## 2026-11-13: run on the process-retained composition

sc-offline runs on the Core's opt-in process-retained host composition, with the bar "loads and works in game like the last shipped version". The product relies on retain-until-exit (no unload; stop is closure) instead of a proof that readers and workers drain. Install quiescence is attested only while the entry gate is held, plus the game thread's own service start. That is a known deviation to revisit after in-game evidence. A Core without the composition keeps the native-startup refusal. See [host composition](host-composition.md) and [the evidence record](handoff/2026-11-18-release-0.8.0-evidence.md#process-retained-startup).

## 2026-10-12: SDK migration decisions

These set the product policy for the move to external plugins on the sco-core SDK. Current exact source and archive identities are in [SDK coordinates](sdk-candidate.md).

| Topic | Decision | Where it stands |
| --- | --- | --- |
| Feature preservation | The target is **13 baseline feature units**: teleport, spawn, crew, loadout, npc, inspector, quantum, build, contracts, mining, multiplayer, creative and discord. **Two optional bridges** (titanlink, voxel_bridge) are counted separately and stay outside the normal release. No silent retirement of a feature. | The host registers no feature built-ins. The release stages **12 native plugins**: spawn, crew, build, mining, npc, inspector, teleport, quantum, creative, discord, contracts and loadout (decided 2026-11-18). Multiplayer is not registered or staged. These are 13 baseline IDs, not 13 of 13 complete. Supporting data packs are not counted as features. |
| Host and plugin dependencies | The Windows host uses an exact sco-core source pin. Standalone plugins use verified published SDK archives, never a source-checkout include tree. | See [SDK coordinates](sdk-candidate.md). |
| Enable and disable | Every feature has an explicit per-folder enable and disable, with no hidden built-in fallback. First-party folders are on by default and the user can disable them. | Eleven native plugin folders (creative and loadout included) ship enabled and need `plugins = on`. Discord ships with a `disabled` marker. |
| UI | Invest in renderer-neutral SDK widgets ([sco-core #139](https://github.com/scuba-sc/sco-core/issues/139)). Command-first examples meanwhile, and no new ImGui coupling. | The spawn, crew and npc plugins own the Vehicles, Crew and NPCs pages through public `sco.widgets`. The host draws them with its own ImGui adapter. |
| Persistent build ownership | Retained placements need scoped, removable handles that survive a plugin reload. No arbitrary entity deletion and no private-pointer fallback. | The build plugin removes its placements on unload. Persistence across a reload is not implemented. |
| Crew ownership | Crew may remove only NPCs it spawned itself, not other seat occupants or another plugin's NPCs. | Applies to every seat and removal path. |
| Quantum | Approved: bounded, game-thread-only, offline-admitted, owner-scoped controls, with no raw engine access. | The quantum plugin consumes `game.quantum`. Core needs a grounded backend and installed cleanup before it activates; the default engine adapter, NAV input and charge, audio and effects parity are pending. Global cvar writes are not authorized by this approval. |
| Console and cvars | Scoped game-cvar reads and reversible writes only. No arbitrary console execution for plugins, MCP or peers. A local debug console needs its own review and an explicit opt-in. | No new console or cvar API has been added. |
| Discord | Off until its transport and transmitted fields get a privacy and security review. | The plugin ships disabled and exposes only `discord.status`, which answers "unavailable" ([review](../plugins/discord/README.md)). Presence is blocked, not retired. |
| Multiplayer | Opt-in private co-presence. Session control is host-only. Narrow, user-consented LAN or VPN access, and no stored peer secrets by default. | No official game servers, no remote console, no wider plugin session or socket authority, no broad network exception. |
| Launcher, EAC and online safety | Refuse to change anything when EAC is present or the offline status is unknown or online. No detection suppression or bypass. | Plugins and MCP cannot change system or network policy. |
| Runtime MCP | Minimal game-pack-owned snapshots plus optional diagnostic consumers, opt-in ([sco-core #141](https://github.com/scuba-sc/sco-core/issues/141)). | Local, read-only, with freshness and redaction. No gameplay calls, mutation, raw memory or addresses, peer secrets or runtime reload. The launcher's own `sc-offline.exe mcp` ([AI agents](ai-agents.md)) is separate: it reads files and logs, never game memory. |
| Merge and evidence | CI gates every PR merge, and users test official releases. Compile, check and synthetic evidence stay separate from gameplay evidence. | A native plugin fault needs a restart, not a reload. No claim of full migration or game support without a recorded run on the exact build. |
| Updates and saves | Keep feature IDs and state stable during the migration. | Atomic first-party updates, preservation or reset of enable choices, and the legacy save and default contradictions are still open. |

**SDK dependencies.** The public SDK and game-pack gaps are tracked in sco-core issues [#122](https://github.com/scuba-sc/sco-core/issues/122) to [#135](https://github.com/scuba-sc/sco-core/issues/135): host composition and lifecycle, guarded relocation, spatial discovery, owned prefabs, filtered rays and free camera, loadout, scoped cvars, quantum controls, seat and persistence ownership, ASOP, hangars and ATC, contracts and missions, local economy, private sessions and content access. Also [#139](https://github.com/scuba-sc/sco-core/issues/139) (UI), [#140](https://github.com/scuba-sc/sco-core/issues/140) (host-source packaging) and [#141](https://github.com/scuba-sc/sco-core/issues/141) (runtime snapshots). They are dependencies, not promises that the pinned SDK exposes them.

Game-neutral kernel APIs stay neutral. Engine signatures, offsets, hooks and guarded mechanics belong to the game pack. Product plugins own feature policy, commands, state and content through public contracts. A missing capability means "unavailable", never a private engine fallback. Contracts land in sco-core before consumers move their pin. See [architecture](architecture.md) and [SDK workflows](sdk-workflows.md).
