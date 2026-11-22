# Rust launcher: reference for maintainers

`sc-offline.exe` is the launcher of the 0.8.0 package. It is written in Rust and lives in `launcher-rs/`. The C++ launcher ships beside it as `sc-offline-classic.exe`, the fallback. The in-game host (`dinput8.dll`, hooks, Core host kit) stays C++. This page is for someone who will maintain or rebuild the launcher. Players read [launcher.md](launcher.md).

Evidence: every check below is source, build, synthetic or CI evidence, and none of it has been run in game. What the maintainer did run is in [status.md](status.md).

The in-game Rust menu is a second crate in the same workspace. It has its own page: [rust-menu.md](rust-menu.md).

## What it does

The launcher runs outside the game. It finds the game folder, copies the mod in, starts the game, and takes the mod out when the game closes. It reads `sc-offline.ini`, builds the game's environment, manages plugins, and updates itself. It has a command line and a window. Both call the same library crates, so the window never starts the exe to do a job the library can do. The exceptions are Play, Update and Repair, which run this exe as a child with `SC_OFFLINE_GUI` set so the output can be shown.

## Crates

One Cargo workspace, `launcher-rs/`, members `crates/*`. Toolchain 1.99.0, target `x86_64-pc-windows-msvc`, pinned in `rust-toolchain.toml`. `Cargo.lock` is committed. `.cargo/config.toml` sets the static CRT (`+crt-static`, so no vcruntime or ucrt DLL import), `/Brepro` and a path remap. `deny.toml` is the licence and ban gate.

| Crate | What it does | Binary or output |
|---|---|---|
| `scol-contract` | Constants of the launcher-to-game contract: `SC_OFFLINE_*` and capability variable names, stop event names, marker file, backup suffix, `pc-changes.txt` record keys, ini keys, player-owned files, `sco_kv` key format, exit codes, and the catalogue constants in `product` (index URL, hosts, trust keys). Each item cites its C++ source. No UI, no network. | |
| `scol-config` | Reads `sc-offline.ini`, edits it in place, builds the game's environment block, reads `plugin.ini`. Reproduces the C++ launcher byte for byte. | |
| `scol-install` | Game discovery, status checks, mod put and take, marker, crash leftovers. Read-only `status`. | |
| `scol-pcchanges` | Firewall rules, the LAN allow rule, the hosts line, the EAC exe rename, their record and undo, and the `--helper` mode. Every effect goes through the `System` trait. | |
| `scol-play` | The `play`, `install` and `uninstall` flow, the stop channel, the game folder picker, and the hidden `--helper`, `--delete-logs` and `--self-test` modes. Every effect goes through the `Host` trait. | `scol-play-standin.exe` (a stand-in game for tests) |
| `scol-update` | Self-update: HTTPS-only fetch from fixed hosts, byte caps, digest and manifest checks, staging, journaled swap with rollback, the `--self-test` gate. | |
| `scol-plugins` | `plugin.ini` parser (host rules) and the plugin zip verifier, a port of the SDK `sco-plugin-verify.py`. Never loads plugin code. | |
| `scol-catalogue` | Signed catalogue client: index check, compatibility policy, download check, install plan. No network code. | |
| `scol-plugin-manager` | `plugins` commands: list, enable, disable, install from folder or zip, remove, settings, catalogue, update. Journaled changes. | `scol-pm.exe` |
| `scol-redact` | Log redactor, a port of `launcher/redact.h`. Used by the crash report and the MCP server. | |
| `scol-mcp` | The read-only MCP server behind `mcp`: four tools (`get_status`, `list_plugins`, `read_log`, `get_settings`). Own JSON code so replies match the C++ server byte for byte. | |
| `scol-ui` | The window (eframe 0.37, egui 0.37, glow renderer). | `scol-ui.exe` (standalone self test and screenshots) |
| `scol-cli` | Argument parsing, usage text, exit codes, dispatch. The only crate that turns on the `real` features of `scol-play` and `scol-pcchanges`. | `sc-offline.exe`, `scol.exe` (same code, kept for the parity scripts), `scol-hello.exe` |
| `scol-menu` | The in-game menu. A DLL, not part of the launcher exe. See [rust-menu.md](rust-menu.md). | `sc_offline_menu.dll` |

