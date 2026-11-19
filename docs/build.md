# Build, check and release

This page is for people who build sc-offline from source. To play, use the release zip and the [README](../README.md).

## What you build

| Output | Built by | What it is |
| --- | --- | --- |
| `dinput8.dll` | CMake, `SCO_HOST_MENU_CLIENT=OFF` | The mod. This is the build that has run in game, and the one the launcher installs by default. |
| `dinput8-rust-menu.dll` | CMake, same sources, option `SCO_BUILD_RUST_MENU_HOST=ON`, target `dinput8_rust_menu` (built with the menu client on) | The mod with the Rust menu hook. It has not been run in game. The Rust launcher installs it as `dinput8.dll` only when `menu = rust` is set and this file sits beside the launcher. |
| `sc-offline.exe` | Cargo, `launcher-rs/` | The Rust launcher players run. |
| `sc_offline_menu.dll` | Cargo, `launcher-rs/` | The Rust in-game menu. |
| `sc-offline-classic.exe` | CMake, target `sc-offline` | The C++ launcher, the fallback. It always installs `dinput8.dll`. |

See [Rust launcher](rust-launcher.md) and [Rust menu](rust-menu.md) for the launcher and the menu themselves. What has and has not been run in game is in [status.md](status.md).

## Get the source

```
git clone --recurse-submodules https://github.com/scuba-sc/sc-offline.git
```

Two submodules are needed:

