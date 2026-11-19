# Status: what was seen in game and what was not

This is the one page that records what has been run in game. Other pages link here instead of repeating it.

## 0.8.0: what was and was not run in game

The owner ran one or two sessions of each item below on game build **4.10.200.58746**, with sco-core SDK **2.4.1** and game pack **0.8.1**. Every session used the classic launcher (`sc-offline-classic.exe`), the classic menu and a `dinput8.dll` built with `SCO_HOST_MENU_CLIENT=OFF`. That is the `dinput8.dll` in the 0.8.0 package. Nothing else was run in game. A build, a mock, a signature match, a static catalogue or a synthetic test is not a game result.

**Seen in game:**

| What | What was seen |
| --- | --- |
| Startup and the menu | The game started with the mod and the classic menu opened. |
| Gear equip through the Gear page (`game.loadout.async`) | The items attached (`AttachmentReceived` in `Game.log`). |
| Prefab placement through `game.prefabs` | An Outposts item was placed. |
| Ship spawn, boarding and Flight Ready | They worked. |
| Character spawn | It worked. |
| A saved-spot move (F7, then F8) | The player really moved to the spot. **Core did not confirm the arrival**: its own 0.05 m and 1 degree check stayed unmet. The fix is in code (below). |
| The seat probe | It logged Enter and Exit. |

**The fix that has not been run:** the teleport plugin now also reads the player's own position and reports "arrived" within 2 m. It logs `confirmation not received; you had arrived` if Core never confirms. Moving while seated in a ship is untested.

**Not run in game.** These are the things 0.8.0 changed or added that the owner has not seen:

