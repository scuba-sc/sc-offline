# sc-offline Rust launcher and SDK reference launcher

Status: first working build, a preview next to the C++ launcher. The C++ `sc-offline.exe` is still the launcher that starts the game; nothing is cut over. Nothing here is claimed to work in game. Every acceptance check below is source, build, synthetic or CI evidence. Docs keep the words "not run in game" until the owner records a session. The section "Build status" says what is built and what is not.


## Build status (2026-11-18)

Built in `launcher-rs/` (one workspace, `crates/*`): `scol-contract`, `scol-config`, `scol-plugins`, `scol-install`, `scol-pcchanges`, `scol-redact`, `scol-mcp`, `scol-catalogue`, `scol-play`, `scol-update`, `scol-plugin-manager`, `scol-ui`, `scol-menu` and `scol-cli`. Release build: `sc-offline.exe` (about 6 MB), `sc_offline_menu.dll`, `scol-pm.exe`, and `scol.exe` (an alias of `sc-offline.exe`, kept for the parity scripts). `cargo fmt`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace` and `cargo deny check` pass.

What the `sc-offline.exe` of the Rust workspace does:

| Command | State |
|---|---|
| `status`, `help`, `mcp [--game-log]` | Built. Text and exit codes match the C++ launcher in 60 status scenarios. Read-only, no network. |
| `play`, `install`, `uninstall` (with `--dry-run`, `--game`, `--skip-eac-check`) | Built, the whole flow of the C++ `wmain`: log rotation, update settle, discovery, EAC and running-game stops, environment and capability switches, helper, game start, wait, clean-up. Dry-run output matches the C++ launcher in 25 scenarios. A real helper process and named events work against a stand-in game process. |
| `update [--check] [--yes] [--prerelease]`, `--elevated-update`, `--self-test` | Built: release check, download, manifest and digest checks, journaled swap with rollback, recovery after an interrupted swap. Old and new launchers accept each other's `--self-test` (tested against a C++ build with the test hook). |
| `plugins list`, `enable`, `disable`, `install <folder or zip>`, `remove`, `catalogue ...`, `update` | Built, with journaled changes and refusal while the game runs. The catalogue commands read a local folder (`--source`); there is no network catalogue source yet. |
| no arguments (double-clicked), `--window` | Opens the egui window: Home, Output, Plugins with a Catalogue tab, Capabilities (the six `exp_*` switches), Settings, in English and Russian. Play, Stop, Update and Repair are connected: Play, Update and Repair run this exe as a child with `SC_OFFLINE_GUI` set (as the C++ window does); Stop uses the stop channel in the window's process. |
| `--helper`, `--delete-logs` | Built (hidden modes, dispatched by argument count as in C++). |

In-game menu (`scol-menu`, `sc_offline_menu.dll`): built on egui. It renders `sco.widgets` panels through the widgets view C interface and sends the events back. Tested against a Rust stand-in view and against sco-core's real view (core-241): 46 checks pass. The C++ host hook is `src/menu_rust.cpp`; it is opt-in (`SC_OFFLINE_MENU=rust`), the ImGui menu stays the default, and it is compiled only with `-DSCO_HOST_MENU_CLIENT=ON` against a Core that has `sco/widgets_view.h`.

Not built, or not proved:

- Not run in game: play, the helper, the window, the menu, any capability switch.
- Not exercised on a real PC: the elevated (UAC) helper, the shell-token game start when elevated, the real PC changes (netsh, hosts file, EAC rename), the live download and swap of an update (only the release check was run against GitHub).
- The window does not restart itself after an update. The folder picker is not ported: with no game found, `play` prints the `game =` message. A game that fails to start makes the Rust launcher clean up at once; the C++ launcher waits for its own exit.
- No `stop` command on the command line; Stop exists only in the window.
- The automatic update check before `play` (`check_updates = on`) prints a one-line notice only (3 s limit); it does not offer to install. Use `update` or the window's Update button.
- The catalogue has no production signing key (`TrustRoot::embedded()` is empty), so every real index is refused. There is no production index URL, so `plugins catalogue` takes a local folder. The window's catalogue download uses WinHTTP but is refused for the same reason.
- Menu: no Settings page, no `scom_host_v1` callbacks, no `menu-window.ini` or `menu-view.ini` persistence, no cursor clip or software cursor, no background image or golden-image comparison. The window opens on F8 (`SC_OFFLINE_MENU_KEY`), not on M. The `menu = imgui|rust` ini key is not written by the launcher. The menu DLL has no Drop and never unloads.
- The `release` profile uses `panic = unwind` (the default), because the menu DLL catches panics at its exported functions inside the game. The `abort` setting of the first skeleton was removed.
- The menu draws with fonts read from the Windows font folder at run time (Bahnschrift, Segoe UI, Arial, Tahoma, Calibri); egui's bundled fonts are off. With none of them present the menu does not start.
- Cutover (P8) is not done. `sc-offline-rs.exe` is shipped only as a preview in the test package.

How to re-run the checks (Windows, own `CARGO_TARGET_DIR`):

```
cd launcher-rs
cargo fmt --all --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
cargo deny check
cargo build --release --workspace
python tests/status/compare_status.py --cpp <C++ sc-offline.exe> --rust <target>/release/sc-offline.exe
python tests/play/compare_dryrun.py   --cpp <C++ sc-offline.exe> --rust <target>/release/sc-offline.exe
python tests/play/real_standin.py     --rust <target>/release/sc-offline.exe --standin <target>/release/scol-play-standin.exe
cargo run -p scol-ui --features fixtures -- --ui-self-test
SCOL_CPP_UPDATE_EXE=<C++ build with -DSCO_UPDATE_TEST> cargo test -p scol-update --features test-hooks
python tests/plugins/creative-proof.py ...    # see tests/plugins/README.md
CORE=<Core checkout> OUT=<scratch> bash tests/menu/run.sh
```

CI job `rust-launcher` runs the build, tests, deny, the status and dry-run parity, the stand-in run and the window self test; the update contract, the creative proof and the menu host are run by hand.

## Owner decisions (2026-11-17)

These override anything below that disagrees.

1. Rust scope: the launcher and the in-game menu, both now. The in-game host (dinput8.dll, hooks, Core host kit) stays C++. The Rust menu reads `sco.widgets` panels through a new versioned C interface in sco-core (see `docs/rust-menu.md`).
2. sco-core 2.4.1 is held until the Rust launcher, Rust menu, catalogue and reference launcher are in it. Read every "after 2.4.1" or "2.4.2" below as "in 2.4.1".
3. Catalogue: signed index. First-party and reviewed native plugins are allowed. Every pulled plugin lands disabled with a native-code warning. Local plugins are always allowed.
4. The five capability switches (`exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native`, `exp_audio_native`) are on by default in all release packages. Each is a visible key in `sc-offline.ini` that the player can turn off, and its text says it has not been run in game. `exp_entities_diag` is diagnostic hooks that patch game functions to write log lines. It is not a feature and is off by default.

Baseline facts used by this plan:

- SDK 2.4.1 is a release candidate. The latest published SDK is 2.4.0. sc-offline pins a development commit of sco-core (4ba313da, SDK 2.4.1-dev.6), not the 2.4.1 tag.
- The sc-offline release job is blocked on purpose until a published SDK matches the pin.
- The sco-core host kit (sco::app, plugins, game::host, ProcessRetained) is C++ with STL types and has no C ABI. The plugin ABI (sco_api.h 1.1) is frozen and only grows.
- The Rust toolchain is pinned to 1.99.0 (x86_64-pc-windows-msvc) in `launcher-rs/rust-toolchain.toml`. Developers need rustup and cargo on PATH.
- sco-core has an experimental raw Rust plugin crate (sco-sdk-raw). Safe Rust and service bindings are deferred to 2.5.0.

## 1. Goal

The owner direction, in plain words:

1. Everything is in the SDK. Nothing is cut out of it to make it "optional".
2. Opt-in means the player chooses to download a piece from the site, or to switch it on. It does not mean hidden.
3. The sc-offline launcher becomes a durable base. It pulls plugins and data from the SDK catalogue. It lets people add their own plugins.
4. The UI is rewritten in Rust.
5. sc-offline becomes the core launcher example. Anyone can build it by following the sco-core SDK instructions.

How this plan meets that, and where it stops:

- The launcher (window, CLI, installer, updater, plugin manager, settings) is rebuilt in Rust. It runs out of process and has no link to the C++ host kit, so it can move safely.
- The in-game menu is rebuilt in Rust now, in scope with the launcher (owner decision 1). It reads `sco.widgets` panels through a new versioned C interface added to sco-core.
- The in-game host (dinput8.dll, hooks, Core host kit) stays C++. It is bound to the C++ host kit, SEH guards, detours, the startup gate and the retain-until-exit rules. A Rust host would need a different, larger C ABI in sco-core and is not in this plan (section 9).
- The Rust launcher crates are designed from day one so they can move into sco-core as a reference launcher. sc-offline then becomes a product config on top of the same crates.
- sco-core 2.4.1 is held until this work is in it (owner decision 2). The new contract, header and crates land in 2.4.1, each as its own small PR.

Out of scope for the first ship: Rust in-game UI, a Rust host, a game-device overlay, new engine hooks.

## 2. Decisions list (pending owner answers)

The four decisions at the top of this document are answered and override this list. Every other item below is still pending an owner answer, with the recommended option shown. Items 1, 4, 5 and 7 are superseded by the answered decisions.

Each item has a recommended answer and the alternative. Items 1 to 8 block work. The rest can be answered as the phase arrives.

1. Rust scope.
   - Answered (owner decision 1): the launcher (window, CLI, install, update, plugin manager, settings, capabilities page) and the in-game menu. The in-game host stays C++.
   - Not chosen: also the host. That needs a new C ABI in sco-core (a permanent promise) or a fork of the kernel. Not before in-game proof of ProcessRetained.
2. Where the Rust code lives first.
   - Recommended: `launcher-rs/` Cargo workspace inside sc-offline, with a clean product-config crate boundary. Promote the reusable crates into sco-core as part of 2.4.1, which is held for them.
   - Alternative: build in sco-core first. This delays the launcher behind an SDK release.
3. Core pin.
   - Recommended: keep the dev pin until sdk-v2.4.1 is published and digests are verified. Phases P1 to P7 do not need the pin. Only cutover does.
   - Alternative: wait for an owner in-game ProcessRetained session first.
4. Trust root for the catalogue and updates.
   - Recommended: GitHub asset digest plus a signed index (embedded ed25519 public key) for the catalogue. Keep today's digest plus manifest model for launcher self-update. Record it in SECURITY.md.
   - Alternative: GitHub digest and attestation only (today, no signature), a pinned index digest compiled into the launcher, or Authenticode.
5. Native plugins from the catalogue in the first release.
   - Recommended: first-party and reviewed entries only. Native allowed, lands disabled, with a warning. Lua and data packs have no native risk. If the owner is uneasy, ship Lua and data only until signing exists.
   - Alternative: Lua and data only.
6. Base zip content.
   - Recommended: keep today's player-offline plugin set in the base zip at first. Optional extras are pulled. Shrink the base later when the catalogue is proven.
   - Alternative: small base that pulls its defaults on first run.
7. Capability switches shown in the launcher.
   - Answered (owner decision 2026-11-17): the five env-based switches get toggles, all default ON in release packages, each a visible key the player can turn OFF, each with an evidence badge and the line "not run in game". `exp_entities_diag` is a visible key too and is OFF by default. Everything else is read-only with the reason.
   - Alternative: show nothing until sco-core adds typed setters.
8. PC changes (firewall block, EAC hosts line, EAC exe rename).
   - Recommended: port exactly as is. Do not widen. Owner security review before cutover (docs/decisions.md already requires it). Docs must not say there is no anti-cheat manipulation.
   - Alternative: change defaults or drop them. That is a policy change, not part of the port.
9. UI technology and hardware floor.
   - Recommended: egui/eframe. Run a spike and pick wgpu or glow from measured size, crate count and Wine behaviour.
   - Alternative: iced with a software renderer, or slint (GPL-3.0 option) if no-GPU machines matter more than size.
10. Fonts.
    - Recommended: turn off egui default fonts. Use an owner-approved OFL or MIT font with Cyrillic.
    - Alternative: accept the Ubuntu Font Licence after a legal check.
11. Network behaviour.
    - Recommended: catalogue fetch only on explicit click. Launch-time check stays as today (short timeout, self-update only). `check_updates = off` covers both. `status` makes no request.
    - Alternative: also check the catalogue at launch.
12. Review levels offered.
    - Recommended: first-party and reviewed only. Community-unreviewed never offered (current sc-plugins rule). A second reviewer for "reviewed".
    - Alternative: unreviewed behind a strong warning.
13. Plugin index of record.
    - Recommended: sc-plugins is the source of truth. The sco-core plugin-directory JSON is generated from it or retired in P0.
    - Alternative: keep both. Not recommended: they share no schema.
14. Release manifest and plugin files.
    - Recommended: remove data/plugins from the launcher release manifest once the catalogue owns those files. This also fixes the plugin-rules.md claim that self-update never touches data\plugins.
    - Alternative: keep plugins in the manifest and fix the doc instead.
15. Compatibility promise.
    - Recommended: the new launcher reads the old ini, data files and pc-changes.txt and updates 0.7.x in place. Keep manifest format 1 and tag equals SCO_VERSION. Pick the version number (0.8.0 or other) at cutover.
    - Alternative: clean break. Strands players. Not recommended.
16. Elevation model and signing.
    - Recommended: keep one binary with a hidden `--helper` mode. Plan for code signing to reduce SmartScreen hits.
    - Alternative: a separate small elevated helper exe.
17. Wine and Linux.
    - Recommended: the CLI stays the supported Wine path. The window is best effort until tested.
    - Alternative: make Wine window support a gate.
18. Crate review.
    - Recommended: cargo-deny allow list, vendored tree, one vendored-tree hash row in the third-party inventory, owner signs the review.
    - Alternative: one inventory row per crate.
19. `mcp` command and the plugin directory.
    - The read-only `sc-offline.exe mcp` server exists on branch `feature/offline-mcp-server` (launcher/mcp_server.h). The Rust CLI must keep it (P3 parity), as a port of that header to Rust (serde_json) with the same tests.
    - K-49 is the roadmap row for the community plugin directory (sco-core docs page and JSON index, merged in sco-core #234). Decision 13 decides whether sc-plugins replaces it.
20. Human-only actions stay human-only: merge, approval, provenance review, tagging, release, in-game verification.

## 3. Architecture

### 3.1 Boundaries

```
 player PC
 +------------------------------------------------------------+
 |  sc-offline.exe  (Rust, new)                               |
 |  window UI (egui)  +  CLI  (same library, no self-spawn)   |
 |                                                            |
 |  scol-ui   scol-cli                                        |
 |     |         |                                            |
 |  scol-product (config: name, paths, default packs,         |
 |                update source, locales, shown switches)     |
 |     |                                                      |
 |  scol-install  scol-pcchanges  scol-update  scol-plugins   |
 |  scol-catalogue  scol-config  scol-contract                |
 +-----|------------------------------------------------------+
       | env block (SC_OFFLINE_*), ini, events, marker file,
       | data\plugins\<id>\, data\storage\<id>.db
       v
 +------------------------------------------------------------+
 |  StarCitizen.exe  <- dinput8.dll (C++, unchanged)          |
 |  startup gate, host kit, ProcessRetained, hooks, menu      |
 |  frozen plugin ABI sco_api 1.1 -> plugins                  |
 +------------------------------------------------------------+

 network (launcher process only, on user action):
   GitHub releases (self-update)      sc-plugins index + zips