- `external/sco-core` is [sco-core](https://github.com/scuba-sc/sco-core): the scanners, signature rows, host kit, plugin loader and sco-lua. It is built into `dinput8.dll`.
- `external/sc-creative` is the Creative plugin. The plugin build, the package check and the release proof read it from there.

In an existing clone, or after a pull that moves a pin, run `git submodule update --init`. Without it, CMake configure and `tools/check.sh` stop and say so.

The host builds from the exact `external/sco-core` commit, which is a development revision of the SDK 2.4.1 line. A fresh checkout needs that commit's objects, and origin cannot serve it until the SDK is published. Rebuild host and Core together. Do not track a moving `main` or copy plugin SDK headers over host source. Plugin proofs use a verified SDK archive instead of the host pin; see [SDK coordinates](sdk-candidate.md).

## Build the two game DLLs (Windows, x64, MSVC)

Install **Visual Studio 2026** (or 2022) with the **Desktop development with C++** workload. It includes CMake. Run these from a Developer PowerShell at the repository root. On Visual Studio 2022, use `-G "Visual Studio 17 2022" -A x64 -T v143` instead of the first line's generator.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DSCO_HOST_MENU_CLIENT=OFF -DSCO_BUILD_RUST_MENU_HOST=ON
cmake --build build --config Release --parallel --target dinput8 dinput8_rust_menu sc-offline
```

One configure builds both DLLs from the same source list. `dinput8` follows `SCO_HOST_MENU_CLIENT` (`OFF`: the configuration that has run in game). The target `dinput8_rust_menu` always builds with the host menu client and the Rust menu hook, and writes `dinput8-rust-menu.dll`. Changing the menu client changes how the ImGui menu gets its pages at about a dozen places in `src/`, so the two DLLs are different programs. Without `-DSCO_BUILD_RUST_MENU_HOST=ON` the second target does not exist.

The outputs are in `build\bin\Release\` (`bin\` of the build folder with a single-configuration generator such as Ninja):

- `dinput8.dll`, `dinput8-rust-menu.dll` and `sc-offline.exe` (the C++ launcher, shipped as `sc-offline-classic.exe`).

With `SCO_HOST_MENU_CLIENT=OFF`, the DLL has no Rust hook, and `menu = rust` does nothing except write one line to `mod.log` saying the new menu needs the Rust launcher.

### CMake options that change the menu

| Option | Default | Effect |
| --- | --- | --- |
| `SCO_HOST_MENU_CLIENT` | `OFF` | Builds the host menu client (`src/menu_widgets.cpp`) and the Rust menu hook (`src/menu_rust.cpp`) into the DLL, and changes how the ImGui menu gets its pages. Needs a Core with `sco/host_menu.h`, a widgets 1.3 renderer and, for the Rust hook, `sco/widgets_view.h`. |
| `SCO_BUILD_RUST_MENU_HOST` | `OFF` | Adds the target `dinput8_rust_menu`, which builds the same sources with the menu client on and writes `dinput8-rust-menu.dll`. |

The `OFF` build is the one that has run in game, so it is the default and the one CI builds. The `ON` build has never run in game. A release carries both, and its notes say so.

The root targets are `dinput8`, the second DLL target, `sc-offline`, `imgui`, three test plugins (`spawn_probe`, `builtins_probe`, `ui_probe`) and the linked sco-core libraries. All use the static CRT. Release enables whole-program optimization.

The host registers no built-in feature plugins. Feature plugins build separately against the verified SDK; see [SDK workflows](sdk-workflows.md). The player release stages the `player-offline` profile of [`tools/release-plugin-profiles.json`](../tools/release-plugin-profiles.json): 12 native modules (spawn, crew, build, mining, npc, inspector, teleport, quantum, contracts, creative, loadout and discord), whose manifests and DLLs staging verifies. Discord keeps its disabled marker. `gear_presets` is a development consumer, TitanLink and voxel_bridge are separate optional bridges, and `sc-mp` is a separate co-presence consumer. The [scuba-engine hostd profiles](https://github.com/scuba-sc/scuba-engine/blob/main/docs/engine/profiles.md) pick plugins for synthetic, headless checks only.

No Visual Studio? CI builds on every push to `main`. Download the `dinput8-release` artifact from [Actions](https://github.com/scuba-sc/sc-offline/actions).

## Build the Rust launcher and menu

The workspace is `launcher-rs/`, 14 crates under `crates/`: `scol-contract`, `scol-config`, `scol-plugins`, `scol-install`, `scol-pcchanges`, `scol-redact`, `scol-mcp`, `scol-catalogue`, `scol-play`, `scol-update`, `scol-ui`, `scol-plugin-manager`, `scol-menu` and `scol-cli`. You need `rustup` (the toolchain is pinned in `launcher-rs/rust-toolchain.toml`: Rust 1.99.0, `x86_64-pc-windows-msvc`) and the MSVC linker from the Visual Studio workload.

```powershell
cd launcher-rs
cargo fmt --all --check
cargo clippy --workspace --all-targets -- -D warnings
cargo build --release --workspace
cargo test --workspace
cargo deny check
```

Set your own `CARGO_TARGET_DIR` when several builds share a PC. Add `--locked` in CI so `Cargo.lock` must already be current.

### Release binaries

Build the binaries that go into a release package with `tools/build_rust_release.py`, not with a plain `cargo build --release`:

```powershell
python tools/build_rust_release.py --target-dir <scratch folder> --out <folder>
```

It runs `cargo build --locked --release --workspace` in `launcher-rs/` and copies `sc-offline.exe` and `sc_offline_menu.dll` into `--out`. `--source <repository root>` picks another checkout, and `--dry-run` prints the cargo command and builds nothing. The reason: a plain release build writes the builder's cargo home, toolchain folder and target folder, which can include a Windows user name, into the binaries (panic messages carry source paths). The script remaps those folders to fixed names, keeps the flags in `launcher-rs/.cargo/config.toml` (static C runtime, reproducible link), ignores a `RUSTFLAGS` variable because it would replace them, and then scans both files for build-machine paths. A hit fails the build. It installs, tags and publishes nothing. Pass its two output files to `package_release.py` as `--rust-exe` and `--menu-dll`.

Outputs in `target\release\`: `sc-offline.exe` (about 6 MB, no dynamic C runtime), `sc_offline_menu.dll`, `scol-pm.exe`, and `scol.exe` (an alias of `sc-offline.exe` for the comparison scripts).

Checks that need no game. The `rust-launcher` CI job runs them, and [Rust launcher](rust-launcher.md) says what each shows:

```powershell
python tests/status/compare_status.py --cpp <C++ sc-offline.exe> --rust target/release/sc-offline.exe
python tests/play/compare_dryrun.py   --cpp <C++ sc-offline.exe> --rust target/release/sc-offline.exe
python tests/play/real_standin.py     --rust target/release/sc-offline.exe --standin target/release/scol-play-standin.exe
python tests/install/compare.py       --cpp <C++ sc-offline.exe> --rust target/release/examples/install_probe.exe   # after: cargo build --release -p scol-install --example install_probe
python tests/zip-corpus/generate.py --check
cargo run -p scol-ui --features fixtures -- --ui-self-test
python ../tools/check_launcher_mcp.py --exe target/release/sc-offline.exe
```

Run by hand only:

- the update contract against a C++ build made with `-DSCO_UPDATE_TEST`: `SCOL_CPP_UPDATE_EXE=... cargo test -p scol-update --features test-hooks`
- the Creative plugin proof: `tests/plugins/creative-proof.py`
- the in-game menu host: `CORE=<Core checkout> bash tests/menu/run.sh` (needs a Core with `sco/widgets_view.h`)

These are source, build and synthetic checks. None of them has been run in game; see [status.md](status.md).

## Build a release package without CI

`tools/package_release.py` assembles the folder and zip that the `release` job publishes, from build outputs you give it. It needs Python 3 only.

```powershell
python tools/package_release.py --tag v0.8.0 --out dist `
  --rust-exe  <out>\sc-offline.exe `
  --classic-exe build\bin\Release\sc-offline.exe `
  --menu-dll  <out>\sc_offline_menu.dll `
  --dinput8   build\bin\Release\dinput8.dll `
  --dinput8-rust-menu build\bin\Release\dinput8-rust-menu.dll `
  --artifact-root <folder with built-external\, built-inspector\, built-npc\, built-contracts\> `
  --check-tree
```

It writes `dist\sc-offline-v0.8.0\` and `dist\sc-offline-v0.8.0.zip`:

| In the package | From |
| --- | --- |
| `sc-offline.exe` | the Rust launcher (`--rust-exe`) |
| `sc-offline-classic.exe` | the C++ launcher (`--classic-exe`) |
| `dinput8.dll` | the mod built with `SCO_HOST_MENU_CLIENT=OFF` (`--dinput8`); installed by default |
| `dinput8-rust-menu.dll` | the mod built with `SCO_HOST_MENU_CLIENT=ON` (`--dinput8-rust-menu`); installed as `dinput8.dll` only when `menu = rust` |
| `sc_offline_menu.dll` | the Rust menu (`--menu-dll`) |
| `sc-offline.ini`, `sc-offline.sh`, `README.md`, `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | the repository |
| `docs\`, `data\` | the tracked files (`git ls-files`; `data\buildables.txt` moves into the `build_catalog` pack) |
| `data\plugins\picker_catalog`, `build_catalog`, `locations_catalog`, `loadout_lists` | list packs copied from `data\ships.txt`, `npcs.txt`, `buildables.txt`, `locations.txt`, `items.txt`, `outfits.txt` and `loadout_slots.txt` |
| `data\plugins\<id>\` | the `player-offline` profile, staged by `tools/stage-release-plugins.py` from `--artifact-root` |
| `manifest.json` | format 1: every other file with its SHA-256 and size |

The tag must equal `SCO_VERSION` in `src/version.h`. A launcher refuses an update whose manifest version differs from its tag. The script unpacks the zip into a scratch folder and checks it against the manifest again. `--check-tree` also runs `tools/check_package_tree.py` on the folder; that needs the plugin sources in the checkout, including `external/sc-creative`. Nothing is uploaded, tagged or pushed. `python tools/test_package_release.py` tests the script on a synthetic tree with stand-in binaries.

### `manifest.json`

`tools/release-manifest.py write` lists every file in the release folder with its SHA-256 and size, plus the version, the tag and the commit. The launcher's self-update copies only files the manifest lists; see [Updates](launcher.md#updates). The release job unzips the finished zip and runs `tools/release-manifest.py check` on it, so a zip that does not match its manifest is never uploaded.

Bump `SCO_VERSION` in `src/version.h` before tagging: `write` fails when the tag (without the `v`) differs from it.

To try it on any folder:

```sh
python3 tools/release-manifest.py write <folder> --tag v<version>
python3 tools/release-manifest.py check <folder>
```

## How the code is organized

`src/` owns the Windows proxy, admission evidence, offline patches, the message, input and render shell, and launcher integration. The game-pack host composition owns signature resolution, the quit and DataCore hooks, app startup and plugin tick admission. The shell passes no built-in feature registrations; features live under `plugins/`. Some legacy engine helpers stay linked for existing product callers, and the unbuilt sources under `src/builtins/` are still parsed by the checks. Neither is a feature fallback. See [architecture](architecture.md), [host composition](host-composition.md) and [SDK workflows](sdk-workflows.md).

## Check on macOS or Linux

```bash
tools/check.sh
```

This takes a few seconds. It parses every `src/*.cpp`, `src/builtins/*.cpp`, `src/builtins/*/*.cpp`, `launcher/*.cpp` and `plugins/*/*.cpp` file with clang against mingw-w64's Windows headers. It also screens `src/` for MSVC error C2712 (`__try` in a function that owns an object needing unwinding, such as a `std::string`). It is not a build; only MSVC's build is. Known clang-only diagnostics are in `tools/check-baseline.txt`, and only new ones fail. You need clang and mingw-w64 (`brew install llvm mingw-w64` on macOS).

Two focused test-process checks run against the pinned host headers:

- `tools/test_app_stop.sh external/sco-core` covers an unverified legacy `void` shutdown, failure, exception and confirmed cleanup, plus an unsafe-guard negative control.
- `tools/test_menu_widgets_release.sh external/sco-core` builds the real menu harness with `NDEBUG` and verifies that a forced failure is detected.

When you run `menu_widgets_test` or `asop_host_composition_test` directly against a legacy host, exit 77 means the functional assertions passed but shutdown could not be verified. It is a skip, not a successful cleanup.

### Include and scan gates

```bash
python3 tools/sdk-headers.py
tools/no-scans.sh
```

`tools/sdk-headers.py` checks that every file under `src/builtins/` (subfolders included) and `plugins/` includes only headers the sco SDK ships (read from `external/sco-core/sdk/components.json`, or the older `sdk/package.py`; ImGui's public headers included), files in its own tree, or toolchain headers. Anything else fails with file and line. Exceptions are in `tools/sdk-headers-allow.txt`: one line per header with a reason. A line whose includes are gone also fails, so the list only shrinks. `plugins/` has no entries and must stay clean.

The legacy sources under `src/builtins/` are not pure SDK code. The list is mostly `src/build.h`, `teleport.h`, `quantum.h`, `travel.h`, `npc.h` and the menu shell headers, plus `sco/net/session.h` and `sco/runtime.h` where the SDK has no form yet. The gate stops new dependencies, not existing ones. A new include of a non-SDK header fails CI, and the fix is a `game.*` service in sco-core, not a new line in the list. See [architecture](architecture.md#what-the-sdk-headers-allow-list-means).

`tools/no-scans.sh` allows no raw pattern scan under `src/` (third-party code excluded). Any call of `FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch` or `memchr` fails it, and it has no allow-list. Exit code 2 means the scan itself could not run, which never counts as clean. Every game address comes from a sco-core signature row.

Other checks:

- `python tools/check_capability_names.py --core <Core checkout>` checks capability names against Core.
- `python tools/check_menu_wording.py` checks the player-facing menu text for internal words.

### Self-update tests under Wine

```bash
WINE_ROOT=<your Wine build> tools/update-test/run.sh
```

This builds the C++ launcher with `-DSCO_UPDATE_TEST` and runs 14 update scenarios under Wine against local release zips (about 20 s). See [`tools/update-test/README.md`](../tools/update-test/README.md). Not run in CI.

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) runs on pushes to `main`, on pull requests and on `v*` tags. Every job except `release` runs on each pull request and push to `main`. The `check`, `sdk-headers` and `build` jobs check out `external/sco-core` (`submodules: true`).

1. `check` runs `tools/check.sh` and the release manifest tests (`python3 tools/test_release_manifest.py`) on Linux, and compares `src/third_party/imgui` (`imconfig.h`, `imgui.h`, `LICENSE.txt`) with the copy the SDK ships.
2. `no-scans` runs `tools/no-scans.sh`.
3. `sdk-headers` runs `tools/sdk-headers.py`.
4. `build` (needs `check`) builds Release x64 with CMake and MSVC, then checks the binaries with `dumpbin` and `mt`. `dinput8.dll` exports `DirectInput8Create` forwarded to System32's `dinput8.dll`. Both files are x64 and import no dynamic C runtime. The launcher's manifest asks for `asInvoker`; the launcher asks for administrator rights itself when it needs them. The DLL contains neither bridge. It uploads `dinput8-release` and `test-plugins` (never part of a release).
5. The optional bridges are not built in CI. The retired host options `SCO_BRIDGE_TITANLINK` and `SCO_BRIDGE_VOXEL` fail configuration on purpose.
6. `npc-sdk-posix` / `npc-sdk` and `inspector-sdk-posix` / `inspector-sdk` download and verify the published SDK 2.4.0 archive (`sdk-v2.4.0`), compile outside the host checkout, check exact compiler dependencies, and run each module workflow and published checker. POSIX also runs private-header, private-symbol and transitive-header negative controls. These jobs do not initialize the host submodule. See [SDK workflows](sdk-workflows.md).
7. `rust-launcher` builds and tests every crate of `launcher-rs/` with clippy `-D warnings`, runs the window self test, builds the C++ launcher from the same source, and runs the comparison scripts that need no game. It checks that the Rust exe reports the version in `src/version.h`, checks the release exes for dynamic CRT imports, runs `cargo deny check`, and uploads `rust-launcher-release` (`sc-offline.exe` and `sc_offline_menu.dll`).
8. `external-sdk` builds, checks and stages discord, crew, spawn, build, mining, teleport and quantum on Windows against the same published archive. `creative-sdk` does the same for Creative from the `external/sc-creative` submodule, running that repository's `tools/check_creative_sdk.py`. `contracts-sdk` supplies the command-first contracts folder; its providers stay deferred.
9. `launcher-mcp` runs `tools/check_launcher_mcp.py`: protocol, schema, log-limit and redaction tests on Windows. The `build` job runs it again with `--exe` against the built launcher in a scratch install and checks that the server wrote nothing.
10. `development-sdk-proofs` runs the isolated consumer proofs against the Core the gitlink selects.
11. `release` runs on tags only and needs the jobs above. It runs `tools/package_release.py` as in the section above, with `--check-tree`. Its first step fails on purpose ("Refuse packaging an unpublished development Core with stable plugin artifacts"), so no tag builds a release until matching release SDK and plugin artifacts have been reviewed. `gear_presets`, multiplayer, bridges and test plugins are not staged.

Every action is pinned to a commit SHA. The workflow defaults to `contents: read`, and only `release` gets `contents: write`.

## Release policy

- `v1.x` tags publish as full releases. Every other tag publishes as a pre-release.
- A maintainer can promote a pre-release to Latest with `gh release edit <tag> --prerelease=false --latest`. That can happen before anyone has played the build. When it does, the release notes and the CHANGELOG say the build is untested.
- If a promoted build turns out broken, mark an earlier release as Latest again: `gh release edit <tag> --latest`.

This page does not name the current release, so it cannot go stale. See [Releases](https://github.com/scuba-sc/sc-offline/releases).

## Test fixtures

`tools/test_fleet_identity.py` is a project-authored GPL-3.0-only fixture. Its `Synthetic_A`, `Synthetic_B` and `Synthetic_C` names are artificial inputs, not a game catalogue or player data. It extracts the legacy fleet mapping, build and retrieve bodies from `src/hooks.cpp` and compiles them with synthetic registry and engine reads. It does not run the game. The release package does not include it.

`tools/test_loadout_content_host.ps1` (Windows x64) takes `-SdkArchive`, `-CoreSourceDir`, `-CMakeExe` and `-GeneratorInstance`. It verifies the pinned SDK archive and Core commit, builds the external Loadout module against the extracted SDK, and runs it with real Core content discovery in a fresh synthetic host. It creates a unique temporary folder and leaves it for inspection. It does not exercise a game, game providers, the launcher or the updater.