- **The Rust launcher's `play` path and its elevated helper.** `sc-offline.exe play` has run only against a stand-in game process. Status text (60 scenarios), dry-run text (25) and install file trees (42) were compared with the C++ launcher in synthetic scenarios. The UAC helper, the real firewall rules, the hosts-file line, the Easy Anti-Cheat rename and the game start from an elevated launcher have not run on a real PC with the real game.
- **The new in-game menu** (`sc_offline_menu.dll`, `menu = rust`) and **`dinput8-rust-menu.dll`**, the DLL that carries its hook. The DLL is built with `SCO_HOST_MENU_CLIENT=ON`, which also changes how the classic menu gets its pages at about a dozen places in the source. The menu was checked against a stand-in widgets view (106 checks) and Core's own view (247 checks, 77 controls on 9 page shapes). The M-key handoff, cursor clip, resize and the "Classic pages" entry are written but have not run. [The decision](decisions.md#2026-11-18-two-game-dlls) explains why the package carries both DLLs.
- **The update swap.** The release check ran against GitHub and the swap against local zips with rollback. No real 0.7.x to 0.8.0 update has run.
- **The catalogue network path.** No production signing key exists, so the trust root is empty and no index is accepted from the network. The local-folder path ran against a synthetic signed index and the real creative plugin.
- **The launcher window on a real PC:** Browse (folder picker), plugin-settings save, Stop, the PC-change repair and the restart after an update.
- **Capability switches.** `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native` and `exp_audio_native` are on by default and have not run. This Core refuses native audio and logs why. `exp_entities_diag` is off.
- **Travel to another planet** is not in 0.8.0 ([why](features.md#travel-between-places)). `quantum.travel` as a whole has not run.
- **Every other feature** in [Features](features.md), as listed in the table below.
- **The package as built.** It was assembled and its file list and manifest checked on a development machine from plugin builds made against the development SDK 2.4.1 archive. The release job refuses to package an unpublished development Core by design. The owner moves the `external/sco-core` pin and publishes the SDK. The standard plugin list does not include the `loadout` plugin, which is a development consumer; see [Features](features.md#plugins).

### Still to show, by feature

| Feature | What a game run has to show |
| --- | --- |
| Teleport and Travel | F7 then F8 returns to the saved spot and says "arrived". `quantum.travel` arrives at the destination radius plus the altitude. `quantum.bookmark` works. The arrival, failure and cancellation messages appear. `relocation = off` refuses moves. |
| Ships and crew | A Javelin with `crew.fill <npc>` logs one `interactable yes\|no` line per seat and seats the usable ones (the expected split is 10 seated, 5 skipped, if the seat-owner theory holds). On a two-seat ship every empty seat fills. `crew.stand_all`, `crew.clear`, `crew.sit pilot` and `crew.power_on` answer OK. |
| NPCs | `npc.spawn <archetype> 3` logs the session IDs as pending. After stream-in `npc.observe` reports visible transforms. `npc.remove` and `npc.clear` touch only this plugin's NPCs. Unload cleans up. Disabling the folder or `plugins = off` removes the commands and the panel. |
| Build | `build.place` with a class and with a `.socpak` path grounds an object ahead of you. `build.undo` and `build.clear` remove only what was placed. Unload removes the placements. F6 refuses cleanly. |
| Creative | `creative.god`, `creative.noclip` with `creative.noclip_speed 100`, `creative.ammo` and `creative.ship_ammo` work and switch off again. After disabling the plugin and restarting, all are off and the fly speed is the game's own. The Creative tab drives the same commands. |
| Inspector | `inspector.read` before and after spawn reports unavailable fields as unknown and mutates nothing. `sco.reload inspector` registers the command once. |
| Contracts | Listing, accept, completion and rewards on the current host engine. |
| Mining | With the mining plugin's `enabled` setting on: natural mining logs `[mining] natural mining active`, `mining.status` counts cells and promoted cells, and extraction puts ore in cargo. Leaving and returning, depletion and persistence across restarts were never verified, even by the fork this comes from. |
| Ship terminals, hangars and ATC | Deliver, Retrieve, Store and the ATC hails, on a current build. |
| Multiplayer | Not in the release; see [sc-mp](https://github.com/scuba-sc/sc-mp). |
| Menu pages and window | Rendering at 100% and 150% DPI, the sidebar, the plugin pages, resize and the saved size. |
| Launcher | Each launcher command against a real game folder. The plugin page reads a real `data/plugins`, honours the `disabled` marker at game start, and a plugin's **Save** reaches the plugin in game. |
| Game data pack | The Gladius' new quantum drive (`quantum_drive` pack) applies as the game loads. |

ChrisWareOffline 0.9.0-rc1 flagged these as built after its last in-game test, and they are still open: the energy-weapon top-up, NPC deletion through the entity handle, Stand up, the real-building prefab preview, and Pyro and Nyx names after a fresh scan.

## Changed since the last in-game run

The owner's in-game sessions used package dev241.16. Its host source is the branch state at commit `1ab34053`. This section lists what went into the 0.8.0 `dinput8.dll` after that, and says whether each change can alter the default path: `menu = imgui`, the classic menu, a DLL built with `SCO_HOST_MENU_CLIENT=OFF`. The list comes from `git diff 1ab34053 HEAD -- src/ CMakeLists.txt` plus the edits made after the last commit. Every item is source or build evidence. Not run in game.

Host source and build files:

| Change | Can it alter the default path? |
| --- | --- |
| `src/version.h`: the version is 0.8.0 instead of 0.7.8.3. | It changes text only: the window title, the first line of `mod.log`, the version each built-in plugin reports and the file properties. No code branches on it. |
| `src/menu.cpp`: code for the Rust menu (M-key handoff, the "Classic pages" entry, a window-state note), all inside `#ifdef SCO_MENU_RUST`. One block was rewritten from a single line into braces. | No. `SCO_MENU_RUST` is defined only when `SCO_HOST_MENU_CLIENT` is on, so the compiler never sees this code in the default DLL. The rewritten block behaves the same without it. |
| `src/menu_rust.cpp` and `src/menu_rust.h`: the Rust menu hook (key handling, the handoff, where it looks for `sc_offline_menu.dll`). | No. They are compiled only into `dinput8-rust-menu.dll`. |
| `src/menu.cpp`, one new block in the menu thread, only in the default DLL: when `SC_OFFLINE_MENU` is `rust` it writes one `mod.log` line saying the new menu needs the Rust launcher. | Not with the default setting. The launcher always sets `SC_OFFLINE_MENU`, to `imgui` unless the ini says `menu = rust`. The block reads that variable once at menu start and writes nothing for `imgui`. |
| `CMakeLists.txt`: the `dinput8` target is now defined by one function that also defines the second DLL. | No. The generated project of the default DLL was compared with the one from the tree at `1ab34053`, configured with the same options and the same Core. Source list, compiler definitions, include folders, compiler options, link libraries and linker settings are identical. |

The plugins in the package come from separate builds and were not compared here.

sco-core is also a build input of `dinput8.dll`. Its source changed after the 2.4.1 line revision the plugin proofs used (`5858fdb`):

| Change in sco-core | Can it alter the default path? |
| --- | --- |
| Relocation: a move counts as arrived when a later player position is within 0.5 m of the target. Rotation is no longer required. Before, it needed 0.05 m and 1 degree. | Yes, on purpose. It affects F7/F8, `quantum.travel` and every other move that waits for arrival. It is the fix for the unconfirmed saved-spot arrival above. Not run in game. |
| Loadout: the outfit slots are 22 to 31 (hair, hair colour and eyebrow slots are gone), and the capability `game.loadout.appearance` is gone. Outfit slots stay off unless `SCO_LOADOUT_OUTFIT=1`, which the launcher never sets. | The slots the Gear page used before (undersuit, armour, weapons, tools) keep their numbers and their code. The outfit slots are off by default. |
| Widgets view: events are kept when the view stops. | The new menu reads this view. The classic menu does not. |

The Core revision of dev241.16 is not recorded, so the sco-core list is every Core commit after `5858fdb`, the revision the plugin proofs used. `6640fc3` holds the three source changes above. `b2d6faf` adds the SDK's example launcher and `f3530f7` rewrites the SDK documents and the status registry; neither changes a file under `src/` or `include/`.

## Checked on a development machine

These are synthetic results, not game results.

- The consumer proofs `tools/check_{inspector,crew,npc,spawn,build,loadout,mining,discord,teleport,quantum}_sdk.py` and `tools/contracts_sdk_check.py --slice read` pass against the development SDK 2.4.1 archive (SHA-256 `634ec29627646c37200d4cc570fed1fb5d487bfb3b161fc1271581e491bc4aa7`, source `5858fdb65bf8`, MSVC 2022). `check_titanlink_sdk.py` and `check_voxel_bridge_sdk.py` pass against the stable SDK 2.4.0 archive. Identities of every SDK archive: [SDK coordinates](sdk-candidate.md).
- The Rust workspace passes `cargo fmt --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo deny check` and `cargo test --workspace` (423 tests, 0 failed, 2 hand-run dialog tests ignored).
- The C++ launcher passes the launcher contract check (735 golden files), the status and config test (394 checks), the MCP stdio proof and the update contract in both directions with the old C++ launcher.
- Both game DLLs build from one CMake source list in one configure. A byte scan finds `sc_offline_menu.dll` (ASCII and UTF-16) only in `dinput8-rust-menu.dll`, and the `mod.log` line about the Rust launcher only in `dinput8.dll`. Neither contains the TitanLink, voxel bridge, multiplayer, inspector or NPC command names. Both report version 0.8.0.
- The Rust launcher's `status` text (60 scenarios), `--dry-run` text (25) and install file trees (42) match the C++ launcher's on synthetic folders. `play` and `stop` ran against a stand-in game process, and the plugin settings proof ran against a synthetic plugin.
- The external plugins build against published SDK archives and pass the SDK's plugin checker with no game services. CI uses the SDK 2.4.0 archive until 2.4.1 is published.
- sco-core keeps the matching table for the `game.*` services, with each capability's evidence level and what a run has to show: [docs/game/status.md](https://github.com/scuba-sc/sco-core/blob/4ba313da3ac76c887cda03772d6106452f33a5d4/docs/game/status.md) at the pin this page was written against. `game.actors.state`, `game.entities.query_radius`, `game.entities.watch` and `game.events.vehicle_seat` are held off there until a run decides them.

These prove contracts, builds and refusal paths with stand-in services only. They do not show Windows rendering, installation, reload, provider cleanup or gameplay.

## Earlier observations

- **v0.7.8.1 offline startup.** Seen twice. First on one Windows PC (game 4.10.196.36804, launched through `sc-offline.exe play` with the game running as administrator): `[g1] evidence: session=Offline officialNetwork=Clear antiCheat=Clear target=Clear`, every offline patch line (including `offline-force (IsOnline = 0): patched` and `local handshake (accept frontend connection): patched`), `[core] signatures: 316/316 OK`, the quit hook installed, and `bOnline[0]` in `Game.log`. That session had no desktop, so the game did not render or reach the menu. Then by the maintainer on a desktop with the game running with normal rights (release DLL SHA-256 `47e89459...`): the same evidence line (`profiles=6 rules=734`), every offline patch, 316/316 signatures, eight plugins loaded, and `Game.log` reaching `Requesting game mode PU_All/SC_Default` and `Loading level megamap`. So the firewall read works without administrator rights on that PC.
- **v0.7.7.** One maintainer run (sco-core `2197dfb`, game pack 0.2.0, NVIDIA Smooth Motion off), recorded in the [v0.7.7 release notes](https://github.com/scuba-sc/sc-offline/releases/tag/v0.7.7). It was a single run, not a per-feature check.
- **Linux.** One report of 0.7.0 played offline on one Wine setup: [Linux](linux.md#historical-observation). Nothing newer has run under Wine.

## Process-retained startup

With a Core that provides the process-retained composition, the host starts natively under retain-until-exit: the module is pinned and never unloaded, stop is closure, and there is no restart. The Core pinned in `external/sco-core` provides it; an older Core keeps the refusal. Beyond the sessions listed above, nothing shows engine-object lifetime inside every hook, or whether starting the game services on the game thread (a documented deviation from the install-quiescence rule) is safe in general. See [host composition](host-composition.md).

## Admission and offline evidence

Since v0.7.8.1 the shell reports offline-session and network evidence from live firewall and connection reads. It never infers safety from flags or patch success. Offline startup was seen in game as above. Easy Anti-Cheat detection and refusal, services published before plugins, and a clean exit were not checked on their own. Actor-stats stat units, ranges and writes are untested. Static signatures target build **4.10.200.58746**, and the hauling semantic change is unverified.

CI proves that code compiles, the checks hold and the binaries have the right shape. It does not prove a feature works. Merge and release policy: [decisions.md](decisions.md).