How they stack: `scol-cli` calls everything. `scol-ui` calls `scol-install`, `scol-plugin-manager`, `scol-play`, `scol-update`, `scol-config` and `scol-catalogue`. `scol-mcp` uses `scol-install` and `scol-redact`. `scol-plugin-manager` and `scol-update` use `scol-catalogue`; `scol-catalogue` and `scol-install` use `scol-plugins`. Everything depends on `scol-contract`. `scol-menu` depends on none of them.

There is no separate product-config crate. The product values (catalogue URL, hosts, trust keys) are in `scol_contract::product`.

Workspace facts that matter:

- The `release` profile keeps `panic = unwind`. The menu DLL catches panics at its exported functions inside the game, which `abort` would defeat. LTO, `opt-level = "s"`, stripped.
- `scol-play` and `scol-pcchanges` compile their real implementations (`RealHost`, `RealSystem`) only with the cargo feature `real` or `real-system`. Only `scol-cli` turns them on. A test that names either fails `tests/no_real_system.rs`. No test starts the game, asks for administrator rights or touches the firewall, the hosts file or the EAC exe.
- Test-only features are never on in a release build: `scol-ui/fixtures` (a signed golden index and the TEST key), `scol-update/test-hooks` (`--apply-zip`), `scol-plugin-manager/test-trust` (`--test-trust-key`).

## Commands

Check against `sc-offline.exe help` and `sc-offline.exe plugins`.

```
sc-offline.exe [command] [--game <folder>] [--dry-run] [--skip-eac-check]
  play       (default) copy the mod in, start the game, take the mod out when it closes
  install    copy the mod in and leave it there
  uninstall  take the mod out and restore anything it replaced
  status     check the setup; read-only, no network
  mcp [--game-log]   MCP server on stdin/stdout; read-only
  update [--check] [--yes] [--prerelease]
  stop [--game <folder>] [--end]   ask the game to close; --end ends it if still open a minute later
  plugins    list | enable | disable | install | remove | settings | catalogue | update
  help
  --console  run `play` in this console when double-clicked
  --window   open the window
```

- `plugins list [--json]`, `enable <id>`, `disable <id>`, `install <folder|zip> [--replace]`, `remove <id> [--purge]`, `settings <id> [name=value ...] [--reset]`, `catalogue list`, `catalogue install <id> [--replace] [--allow-downgrade]`, `catalogue update [<id>]`, `update [<id>]`. `--source <folder>` on the catalogue commands reads a local folder. `--root <launcher folder>` goes before `list`.
- No arguments: the window when double-clicked, otherwise `play`.
- Hidden modes: `--helper`, `--delete-logs`, `--self-test <tag>`, `--elevated-update`. Test builds add `--apply-zip`. `scol-ui.exe` also takes `--here`, `--ui-self-test`, `--ui-screenshots`.
- Exit codes: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running, 10 (window child only) an update was applied and the window restarts.
- `stop`, `report` and `plugins` do not exist in the C++ launcher. The usage text differs by exactly those three lines, and the parity scripts drop them.

## How the launcher and the game talk

The contract is files, environment variables and named events. The host does not know which launcher started it.

Environment block (set by `play`; `scol-config::env`): `SC_OFFLINE_BOOT_MAP`, `SC_OFFLINE_MOD_LOG`, `SC_OFFLINE_SPAWN_FILE`, `SC_OFFLINE_SHIPS_FILE`, `SC_OFFLINE_START`, `SC_OFFLINE_START_SHIP`, `SC_OFFLINE_PLUGINS`, `SC_OFFLINE_RELOCATION`, `SC_OFFLINE_MULTIPLAYER`, `SC_OFFLINE_MULTIPLAYER_ALLOW`, `SC_OFFLINE_ASOP`, `SC_OFFLINE_ASOP_FLEET_LIST`, `SC_USER`, `SC_OFFLINE_REGISTRY_PROBE`, and `SC_OFFLINE_MENU` last. `SC_OFFLINE_MENU` is always set, to `imgui` or `rust`, so a value in the player's shell cannot choose the menu. The capability variables are separate (next section). `SC_OFFLINE_GUI` is set only on the window's child process.

