# Building, checks, CI and releases

## Build on Windows

The build is CMake (`CMakeLists.txt` at the root), x64 only, with MSVC.

[sco-core](https://github.com/scuba-sc/sco-core) (the scanners, signature rows, host kit, plugin loader and sco-lua) is a git submodule in `external/sco-core`, built into `dinput8.dll`. Clone with it:

```
git clone --recurse-submodules https://github.com/scuba-sc/sc-offline.git
```

The Creative plugin is a second submodule, `external/sc-creative` (the [`sc-creative`](https://github.com/scuba-sc/sc-creative) repository). The plugin build (`plugins/CMakeLists.txt`), the package preflight and the release proof read it from there, so `--recurse-submodules` (or `git submodule update --init`) is needed for them too.

In an existing clone, or after a pull that moves the pin, run `git submodule update --init`. Without it, CMake configure and `tools/check.sh` stop and say so.

The host is pinned to the exact `external/sco-core` gitlink, an unpublished Core revision, so a fresh checkout needs that commit's objects and origin cannot fetch it until it is published. Rebuild host and Core together, and build the development SDK from the same source for development plugin proofs. The published SDK 2.4.0 archive is what the external plugin jobs use, not the host pin. See [SDK coordinates](sdk-candidate.md). Do not track a moving `main` or copy plugin SDK headers over host source.

1. Install **Visual Studio 2026** with the **Desktop development with C++** workload. It includes CMake.
2. In Visual Studio, use **File → Open → Folder** on the repository root. Visual Studio reads `CMakeLists.txt`; pick an x64 **Release** configuration and build. Or from a Developer PowerShell:

   ```powershell
   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
   cmake --build build --config Release --parallel
   ```

   On Visual Studio 2022, configure with `-G "Visual Studio 17 2022" -A x64 -T v143` instead.

3. The build produces `dinput8.dll` (the mod, from `src/`) and `sc-offline.exe` (the C++ launcher, from `launcher/`; the release ships it as `sc-offline-classic.exe`) in `build\bin\Release\` (`build\bin\Debug\` for Debug; directly in `bin\` of the build folder with a single-configuration generator such as Ninja, which Visual Studio's Open Folder uses by default). To play, put both in a folder together with `data/` and `launcher/sc-offline.ini`. Feature plugins build separately against the verified published SDK, using the [SDK workflow proofs](sdk-workflows.md). The host build does not produce their DLLs; a host-only local build needs matching external plugin folders staged separately.

The root targets are `dinput8`, `sc-offline`, `imgui`, the three test plugins (`spawn_probe`, `builtins_probe`, `ui_probe`) and the linked sco-core libraries. All use the static CRT; Release enables whole-program optimization. The host registers no built-in feature plugins. The player release stages the `player-offline` profile in [`tools/release-plugin-profiles.json`](../tools/release-plugin-profiles.json): 11 native modules, whose manifests and DLLs staging verifies; Discord keeps its disabled marker. `gear_presets` and `loadout` are development consumers; TitanLink and voxel_bridge are separately built optional bridges; `sc-mp` is a separate co-presence consumer. The [scuba-engine hostd profiles](https://github.com/scuba-sc/scuba-engine/blob/main/docs/engine/profiles.md) pick trusted plugins for synthetic, headless checks; they are not player packages and not evidence of game readiness. Staging a plugin does not turn on a missing or default-off provider.

No Visual Studio? CI builds both on every push to `main`. Download the `dinput8-release` artifact from [Actions](https://github.com/scuba-sc/sc-offline/actions).

## Build the Rust launcher and menu

From 0.8.0 the launcher the player runs is the Rust workspace in `launcher-rs/` (14 crates under `crates/`: `scol-contract`, `scol-config`, `scol-plugins`, `scol-install`, `scol-pcchanges`, `scol-redact`, `scol-mcp`, `scol-catalogue`, `scol-play`, `scol-update`, `scol-ui`, `scol-plugin-manager`, `scol-menu` and `scol-cli`). It builds the release `sc-offline.exe`, `sc_offline_menu.dll` (the opt-in Rust in-game menu) and a few helper programs. You need `rustup`; the toolchain is pinned in `launcher-rs/rust-toolchain.toml` (Rust 1.99.0, `x86_64-pc-windows-msvc`) and the MSVC linker from the Visual Studio workload above.

```powershell
cd launcher-rs
cargo fmt --all --check
cargo clippy --workspace --all-targets -- -D warnings
cargo build --release --workspace
cargo test --workspace
cargo deny check
```

Use your own `CARGO_TARGET_DIR` when several builds share a PC. Add `--locked` in CI so `Cargo.lock` must already be current. Outputs are in `target\release\`: `sc-offline.exe` (about 6 MB, no dynamic C runtime), `sc_offline_menu.dll`, `scol-pm.exe` and `scol.exe` (an alias of `sc-offline.exe` for the comparison scripts).

Checks that need no game (the `rust-launcher` CI job runs them; `docs/rust-launcher.md` has the full list and what each one shows):

```powershell
python tests/status/compare_status.py --cpp <C++ sc-offline.exe> --rust target/release/sc-offline.exe
python tests/play/compare_dryrun.py   --cpp <C++ sc-offline.exe> --rust target/release/sc-offline.exe
python tests/play/real_standin.py     --rust target/release/sc-offline.exe --standin target/release/scol-play-standin.exe
python tests/install/compare.py       --cpp <C++ sc-offline.exe> --rust target/release/examples/install_probe.exe   # after: cargo build --release -p scol-install --example install_probe
python tests/zip-corpus/generate.py --check
cargo run -p scol-ui --features fixtures -- --ui-self-test
python ../tools/check_launcher_mcp.py --exe target/release/sc-offline.exe
```

By hand only: the update contract against a C++ build with `-DSCO_UPDATE_TEST` (`SCOL_CPP_UPDATE_EXE=... cargo test -p scol-update --features test-hooks`), the creative plugin proof (`tests/plugins/creative-proof.py`) and the in-game menu host (`CORE=<Core checkout> bash tests/menu/run.sh`, needs a Core with `sco/widgets_view.h`). All of this is source, build and synthetic evidence. None of it ran in game.

### CMake options that change the menu

`SCO_HOST_MENU_CLIENT` (default `OFF`) builds the host menu client (`src/menu_widgets.cpp`) and the opt-in Rust menu hook (`src/menu_rust.cpp`). It needs a Core with `sco/host_menu.h`, a widgets 1.3 renderer and, for the Rust hook, `sco/widgets_view.h`. With it `OFF`, `dinput8.dll` has no Rust hook and the ini key `menu = rust` does nothing. Turning it `ON` also changes how the ImGui menu gets its panels, and that path has not been run in game, so the project's default build and CI leave it `OFF` until the owner has run it. A release that is meant to carry the opt-in Rust menu must configure with `-DSCO_HOST_MENU_CLIENT=ON`; say so in its release notes.

## Build a release package without CI

`tools/package_release.py` assembles the same folder and zip the `release` job publishes, from build outputs you give it. It needs Python 3 only.

```powershell
python tools/package_release.py --tag v0.8.0 --out dist `
  --rust-exe  launcher-rs\target\release\sc-offline.exe `
  --classic-exe build\bin\Release\sc-offline.exe `
  --menu-dll  launcher-rs\target\release\sc_offline_menu.dll `
  --dinput8   build\bin\Release\dinput8.dll `
  --artifact-root <folder with built-external\, built-inspector\, built-npc\, built-contracts\> `
  --check-tree
```

It writes `dist\sc-offline-v0.8.0\` and `dist\sc-offline-v0.8.0.zip`:

| In the package | From |
| --- | --- |
| `sc-offline.exe` | the Rust launcher (`--rust-exe`) |
| `sc-offline-classic.exe` | the C++ launcher (`--classic-exe`), the fallback |
| `sc_offline_menu.dll` | the Rust menu (`--menu-dll`), used only with `menu = rust` |
| `dinput8.dll` | the mod (`--dinput8`) |
| `sc-offline.ini`, `sc-offline.sh`, `README.md`, `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | the repository |
| `docs\`, `data\` | the tracked files (`git ls-files`; `data\buildables.txt` moves into the `build_catalog` pack) |
| `data\plugins\picker_catalog`, `build_catalog`, `locations_catalog` | the list packs copied from `data\ships.txt`, `npcs.txt`, `buildables.txt` and `locations.txt` |
| `data\plugins\<id>\` | the `player-offline` profile of `tools/release-plugin-profiles.json`, staged by `tools/stage-release-plugins.py` from `--artifact-root` |
| `manifest.json` | format 1: every other file with its SHA-256 and size |

The tag must equal `SCO_VERSION` in `src/version.h` (a launcher refuses an update whose manifest version differs from its tag). The script unpacks the zip into a scratch folder and checks it against the manifest again; `--check-tree` also runs `tools/check_package_tree.py` on the folder (it needs the plugin sources in the checkout, including the `external/sc-creative` submodule). Nothing is uploaded, tagged or pushed. `python tools/test_package_release.py` tests it on a synthetic tree with stand-in binaries.

## How the code is organized

`src/` owns the Windows proxy, admission evidence, offline patches, the message, input and render shell, and launcher integration. The game-pack host composition owns signature resolution, the quit and DataCore hooks, app startup and plugin tick admission. The shell passes no built-in feature registrations; the feature implementations live under `plugins/`. Some legacy engine helpers stay linked for existing product callers, and the unbuilt sources under `src/builtins/` are still parsed by the checks. Neither is a host feature fallback. See [host composition](host-composition.md) and [SDK workflows](sdk-workflows.md).

## Check on macOS or Linux

```bash
tools/check.sh
```

This runs in a few seconds. It parses every `src/*.cpp`, `src/builtins/*.cpp`, `src/builtins/*/*.cpp`, `launcher/*.cpp` and `plugins/*/*.cpp` file with clang against mingw-w64's Windows headers. It also screens `src/` for MSVC error C2712 (`__try` in a function that owns an object needing unwinding, such as a `std::string`). It is not a build: only MSVC's build is. Known clang-only diagnostics are listed in `tools/check-baseline.txt`, and only new ones fail the check. You need clang and mingw-w64 (`brew install llvm mingw-w64` on macOS).

The focused test-process shutdown regression runs with the pinned host headers via
`tools/test_app_stop.sh external/sco-core`. It covers an unverified legacy `void`
shutdown, failure, exception and confirmed cleanup, plus an unsafe-guard negative
control. `tools/test_menu_widgets_release.sh external/sco-core` builds the real
menu harness with `NDEBUG` and verifies that a forced failure is detected. When invoking `menu_widgets_test` or `asop_host_composition_test`
directly against a legacy host, exit 77 means the functional assertions passed but
shutdown could not be verified; it is a skip, not a successful cleanup.

### Include and scan gates

```bash
python3 tools/sdk-headers.py
tools/no-scans.sh
```

`tools/sdk-headers.py` checks that every file under `src/builtins/` (subfolders included) and `plugins/` includes only headers the sco SDK ships (read from `external/sco-core/sdk/components.json`, or the older `sdk/package.py`; ImGui's public headers included), files in its own tree, or toolchain headers. Anything else fails with file and line. The exceptions are in `tools/sdk-headers-allow.txt`: one line per header with a reason, and a line whose includes are gone fails too, so the list only shrinks. `plugins/` has no entries and must stay clean.

What the allow-list means: the legacy sources under `src/builtins/` are **not** pure SDK code. The list is mostly `src/build.h`, `teleport.h`, `quantum.h`, `travel.h`, `npc.h` and the menu shell headers, which those sources still call directly, plus `sco/net/session.h` and `sco/runtime.h` where the SDK has no form yet. The gate stops new dependencies, not existing ones: a new include of a non-SDK header fails CI, and the fix is a `game.*` service in sco-core, not a new line there. See [architecture.md](architecture.md#what-the-sdk-headers-allow-list-means).

`tools/no-scans.sh` allows no raw pattern scan under `src/` (third-party code excluded): any call of `FindPattern`, `FindUniquePattern`, `FindCString`, `FindRipLea`, `FunctionStart`, `BytesMatch` or `memchr` fails it, and it has no allow-list. Exit code 2 means the scan itself could not run, which never counts as clean. Every game address comes from a sco-core signature row.

### Self-update tests under Wine

```bash
WINE_ROOT=<your Wine build> tools/update-test/run.sh
```

Builds the launcher with `-DSCO_UPDATE_TEST` and runs 14 update scenarios (good update, tampered or unlisted files, downgrade, `..` paths, a failed or killed swap, a locked file, a new launcher that fails `--self-test`) against local release zips under Wine. About 20 s. See `tools/update-test/README.md`. Not run in CI.

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) runs on pushes to `main`, on pull requests, and on `v*` tags. Every job except `release` runs on each pull request and on each push to `main`, so a green `main` shows the gates hold. The `check`, `sdk-headers` and `build` jobs check out `external/sco-core` (`submodules: true`).

1. `check` runs `tools/check.sh` and the release manifest tests (`python3 tools/test_release_manifest.py`) on Linux, and compares `src/third_party/imgui` (`imconfig.h`, `imgui.h`, `LICENSE.txt`) with the copy the SDK ships.
2. `no-scans` runs `tools/no-scans.sh`: no raw game-memory pattern scan under `src/`. It is its own job so a failure there doesn't hide whether the sources compile.
3. `sdk-headers` runs `tools/sdk-headers.py` (above). Its own job for the same reason.
4. `build` (needs `check`) configures and builds Release x64 with CMake and MSVC on Windows, then checks the binaries with `dumpbin` and `mt`: `dinput8.dll` exports `DirectInput8Create` forwarded to System32's `dinput8.dll`, both files are x64, neither imports the dynamic C runtime, `sc-offline.exe`'s manifest asks for `asInvoker` (the launcher asks for administrator rights itself when it needs them), and the DLL contains neither bridge. It uploads `dinput8-release` (the DLL and launcher) and `test-plugins` (never part of a release).
5. The optional bridges are not built in CI and are excluded from the host and release; the retired host bridge options (`SCO_BRIDGE_TITANLINK`, `SCO_BRIDGE_VOXEL`) fail configuration on purpose.
6. `npc-sdk-posix` / `npc-sdk` and `inspector-sdk-posix` / `inspector-sdk` download and verify the published stable `sco-sdk-2.4.0.zip` (`sdk-v2.4.0`), compile outside the host checkout, check exact compiler dependencies and run each named module workflow and published checker. POSIX also exercises private-header/symbol and transitive-header negative controls; Windows uploads enabled `npc-plugin` and `inspector-plugin` folders. These isolated jobs do not initialize the host submodule. See the [worked tutorial](sdk-workflows.md).
6a. `rust-launcher` builds and tests every crate of `launcher-rs/` with clippy `-D warnings`, runs the window self test, builds the C++ launcher from the same source, and runs the comparison scripts that need no game (status, dry-run play, install and uninstall, the stand-in game with the real helper, the plugin zip corpus, the launcher contract files, the MCP stdio session against the Rust exe), checks that the Rust exe reports the version of `src/version.h`, checks the release exes for dynamic CRT imports, runs `cargo deny check`, and uploads `rust-launcher-release` (`sc-offline.exe` and `sc_offline_menu.dll`) for the release job.
7. `external-sdk` builds/checks/stages discord, crew, spawn, build, mining, teleport and quantum independently on Windows using the same published stable archive; `creative-sdk` does the same for creative from the `external/sc-creative` submodule, running that repository's own `tools/check_creative_sdk.py` before the folder is staged. `contracts-sdk` supplies the command-first contracts folder; its providers remain deferred.
8. `launcher-mcp` runs `tools/check_launcher_mcp.py`: the portable protocol, schema, log-limit and redaction tests on Windows. The `build` job then runs it again with `--exe` against the built launcher, in a scratch install, and checks that the server wrote nothing.
9. `development-sdk-proofs` runs the isolated consumer proofs against the Core the gitlink selects.
10. `release` runs on tags only and needs the jobs above (`rust-launcher` included). It downloads the C++ launcher and `dinput8.dll` from `build`, the Rust files from `rust-launcher`, and the plugin folders, then runs `tools/package_release.py` as in the section above, with `--check-tree`. Its first step currently fails on purpose ("Refuse packaging an unpublished development Core with stable plugin artifacts"), so no tag builds a release until matching release SDK and plugin artifacts have been reviewed. After that step it assembles the launcher, host DLL, configuration, tracked data and docs, the 11 external feature folders, notices and `manifest.json`. Discord keeps its disabled marker; the other ten folders ship enabled. Loadout, multiplayer, bridges and test plugins are not staged.

### Stable-ID test fixture provenance and packaging

`tools/test_fleet_identity.py` is a project-authored GPL-3.0-only test fixture, not
third-party code. Its `Synthetic_A`, `Synthetic_B`, and `Synthetic_C` names and
numeric class stand-ins are artificial test inputs, not a game catalogue or captured
player data. It extracts the legacy fleet mapping/build and retrieve bodies from the
local `src/hooks.cpp` and compiles them with synthetic registry/engine reads and
platform stand-ins. Its temporary ships file is generated by the test. It is not a
game execution, production registry evidence, or gameplay acceptance.

The tag release (`tools/package_release.py`) stages tracked `docs/` and `data/`, selected root files and built
artifacts; it does not stage this `tools/` fixture or its generated files. The
release manifest describes the staged payload, not all source/test inputs. The
manifest regression test checks the tracked docs/data staging and a local ZIP
round trip; that is not a complete CI release build or release approval.

### `manifest.json`

`tools/release-manifest.py write` lists every file in the release folder with its SHA-256 and size, plus the version, the tag and the commit. The launcher's self-update uses it to decide which files it may copy; see [Updates](launcher.md#updates). The release job then unzips the finished zip and runs `tools/release-manifest.py check` on it, so a zip that doesn't match its own manifest is never uploaded.

**Bump `SCO_VERSION` in `src/version.h` before tagging.** The manifest's version comes from the tag, and `write` fails the release when the tag (without the `v`) differs from `SCO_VERSION`. A launcher refuses an update whose manifest version differs from the release tag, so a mismatch would break every update to that release.

To try it locally on any folder: `python3 tools/release-manifest.py write <folder> --tag v<version>`, then `python3 tools/release-manifest.py check <folder>`.

Every action is pinned to a commit SHA. The workflow defaults to `contents: read`, and only the `release` job gets `contents: write`.

## Release policy

- `v1.x` tags publish as full releases. Every other tag publishes as a **pre-release**.
- A maintainer can promote a pre-release to Latest with `gh release edit <tag> --prerelease=false --latest`. **That can happen before anyone has played the build.** When it does, the release notes and the CHANGELOG say the build is untested.
- If a promoted build turns out broken, re-mark an earlier release as Latest (`gh release edit <tag> --latest`).

To see the current state, check [Releases](https://github.com/scuba-sc/sc-offline/releases). This file deliberately doesn't name the current release, so that it can't go stale.

## Synthetic Loadout content host proof

On Windows x64, `tools/test_loadout_content_host.ps1` accepts `-SdkArchive`,
`-CoreSourceDir`, `-CMakeExe`, and `-GeneratorInstance`. It verifies the pinned
SDK archive and Core source commit, builds the external Loadout module against
the extracted public SDK, and runs it with real Core content discovery in a
fresh synthetic host. It creates a unique temporary build and fixture directory
and leaves it for inspection. The fixture uses synthetic lists and does not
exercise a game, game providers, the launcher, or updater.