```

The contract between launcher and game is files, environment variables and named events. It does not change. That is why the launcher can be replaced without touching the host.

### 3.2 Repos

| Repo | Change | Timing |
|---|---|---|
| sc-offline (public) | New `launcher-rs/` workspace. Old `launcher/` (C++) keeps shipping until cutover, then stays one release as fallback. `src/` (host, menu, patches, hooks) untouched. | P1 onward |
| sco-core (public) | Doc fixes first. Then, still inside 2.4.1: launcher contract doc, build-a-launcher guide, reference launcher, host-config setters. Each its own small PR. | all in 2.4.1 |
| sc-plugins (public) | Build tooling and CI that turns pinned entries into zips and a signed `index.json`. Becomes the single index. | P7 |
| sc-creative (public) | Unchanged. First catalogue entry and the worked example of an external plugin. | none |
| Private repos | Nothing flows in. Owner in-game results are booked in the private ledgers as today. | none |

### 3.3 Components and languages

| Component | Language | Notes |
|---|---|---|
| Launcher exe, library crates | Rust | Edition 2021, MSRV 1.99, pinned 1.99.0, panic=abort, crt-static, `--locked`, vendored. |
| dinput8.dll host, menu, patches, hooks | C++ (unchanged) | Built by the top-level CMake. |
| Plugins | C, C++, C#, Lua, Rust (raw) | Frozen ABI. Rust plugins use sco-sdk-raw. |
| Catalogue tooling (CI) | Python | Same style as existing sc-plugins tools. |

### 3.4 Rust workspace (`launcher-rs/`)

- `scol-contract`: constants and parsers for the frozen contracts. SC_OFFLINE_* names, stop and ready event names, marker file, `.sc-offline-backup` suffix, pc-changes.txt record, ini keys, player-owned file list, manifest format 1, `sco_kv` key format. No UI and no network. Golden vectors live in `tests/golden/`.
- `scol-config`: ini read and in-place edit, env block builder.
- `scol-install`: game discovery, mod put and take, marker, crash-leftover handling, read-only status.
- `scol-pcchanges`: firewall rules, LAN-only rule, hosts line, EAC exe rename. Copied exactly.
- `scol-update`: HTTPS-only fetch, hard-coded hosts, byte caps, timeouts, digest and manifest verify, staging, journaled swap, rollback, `--self-test` gate. Same guarantees as today.
- `scol-plugins`: plugin.ini and `[settings]` parser (same rules as the host), `disabled` marker, settings store, zip verifier, install journal, `installed.json`.
- `scol-catalogue`: index fetch and verify, compatibility check, capability registry reader.
- `scol-product`: the product-config struct. This is the seam that sc-offline fills in and the reference launcher reuses.
- `scol-cli`: play, install, uninstall, status, update, mcp, help. Hidden: `--helper`, `--self-test`, `--apply-zip`, `--elevated-update`, `--delete-logs`. Output text and exit codes (0, 1, 2, 3, 10) stay compatible.
- `scol-ui`: egui window. Pages: Home, Output, Plugins, Capabilities, Settings. It calls the libraries directly. It never spawns itself.

## 4. Opt-in model

Principle: the SDK ships everything. Opt-in is a visible choice to download or to enable. It is never a hidden switch and never a removal from the SDK.

### 4.1 "Full" SDK view

Add a `full` view in `sdk/components.json` that is the union of all profiles (core, game-sc, source kits, launcher kit, first-party plugin sources, examples). Existing profiles stay as slices of it. Archives stay byte-reproducible with SBOM and SHA256SUMS. This is sco-core work inside 2.4.1.

### 4.2 Three kinds of choice

1. Download choice (Plugins page, Catalogue tab). First-party plugins become catalogue entries, marked bundled-default or optional. Nothing downloads until the player clicks.
2. Enable choice (Plugins page, installed list). Every pulled or locally added plugin lands disabled. Enabling is a separate act. Native plugins show a plain warning: trusted in-process code, no sandbox.
3. Capability choice (Capabilities page). Hidden environment variables become documented toggles.

### 4.3 The capability switches

The launcher already builds the game's environment, so it can set these with no sco-core change. Each switch is a key in `sc-offline.ini`, a toggle in the window, and a value of exactly `"1"` only when on. The key is `exp_` plus the variable name without `SCO_`, in lower case; `exp_` is a name prefix, not a label on the feature. Default is ON in all release packages for the five capability switches (owner decision 2026-11-17). Each is a visible setting the player can turn OFF, and its text says it has not been run in game. When a key is off the launcher removes the variable from the game's environment, so a value left in the player's shell or in Windows cannot turn it on. The C++ launcher already does this (`ApplyExperimentalEnv`) and the golden files under `launcher-rs/tests/golden` hold its results; the Rust crates reproduce them byte for byte. Changes apply at next Play and are blocked while the game runs.

| Key (variable) | Capability | What it needs besides the toggle | Evidence | Launcher offers |
|---|---|---|---|---|
| exp_actor_stats_write (SCO_ACTOR_STATS_WRITE) | game.actor_stats.write | read rows, fresh offline admission, own player, stat 1..9 | off_pending_probe | toggle, ON by default, warning |
| exp_inventory_read (SCO_INVENTORY_READ) | game.inventory.read | inventory rows | synthetic_only | toggle, ON by default |
| exp_loadout_async (SCO_LOADOUT_ASYNC) | game.loadout.async | six loadout and spawn rows | synthetic_only | toggle, ON by default |
| exp_cvars_native (SCO_CVARS_NATIVE) | game.cvars.read adapter | seven cvars rows | synthetic_only | toggle, ON by default. `cvars.write` stays off even when on. |
| exp_audio_native (SCO_AUDIO_NATIVE) | audio native adapter attempt | grounded witness for play_once and triggers | synthetic_only | toggle, ON by default. Play and triggers stay false. |
| exp_entities_diag (SCO_ENTITIES_DIAG) | none: diagnostic hooks | none | not a capability | shown under Diagnostics. It installs hooks that patch game functions, only to write log lines to mod.log. It is not a feature and the 2026-11-17 decision on capability switches does not cover it: it is OFF by default, and the player turns it on only when asked for a bug report. |

Every row shows its evidence level and the line "not run in game". No row can show "verified", because no registry entry is `in_game_verified`.

What stays off, and why:

- The other default-OFF capabilities have no switch. Reasons are in source: constants held `probed=false`, `kNativeVerified=false`, `kQueryRadiusConfirmed=false`, forced-false states, or no native backend at all (audio triggers, container contents, free camera, entity lookup, loadout apply, gamerules read, economy, missions, inventory layout, relocation position). The page lists them read-only with the reason from `sdk/status_registry.json`. A toggle that does nothing would mislead.
- Host-level switches (dataCore, gameServices, loadFromCopies, startup snapshot, runtimeInspection) are not exposed. gameServices is refused by the public path and is only reachable through ProcessRetained. The product already sets what it needs.
- `game.quantum.control` has no env switch and is not offered.

The page table is generated from the pinned `sdk/status_registry.json`. A drift test fails when the registry gains or loses a capability and the table is not regenerated. A wording check bans claims of in-game behaviour.

Later (a typed host-config file; graft from the SDK-first plan): a typed host-config file and public setters for the env-only switches (loadout async, cvars native, audio native, and a public inventory-read setter, which today sits in an internal header). Env stays as a developer override. Each setter ships with a contract record and a synthetic test. Defaults do not change without an in-game probe, and registry, docs and tests move together.

## 5. Plugins

### 5.1 Pull from the catalogue

All steps run on explicit user action, in the launcher process only, never while the game runs.

1. Fetch `index.json` over HTTPS from a hard-coded host. Byte cap and per-read timeouts.
2. Verify the index against the trust root. Fail closed on a bad signature, duplicate key, unknown field, stale version or expired index.
3. List only first-party and reviewed entries. Show kind, licence, SDK range, maintainers and the evidence note.
4. Check compatibility before download: plugin api major equal and minor not newer than the host, `requires` resolvable, id not reserved (sco, host, menu, game), no clash with a local folder, SDK range matches the pinned SDK.
5. Download the built zip. Check SHA-256 against the index. Run the zip verifier without extracting.
6. Stage under `data\launcher\staging`. Re-verify. Swap into `data\plugins\<id>\` with a per-file journal and rollback, copying the updater's pattern. Write `data\launcher\installed.json` (id, version, source, zip digest, index digest, date, state).
7. The plugin lands with the `disabled` marker.
8. Update uses the same flow. Downgrade only on an explicit click. `data\storage\` is never touched. Uninstall removes the folder, `.reload-*` leftovers and the record, and keeps settings unless the player asks.

The launcher never loads or runs plugin code. `sco-plugin-check` executes native DLLs, so it runs only in CI and on developer machines. Its result is recorded in the index as development evidence, not a safety claim.

### 5.2 Zip verifier rules

A Rust port of `sco-plugin-verify.py` with the same limits and shared test vectors: one top-level folder equal to the plugin id, plugin.ini inside and at most 16 KiB, entry is a regular file, safe unique paths (case and Unicode collisions), no alternate data streams, reserved device names, symlinks, encryption or ZIP64, and at most 256 MiB, 4096 entries and 200:1 ratio. Path rules match the updater (no `..`, no leading `/`, no backslash, colon or control characters).

The Rust verifier and the Python tool must agree on a shared corpus of good and hostile zips. The corpus is checked in and run by both.

### 5.3 Add your own plugin

- Drop a folder or zip on the Plugins page, or run `sc-offline install-plugin <path>`.
- A zip goes through the same verifier. A folder is checked with the host's plugin.ini rules so launcher and host agree.
- Recorded as `source = local, unverified`, default disabled, with a native-code warning. Hand-copied folders still work and show as local.
- A local plugin cannot be overwritten by a catalogue plugin of the same id without a confirm. Updates and the catalogue never delete unlisted folders.
- Authors use the SDK: `sco-new-plugin.py`, `sco-plugin-check`, `sco-plugin-verify`. To list in the catalogue they open a sc-plugins PR with an `entry.toml` pinned to a 40-hex commit. CI builds against a published SDK. A maintainer sets the review level.
- Later, with the 2.5.0 Rust work: a Rust scaffold in `sco-new-plugin.py`, a cargo helper, and a note for authors (panic=abort, crt-static or a documented VC runtime).

### 5.4 Offline rules

- The launcher is the only process that talks to the network. Plugins keep only `sco.ipc` and `sco.net`. No telemetry.
- No network while the game runs. `status` stays network-free. `check_updates = off` also turns off catalogue checks.
- A fully offline install from a local zip or folder works. Index and plugin bytes may be cached locally.
- Installs write only under `data\plugins\<id>\` and `data\launcher\`, only while StarCitizen.exe is not running.

## 6. The launcher as the SDK reference example

Target: sco-core 2.4.1 (held until it is ready) carries a reference launcher that anyone can build from the kit. sc-offline is the full product built on the same crates.

### 6.1 What goes in sco-core (2.4.1, one small PR per item)

1. `docs/launcher-contract.md`: env var table including the five switches, plugin folder and `disabled` marker, `sco_kv` settings key format, `plugin-startup.json`, stop and ready event names, package and compatibility rules. Golden vectors ship with it.
2. `docs/building-a-launcher.md`: end to end. Bootstrap (dinput8 forwarder and startup gate), Platform fields, Admission inspector, `gameReadersQuiesced` attestation, ProcessRetained rules, Start/Tick/Stop thread rules, data root layout, plugin install, capability switches. Builds on `tools/sco-host-sim.cpp` and framework.md lessons 1 to 6.
3. `sdk/examples/launcher/`: a reduced Rust reference launcher. CLI, plugin list, enable and disable, verified install from a local zip, capabilities page stub. No PC changes, no auto update. Ships in the core and combined kits like `rust_hello`. Its text says not run in game until the owner records a session.
4. A `launcher-kit` profile and the `full` view in `components.json`, with provenance and third-party rows. `sdk/package.py`, `test_package.py` and `test-zip.sh` must pass.
5. A `sco-build-launcher` skill under `sdk/skills`, and `sco-package-plugin` extended with install and catalogue steps.
6. A named "launcher seam" page listing what sco-core will not provide: injection, offline patches, OS admission, menu renderer.

### 6.2 What stays in sc-offline

The dinput8 proxy, offline patches and hooks, admission inspector, quiescence attestation, PC changes, branding, locales, default pack set, release packaging, and the product-config instance.

### 6.3 How someone builds it from the SDK instructions

1. Download the core kit (and `core-source` or `host-source-sc` if building a host) from the release, verify against SHA256SUMS.
2. Follow `docs/building-a-launcher.md`.
3. `cargo build --locked --offline --release` for the reference launcher with the pinned toolchain.
4. Run it against a sample plugin folder. List, verify and install a plugin from a local zip.
5. Acceptance: a person who did not write the guide completes steps 1 to 4 on a clean machine, and the reference launcher passes the same golden vectors as sc-offline for the contracts they share.

The reference launcher does not start a real game. Starting and admission stay product duties, and the guide says so.

## 7. Rust stack

Licences and sizes are from memory and were not checked. Run `cargo deny check licenses bans` and vendor the tree before adoption. Third-party rows go in `docs/legal/third-party-inventory.md` and `THIRD_PARTY_NOTICES.md` (or one vendored-tree hash row).

| Crate | Purpose | Licence (expected) |
|---|---|---|
| egui, epaint, emath, ecolor | UI | MIT OR Apache-2.0 |
| eframe (default-features off) | window shell, wgpu or glow backend | MIT OR Apache-2.0 |
| wgpu family, naga (or egui_glow, glow) | renderer, chosen by spike | MIT OR Apache-2.0 (glow also Zlib) |
| winit | windowing | Apache-2.0 |
| windows-sys | WinHTTP, BCrypt, Win32, shell dialogs | MIT OR Apache-2.0 |
| serde, serde_json | settings, index, release metadata | MIT OR Apache-2.0 |
| rusqlite (bundled) or cc on Core's sqlite3.c | settings store | MIT (rusqlite), public domain (SQLite) |
| ed25519 verify crate (small, audited) | index signature, if decision 4 chooses it | to be reviewed |
| cargo-deny | licence and ban gate (build tool) | tool, not shipped |

Avoid: rustls and ring (the current launcher uses WinHTTP), the `dirs` family (MPL-2.0), rfd and arboard unless needed, webview-based stacks. Fonts: egui default fonts include Ubuntu Light under the Ubuntu Font Licence. Turn them off and ship an approved OFL or MIT font with Cyrillic.

Build settings: `rust-toolchain.toml` pinned to 1.99.0, `.cargo/config.toml` with `+crt-static`, `panic = "abort"`, LTO, opt-level s, strip, `/Brepro` link arg, `--remap-path-prefix`, committed `Cargo.lock`, `--locked --offline` from a vendored tree. Check reproducibility by a double build and hash compare.

Expected: no copyleft in the tree, 150 to 400 transitive crates (egui with wgpu is the large end). The spike measures binary size, crate count and cargo-deny output before the choice.

## 8. Migration phases

2.4.1 stays shippable throughout. Phases P1 to P7 never touch `src/` or `launcher/`, and need no new pin. Only P8 moves the pin.

### P0. Decisions, toolchain, baseline (2 to 3 days)

- Deliverable: owner answers (section 2). `rust-toolchain.toml`. A documented way to put cargo on PATH. A build confirms cargo finds the MSVC linker. sco-core doc-fix PRs for 2.4.1: `docs/game/host.md` header (still says "do-not-merge host-source experiment"), `docs/defaults-readiness.md` baseline (2.4.0) and "startup refused" wording, `plugin-rules.md:79`. Choose the single plugin index. sc-offline docs updated for the real pin (4ba313da) and the version drift (0.7.8.3 versus 0.8.0).
- Acceptance: `cargo --version` works from the documented shell. A hello crate builds with crt-static and `dumpbin` shows no dynamic CRT import. Doc PRs pass sco-core CI. Decision list answered or deferred in writing.

### P1. Contract freeze (1 to 2 weeks)

- Deliverable: golden files and a C++ test tool that dumps today's launcher results: ini parse and edit, env block per config, marker text, pc-changes.txt format, manifest accept and reject cases, `sco_kv` format, plugin.ini and `[settings]` cases, discovery states, status text, exit codes. Reuse the install-discovery tests.
- Acceptance: golden files committed. The C++ launcher passes its own golden checks in CI. Each contract item is a named test. No behaviour change.

### P2. Rust libraries, no UI (3 to 4 weeks)

- Deliverable: `scol-contract`, `scol-config`, `scol-install`, `scol-pcchanges`, `scol-update`, `scol-plugins`, `scol-product`. cargo-deny config. Vendored tree. Inventory rows. CI job on the Windows runner with `--locked --offline`.
- Acceptance: all golden tests pass against the Rust libraries. `cargo deny check` passes. Double build gives identical hashes, or the gap is recorded. A public-hygiene scan (no names, emails, private paths) passes.

### P3. CLI parity (4 to 6 weeks)

- Deliverable: Rust CLI with play, install, uninstall, status, update, mcp (port of launcher/mcp_server.h with its tests), help, and the hidden modes. Stop channel, ready events, helper cleanup, crash leftovers, elevation only for helper and PC changes, game started with normal rights.
- Acceptance: side-by-side run on a scratch folder (never a real game folder or a running game) gives the same files, marker and env block as the C++ launcher. Update tests pass, including the old updater contract: the old launcher stages the new exe, runs `--self-test`, swaps, and rolls back when `--self-test` fails. dumpbin and mt checks pass (static CRT, asInvoker, Common-Controls 6). Game start stays not run in game.

### P4. Window UI (4 to 5 weeks)

- Deliverable: egui window with Home, Output, Plugins, Settings. Direct library calls. en and ru from the existing generator. Spike first (Play and Status only) to pick the backend.
- Acceptance: spike report committed (size, crate count, start on a machine without a GPU, Wine). `check_launcher_localization` passes. Plugin page matches current features against the same golden data. Screenshots of each page in en and ru.

### P5. Capabilities page (1 to 2 weeks)

- Deliverable: page, the `exp_*` ini keys, env entries for the five switches, the diagnostic key, read-only rows for the rest, `docs/capabilities.md`, updates to `docs/data-files.md` and `docs/launcher.md`, a generator from the pinned registry.
- Acceptance: a test sets each switch and checks the env block holds exactly `"1"` for that name when on and no entry when off. The five capability switches are ON and `exp_entities_diag` is OFF in the release config, and each can be turned off. A test over the shipped `launcher/sc-offline.ini` checks this. No row shows verified. A drift test fails on registry change. A wording check bans in-game claims.

### P6. Plugin manager (3 to 4 weeks)

- Deliverable: verify, install, uninstall, local add, journal, rollback, `installed.json`, disabled by default, native warning. Works from a local zip first with no network.
- Acceptance: the Rust verifier and `sco-plugin-verify.py` agree on the shared corpus (traversal, case collision, alternate data streams, device names, symlink, zip bomb, oversize plugin.ini, id not equal folder). Install, update, uninstall and crash-mid-install rollback pass. Install is refused while a stub StarCitizen.exe runs. A file-system diff test shows nothing is written outside `data\plugins\<id>\` and `data\launcher\`.

### P7. Catalogue (3 to 4 weeks, after decision 4)

- Deliverable: sc-plugins `tools/build_index.py` and CI that builds each pinned entry against a published SDK (after checking SHA256SUMS, as sc-creative does), runs the verifier, uploads zips, and writes a signed `index.json` with zip SHA-256, SDK range, review level and evidence note. Launcher client. First entry: creative. The sco-core directory JSON is retired or generated from the index.
- Acceptance: schema check in CI with duplicate-key rejection. Launcher refuses a tampered index, a wrong zip hash, an expired index, an unreviewed entry, and an incompatible api or SDK range (one test each). `check_updates = off` disables fetch. `status` makes no request (tested). Install lands disabled. Creative installs into a scratch folder and passes `sco-plugin-check` there. Index rebuild twice gives identical bytes.

### P8. Cutover (2 to 3 weeks, after sdk-v2.4.1 is published)

- Deliverable: pin PR moving `external/sco-core` to the tag commit after digests are verified. Release job builds the Rust launcher with the C++ dinput8.dll, stages plugins, writes manifest format 1 (tag equals SCO_VERSION). The blocked first step of the release job is removed only by this PR. Plugins leave the release manifest. C++ launcher kept one release, then removed in its own PR. Migration doc for 0.7.x players.
- Acceptance: existing dumpbin, mt and string-absence checks pass. A 0.7.x install on a scratch folder updates in place and keeps `sc-offline.ini` and player-owned files. CHANGELOG and `docs/status.md` say game start is not run in game until the owner records a session. Merge, tag and publish are human acts.

### P9. SDK reference launcher (3 to 4 weeks, inside sco-core 2.4.1)

- Deliverable and acceptance: section 6.1 and 6.3. sc-offline pins 2.4.1 in a separate PR once it is published.

### Track B. Later, separately gated (not in the first ship)

- Host-config file and setters (section 4.3).
- Rust plugin support (2.5.0 line): service-table bindings, scaffolder, cargo helper, safe layer.
- In-game UI: first port ImGui-using plugins (gear_presets, inspector, loadout, titanlink, pickers, quantum controls, spatial travel) to `sco.widgets` 1.4. The Rust in-game menu is in scope now (owner decision 1). It reads panels through a new versioned flat C snapshot interface for widgets and surfaces, added to sco-core (header, wrapper, contract record and tests); the design goes in `docs/rust-menu.md`, still to be written. Keep an ImGui fallback during transition. A game-device overlay needs a graphics-API check, a Present hook through the existing hook owner, and an owner in-game run. None of this has been run in game.

## 9. What stays C++ and why

- dinput8 proxy and startup gate: ordinal-1 forwarder, entry-point parking, static CRT, SegmentHeap. Must run in the game process before the game's main thread.
- The sco host kit (sco::app, plugins, game::host, ProcessRetained): C++ with STL types in its API, no C ABI, SEH guards and detours. A Rust host needs either a new versioned C ABI in sco-core or a kernel fork. A fork would copy the callout guard, owner gates and ProcessRetained rules. Neither is in this plan.
- Admission evidence and quiescence attestation: safety-critical and only synthetic-tested. Moving them is not worth the risk before an in-game session and a human review.
- patches.cpp and hooks.cpp: the offline patches and hooks. They migrate into Core game-pack services over time under the owner rule of zero product engine code. Not part of this rewrite.
- The ImGui menu: it is already its own window and device (no Present hook). Shipped plugins draw into sc-offline's ImGuiContext. Replacing it breaks them until they move to `sco.widgets`.
- Plugins: the frozen ABI 1.1 is not touched.

## 10. Risks

1. Existing players break. The Rust exe must satisfy the old updater (manifest, digest, `--self-test`) or players are stranded. Mitigation: P3 old-to-new update test with forced rollback, C++ launcher kept one release.
2. PC changes and the elevated helper sit near the anti-cheat boundary. Mitigation: copy exactly, golden tests, scratch-folder runs, owner security review before cutover. Never widen. Docs do not claim "no anti-cheat manipulation".
3. Catalogue trust. Releases are unsigned today. Anyone who can publish a release can publish an update or index. Native plugins run in the game with full rights and no sandbox. Mitigation: signed index, first-party and reviewed only, land disabled, verify before write, never run plugin code in the launcher, record the trust root in SECURITY.md. Option: Lua and data only first.
4. Supply chain and licences. A 150 to 400 crate tree needs review. Font licence unclear. Licence data here is from memory. Mitigation: cargo-deny, vendoring, measured spike, approved font.
5. GPU and Wine. wgpu has no D3D11 backend. glow needs OpenGL 3.3. Wine is unverified today. Mitigation: CLI stays the Wine path, spike decides, software renderer fallback.
6. Timeline coupling. sdk-v2.4.1 is unpublished and the release job is blocked. Mitigation: P1 to P7 do not need the pin. Do not tag from the dev pin.
7. Scope creep into the host. Pressure to rewrite dinput8 in Rust would force a C shim or a kernel fork and break "sc-offline consumes only published SDKs". Mitigation: Track B, owner-gated, sco-core contract first.
8. Logic drift from Core. The launcher re-implements the settings grammar, `sco_kv` format and discovery states. Mitigation: shared golden vectors, published with the reference launcher, CI check on grammar or registry change.
9. Misleading UI. Players may read a toggle as a working feature. Mitigation: five real switches and one diagnostic key only, evidence badge and "not run in game" on every row, wording check.
10. Two plugin indexes drift. Mitigation: one source of truth chosen in P0.
11. Updater and catalogue fight over `data\plugins\`. Mitigation: plugins leave the release manifest (decision 14).
12. Evidence honesty. Builds, mocks and checker passes are not game verification, and ProcessRetained has never run in a game session. Mitigation: wording gates. The owner runs and records the first session.
13. Public hygiene. New crates, docs and fixtures may leak private paths, names or ledger content. Mitigation: path and name scan in CI for `launcher-rs/` and sc-plugins.
14. Provenance. AI-authored files need `needs-review` rows and human rights review. Code moved from sc-offline to sco-core needs per-file provenance and licence headers.

## 11. First three PRs

All are small, reviewable, and touch no game-facing behaviour. No commit, push or merge is done by this plan. Each PR is opened by a human or on the owner's go-ahead.

1. sco-core: documentation fixes for the 2.4.1 candidate.
   - `docs/game/host.md` header, `docs/defaults-readiness.md` baseline and "startup refused" wording, `sdk/docs/plugin-rules.md` line 79 (or the updater decision).
   - Acceptance: docs checks pass. No header, contract or registry change. Words "not run in game" kept.
2. sc-offline: docs and toolchain baseline.
   - Correct the pin and version drift in `docs/status.md`, `docs/host-composition.md` and `docs/sdk-candidate.md`. Add `launcher-rs/rust-toolchain.toml`, `.cargo/config.toml` (crt-static), `deny.toml`, an empty workspace with `scol-contract`, and a CI job that builds it with `--locked`. Record the decisions in `docs/decisions.md` and add this design under `docs/`.
   - Acceptance: CI green. `dumpbin` shows no dynamic CRT import on the hello binary. No change under `src/` or `launcher/`.
3. sc-offline: contract freeze (P1 start).
   - Golden files and the C++ dump tool for ini, env block, marker, pc-changes.txt and manifest cases. Named tests for each contract item.
   - Acceptance: the C++ launcher passes its golden checks. No behaviour change. These vectors become the Rust acceptance suite and later ship with the reference launcher.

After these three: P2 libraries, in small PRs per crate.