Files:

| File | Where | Meaning |
|---|---|---|
| `sc-offline.ini` | launcher folder | Settings. Keys are case-insensitive. A bad value keeps the old one and prints `[!] sc-offline.ini line N: ...`. |
| `dinput8.dll` | game `Bin64` | The mod. A foreign one is set aside as `dinput8.dll.sc-offline-backup`. |
| `sc-offline.installed` | game `Bin64` | Marker of what the launcher changed. |
| `default_1.xml` | `user\client\0` of the channel | Replaced and restored (`.sc-offline-backup`). |
| `pc-changes.txt` | `%ProgramData%\sc-offline\` | Record of PC changes. Only recorded changes are undone. Deleted only when every undo worked. |
| `data\plugins\<id>\` | launcher folder | Plugins. An empty `disabled` file keeps one off. |
| `data\storage\<id>.db` | launcher folder | Plugin settings, table `sco_kv`, keys `sco.settings.<name>`, values `<type>:<text>`. |
| `data\launcher\installed.json`, `data\launcher\` | launcher folder | Plugin install record, staging, journals. |
| `data\update\applied.txt` | launcher folder | Update journal (same format as the C++ launcher). |
| `data\launcher.log` | launcher folder | The launcher's own log; rotated to `launcher.log.1`. |

Events: `Local\sc-offline-stop-<pid>` (launcher to mod: run the exit work now) and `Local\sc-offline-stopped-<pid>` (mod to launcher: done). `stop` signals the first and waits up to 10 s for the second, then sends WM_CLOSE and waits 60 s, then asks before ending the process (`--end` ends it without asking).

Helper: when PC changes are wanted or left over, `play` starts itself again as `--helper play <Bin64> <pid> <event> <flags> [<allow>]`, elevated. The helper copies the mod in, applies the changes, signals the ready event, waits for the game to close, then undoes everything. The game starts with normal rights. Helper exit codes: 0 ok, 2 mod copy failed, 3 mod removal failed, 4 bad arguments, 5 PC change failed.

### Which game DLL is installed

The package carries two host DLLs built from the same sources. The decision and its end-to-end flow are in [rust-menu.md](rust-menu.md#how-the-new-menu-is-enabled).

- `dinput8.dll`: built with `SCO_HOST_MENU_CLIENT=OFF`, the configuration run in game. Installed by default.
- `dinput8-rust-menu.dll`: built with `SCO_HOST_MENU_CLIENT=ON`, with the Rust menu hook.

The Rust launcher installs `dinput8-rust-menu.dll` into `Bin64` under the name `dinput8.dll` only when the ini says `menu = rust` and the file is beside the launcher. In every other case it installs `dinput8.dll`. The classic launcher always installs `dinput8.dll`. When the installed DLL differs in bytes from the selected source, `play` replaces it, as it does after an update. So a change of `menu` takes effect on the next Play.

## Capability switches

Six keys in `sc-offline.ini`, each a variable the sco-core host reads (`scol_contract::capability_env`). A switch sets its variable to exactly `1` when on. When off, the launcher removes the variable from the game's environment, so a value in the player's shell or in Windows cannot turn it on. Changes apply at the next Play. `play` prints one line listing the switches that are on. The golden files in `tests/golden` hold the C++ results; the Rust crate matches them.

| Key | Variable | Capability | Evidence level | Default |
|---|---|---|---|---|
| `exp_loadout_async` | `SCO_LOADOUT_ASYNC` | `game.loadout.async` | `synthetic_only` | on |
| `exp_inventory_read` | `SCO_INVENTORY_READ` | `game.inventory.read` | `synthetic_only` | on |
| `exp_actor_stats_write` | `SCO_ACTOR_STATS_WRITE` | `game.actor_stats.write` | `off_pending_probe` | on |
| `exp_cvars_native` | `SCO_CVARS_NATIVE` | `game.cvars.read` adapter (`cvars.write` stays off) | `synthetic_only` | on |
| `exp_audio_native` | `SCO_AUDIO_NATIVE` | audio native adapter attempt (play and triggers stay false) | `synthetic_only` | on |
| `exp_entities_diag` | `SCO_ENTITIES_DIAG` | none: hooks that patch game functions to write log lines | not a capability | off |

The window's Capabilities page shows these in plain words with one evidence line each. The rest of the capability registry is listed read-only in two collapsed groups. The registry is `crates/scol-contract/src/capabilities.json`; `python tools/check_capability_names.py --core <core checkout>` checks the names against Core. Host-level switches (DataCore, game services, startup snapshot) are not exposed. The player-facing text is [capabilities.md](capabilities.md).

## Plugins and the catalogue

`scol-plugin-manager` writes only under `data\plugins\<id>\` and `data\launcher\`, and under `data\storage\<id>.db` for `settings` and `remove --purge`. It refuses to change anything while the game runs, and it never loads plugin code. A change is a journaled swap, all or nothing. Every added plugin lands with the `disabled` marker, and native plugins show a native-code warning. A local folder is not replaced without `--replace`.

Zip verifier (`scol-plugins`): checked without extracting. One top-level folder equal to the plugin id, `plugin.ini` inside and at most 16 KiB, regular files only, safe unique paths (no `..`, leading `/`, backslash, colon or control characters; no case or Unicode collisions, alternate data streams or reserved device names), no symlinks, encryption or ZIP64. Limits: 256 MiB archive, 4096 entries, 128 MiB per file, 512 MiB in total, compression ratio 200:1. The Rust verifier and the SDK Python tool agree on the shared corpus in `tests/zip-corpus/`.

Catalogue (`scol-catalogue`), on an explicit command or click only, never while the game runs, never when `check_updates = off`:

1. Fetch `index.json` and `index.json.sig` over HTTPS from `github.com` (the last redirect hop may go to `objects.githubusercontent.com` or `release-assets.githubusercontent.com`).
2. Check the signature (Ed25519, over the exact bytes, before any parsing), then a strict parse (unknown fields and duplicate keys are errors), expiry, and the sequence number (no rollback).
3. Offer only first-party and reviewed entries with a compatible plugin API and SDK range. Ids `sco`, `host`, `menu` and `game` are reserved.
4. Check size and SHA-256 against the index, then run the zip verifier, then stage and swap.

The shipped trust root is empty (`scol_contract::product::CATALOGUE_TRUST_KEYS`), so every index is refused. `plugins catalogue` without `--source` prints `no catalogue is published for this build yet` and makes no request. Adding the production key is a release-time act of the maintainer. The TEST key in `tests/catalogue/golden/` must never go there. The window's Catalogue tab reads a local folder, `data\launcher\catalogue`.

## Self-update

`update` asks the GitHub release API (`api.github.com`, `github.com`) which release is on offer on the player's channel (`update_channel`), downloads the zip, checks it against GitHub's SHA-256, reads `manifest.json` (format 1, tag equals version), refuses unlisted files and unsafe paths, and extracts only listed files with size and digest checks. It never runs while the game runs. The swap writes each file as `.update-new`, journals it, moves the old file to `.update-old`, moves the new one in and re-hashes. Then it runs the new exe's `--self-test <tag>`. Any failure restores the old files. A locked file makes the swap wait 100, 250, 500, 1000, 2000 and 4000 ms, then give up. `sc-offline.ini` and the player-owned files are never touched. After a swap the new exe takes over: a console run starts it, the window's child returns 10 and the window starts the new one.

The old C++ launcher and the Rust exe accept each other's `--self-test`, so an update works in both directions. `play` with `check_updates = on` offers the update first and installs after a yes.

## Window

`scol-ui` has five pages: Home (status, Play or Stop, Update, Repair banner), Output, Plugins (installed list, Settings per plugin, add from folder or zip, Catalogue tab), Capabilities and Settings (Repair, `clean_logs`, `language`, `menu`, `plugins`, `check_updates`, open the ini and its folder). English and Russian. Ctrl+1 to Ctrl+5 switch pages and F5 reads the status again. Text keys are generated into `src/locale_table.rs` by `crates/scol-ui/tools/gen_locale.py` from `launcher/locale_strings.h`, `locales/launcher/ru.ini` and `locale/extra_*.ini`; run it after any locale change. Fonts are read from the Windows font folder at run time (Segoe UI, then Arial, Tahoma, Calibri, Segoe symbols); none is embedded, and with none present the window exits with an error. The renderer is glow (OpenGL 3.3). It needs OpenGL 3.3 and has not been tried without a GPU driver. The CLI is the supported path under Wine. More detail: [scol-ui README](../launcher-rs/crates/scol-ui/README.md).

## Build and test

Windows, with cargo on `PATH` and the MSVC linker reachable. Use your own `CARGO_TARGET_DIR`.

```
cd launcher-rs
cargo fmt --all --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace --locked
cargo deny check
cargo build --release --workspace --locked
```

Parity and proof scripts. All use scratch folders under `SCOL_SCRATCH` (else the system temp folder) or a `--work`/`--scratch` path you give, and none starts the real game. Each folder under `launcher-rs/tests/` has a README with the details.

| Command | Shows |
|---|---|
| `python tests/status/compare_status.py --cpp <C++ exe> --rust <target>/release/sc-offline.exe` | `help` and `status` text and exit codes match the C++ launcher; `status` writes nothing |
| `python tests/play/compare_dryrun.py --cpp <C++ exe> --rust <exe>` | `play`, `install` and `uninstall` `--dry-run` output matches |
| `python tests/install/compare.py --cpp <C++ exe>` | install and uninstall file trees and logs match |
| `python tests/play/real_standin.py --rust <exe> --standin <scol-play-standin.exe>` | real helper, named events and process start against a stand-in game |
| `python tests/play/stop_standin.py ...` | the stop channel, WM_CLOSE and `--end` against a stand-in |
| `cargo run -p scol-ui --features fixtures -- --ui-self-test` | every page builds in both languages and the page actions write the right files |
| `SCOL_CPP_UPDATE_EXE=<C++ build with -DSCO_UPDATE_TEST> cargo test -p scol-update --features test-hooks` | the old updater and the new exe accept each other |
| `python tests/plugins/creative-proof.py ...`, `python tests/plugins/settings_proof.py` | the plugin chain with the real creative plugin; settings read and written by a second SQLite implementation |
| `python tests/zip-corpus/generate.py --check`, `run_python.py` | the zip corpus is current; the Python tool agrees |
| `python tools/check_launcher_mcp.py --exe <exe>` | the MCP stdio session |
| `CORE=<core checkout> OUT=<scratch> bash tests/menu/run.sh` | the menu host tests ([rust-menu.md](rust-menu.md#build-and-test)) |

The golden files under `tests/golden/` are written from the real C++ code by `tools/launcher-contract`; `python tools/check_launcher_contract.py` checks them. The MCP vectors in `tests/mcp-vectors/` are written by `tools/launcher-mcp`.

CI job `rust-launcher` (`.github/workflows/build.yml`) runs the cargo checks, the status, dry-run and install parity, the stand-in run, the window self test, the MCP session, the version check and the CRT import check, and uploads the launcher and menu for the release job. The update contract against a C++ build, the creative proof, the Python zip corpus run and the menu host run by hand. [build.md](build.md) lists the CI jobs.

`tools/package_release.py` builds the package folder and zip: `sc-offline.exe`, `sc-offline-classic.exe`, `sc_offline_menu.dll`, `dinput8.dll` (and `dinput8-rust-menu.dll`), `sc-offline.ini`, `sc-offline.sh`, documents, `data/`, plugins and `manifest.json`.

## What is built

Everything in the crate table is built. The release build gives `sc-offline.exe` (about 7.4 MB), `sc_offline_menu.dll`, `scol-pm.exe`, `scol.exe` and `scol-ui.exe`. Specifics:

- `status`, `help`, `mcp`: text and exit codes match the C++ launcher in the parity scenarios. The MCP session passes `tools/check_launcher_mcp.py`.
- `play`, `install`, `uninstall`: the whole C++ flow. Dry-run output and install trees match. A real helper process and named events work against a stand-in game.
- Game folder picker: a real Windows folder dialog (`scol_play::pick`). `play` uses it when no game is found and a person can answer; the window's Browse button uses it too. The pick is remembered; `--game` and `game =` win.
- `stop`, `update`, `plugins`, the window, the capability switches and `menu = imgui|rust` as described above.

## Not built

- Travel to other planets. Not in 0.8.0.
- A production catalogue key and a published index. The index fetch over the real network ran only through the fetcher trait with the test key.
- Russian names for capability rows that have no switch; they stay English.
- A reduced reference launcher built on these crates. sco-core has `docs/launcher-contract.md`, `docs/building-a-launcher.md` and `sdk/examples/launcher/` on the 2.4.1 line.
- CI does not run the C++ update contract, the creative proof or the menu host.

## Not run in game

- Play, the helper, the elevated (UAC) path, the shell-token game start when elevated, and the real PC changes (netsh, hosts file, EAC rename). The `Safer` normal-user token self-test ran only from a non-elevated process.
- The window against a real game. The real update swap on a real install (only the release check ran against GitHub). The window's restart after an update.
- The Rust menu: see [rust-menu.md](rust-menu.md#not-run-in-game).
- Capability switches except `exp_loadout_async`, which was run in game and worked (gear attached). The other four are on by default by maintainer decision.

The maintainer's in-game session used the C++ launcher and the classic menu.

## Decisions

Dated records are in [decisions.md](decisions.md). The ones that shape this code:

- Rust scope is the launcher and the menu. The host stays C++: it is bound to the C++ host kit, SEH guards, detours, the startup gate and the retain-until-exit rules.
- The contract between launcher and game does not change. The new launcher reads the old ini, data files and `pc-changes.txt`, and updates 0.7.x in place. Manifest format 1, tag equals `SCO_VERSION`.
- PC changes are copied exactly and never widened. The maintainer reviews `scol-pcchanges` before it replaces the C++ code in a release. The docs never say the launcher leaves anti-cheat alone.
- One binary with a hidden `--helper` mode, not a separate helper exe.
- WinHTTP for network, no rustls or ring. egui with the glow backend: 3.4 MB against 6.4 MB for wgpu and 105 against 162 crates in the spike. Fonts come from Windows, none are bundled.
- Catalogue: signed index, first-party and reviewed entries only, everything lands disabled, local plugins always allowed. Only the launcher process uses the network, and only on a user action. `status` makes no request.
- The five capability switches are on by default and visible; `exp_entities_diag` is off. Names of the launchers: Rust is `sc-offline.exe`, C++ is `sc-offline-classic.exe` and can undo PC changes from the same record.
- Two game DLLs, chosen by the launcher from `menu`. See above.
- The spike that chose glow is kept as a historical record: `launcher-rs/spike/egui-launcher/REPORT.md`.

## Open work

- Production catalogue key, a published index, and the sc-plugins build and signing job.
- Test glow on a machine with no GPU driver; add a software fallback or document that the window needs one.
- Run the PC-change checks by hand on a throwaway PC: a UNC or `\\?\` game path, and play then close with every rule, hosts line and `.bak` gone afterwards.
- A fix for the shared install limit: when the `dinput8.dll` copy fails after a fresh `default_1.xml` was copied, install does not roll back. Both launchers share this. The maintainer decides; it must change in both.
- Maintainer in-game runs of everything under "Not run in game".
