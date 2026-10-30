# Building, checks, CI and releases

## Build on Windows

The build is CMake (`CMakeLists.txt` at the root), x64 only, with MSVC.

[sco-core](https://github.com/scuba-sc/sco-core) (the scanners, signature rows, host kit, plugin loader and sco-lua) is a git submodule in `external/sco-core`, built into `dinput8.dll`. Clone with it:

```
git clone --recurse-submodules https://github.com/scuba-sc/sc-offline.git
```

In an existing clone, or after a pull that moves the pin, run `git submodule update --init`. Without it, CMake configure and `tools/check.sh` stop and say so.

The embedded host is pinned to the published `sdk-v2.4.0-rc.2` release, exact source `844e136ccb280ad8cb43f73bc43c9447e8f6799d`: SDK 2.4.0, plugin ABI 1.1, game pack 0.8.0. External plugin jobs consume the checksum-pinned published RC2 archive separately. See [candidate coordinates and limits](sdk-candidate.md). Do not replace the submodule with moving `main` or copy plugin SDK headers over host source.

1. Install **Visual Studio 2026** with the **Desktop development with C++** workload. It includes CMake.
2. In Visual Studio, use **File → Open → Folder** on the repository root. Visual Studio reads `CMakeLists.txt`; pick an x64 **Release** configuration and build. Or from a Developer PowerShell:

   ```powershell
   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
   cmake --build build --config Release --parallel
   ```

   On Visual Studio 2022, configure with `-G "Visual Studio 17 2022" -A x64 -T v143` instead.

3. The build produces `dinput8.dll` (the mod, from `src/`) and `sc-offline.exe` (the launcher, from `launcher/`) in `build\bin\Release\` (`build\bin\Debug\` for Debug; directly in `bin\` of the build folder with a single-configuration generator such as Ninja, which Visual Studio's Open Folder uses by default). To play, put both in a folder together with `data/` and `launcher/sc-offline.ini`. Feature plugins build separately against the verified published SDK, using the [SDK workflow proofs](sdk-workflows.md). The host build does not produce their DLLs; a host-only local build needs matching external plugin folders staged separately.

The root targets are `dinput8`, `sc-offline`, `imgui`, the three test plugins (`spawn_probe`, `builtins_probe`, `ui_probe`) and the linked sco-core libraries. All use the static CRT; Release enables whole-program optimization. The host registers no built-in feature plugins. The player release stages the explicit `player-offline` selection in [`tools/release-plugin-profiles.json`](../tools/release-plugin-profiles.json); the profile lists its 11 native modules, verifies their manifests and DLLs, and preserves Creative and Discord as disabled. `gear_presets` and `loadout` remain development consumers; TitanLink and voxel_bridge are separately built optional bridges; `sc-mp` is maintained as a separate co-presence consumer. The [scuba-engine hostd profiles](https://github.com/scuba-sc/scuba-engine/blob/main/docs/engine/profiles.md) select trusted plugins for synthetic services and headless checks; they are not sc-offline player packages or game-readiness evidence. Staging a plugin does not turn on a missing or default-off provider.

No Visual Studio? CI builds both on every push to `main`. Download the `dinput8-release` artifact from [Actions](https://github.com/scuba-sc/sc-offline/actions).

## How the code is organized

`src/` owns the Windows proxy, admission evidence, offline patches, message/input/render shell and launcher integration. The game-pack host composition owns signature resolution, required quit/DataCore hooks, app startup and plugin tick admission. The shell passes no built-in feature registrations; the external feature implementations live under `plugins/`. Some legacy engine helpers remain linked for existing product callers, and unbuilt sources under `src/builtins/` are still parsed by the checks. They do not provide a host feature fallback. See [host composition](host-composition.md) and [SDK workflows](sdk-workflows.md).

## Check on macOS or Linux

```bash
tools/check.sh
```

This runs in a few seconds. It parses every `src/*.cpp`, `src/builtins/*.cpp`, `src/builtins/*/*.cpp` (the bridges) and `launcher/*.cpp` file with clang against mingw-w64's Windows headers. It also screens `src/` for MSVC error C2712 (`__try` in a function that owns an object needing unwinding, such as a `std::string`). It is not a build: only MSVC's build is. Known clang-only diagnostics are listed in `tools/check-baseline.txt`, and only new ones fail the check. You need clang and mingw-w64 (`brew install llvm mingw-w64` on macOS).

### Include and scan gates

```bash
python3 tools/sdk-headers.py
tools/no-scans.sh
```

`tools/sdk-headers.py` checks that every file under `src/builtins/` (the bridges' subfolders included) and `plugins/` includes only headers the sco SDK zip ships (read from `external/sco-core/sdk/package.py`, ImGui's public headers included), files in its own tree, or toolchain headers. Anything else fails with file and line. The exceptions are in `tools/sdk-headers-allow.txt`: one line per header with a reason, and a line whose includes are gone fails too, so the list only shrinks. `plugins/` has no entries and must stay clean.

What the allow-list means: the built-ins are **not** pure SDK plugins. Most of what it lists is `src/spawner.cpp`, `build.cpp`, `teleport.cpp`, `loadout.cpp`, `quantum.cpp`, `contracts.cpp`, `npc.cpp`, `cvars.cpp` and the menu shell, which the built-ins still call directly, plus a few kernel headers (`sco/hook.h`, `sco/net/session.h` and the `game.mining` rows) where the SDK has no form yet. The gate stops new dependencies, not existing ones: a new built-in include of a non-SDK header fails CI, and the fix is a `game.*` service in sco-core, not a new line there. See [architecture.md](architecture.md#what-the-sdk-headers-allow-list-means).

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
5. Optional bridges configure independently and are excluded from the host and standard release; the retired host bridge options deliberately fail configuration.
6. `npc-sdk-posix` / `npc-sdk` and `inspector-sdk-posix` / `inspector-sdk` download and verify published `sco-sdk-2.4.0.zip` (SDK 2.4.0 release candidate 2, tag `sdk-v2.4.0-rc.2`), compile outside the host checkout, check exact compiler dependencies and run each named module workflow and published checker. POSIX also exercises private-header/symbol and transitive-header negative controls; Windows uploads enabled `npc-plugin` and `inspector-plugin` folders. These isolated jobs do not initialize the host submodule. See the [worked tutorial](sdk-workflows.md).
7. `external-sdk` builds/checks/stages creative, discord, crew, spawn, build, mining, teleport and quantum independently on Windows using the same published RC2 archive. `contracts-sdk` supplies the command-first contracts folder; its providers remain deferred.
8. `release` needs `build`, `npc-sdk`, `inspector-sdk`, `contracts-sdk` and every `external-sdk` matrix job. It runs on tags only and assembles the launcher, host DLL, configuration, tracked data/docs, the 11 matching external feature folders, notices and `manifest.json`. Creative/Discord retain their disabled markers; the other nine folders ship enabled. Loadout, multiplayer, bridges and test plugins are not staged. No release is created by opening a candidate-update PR.

### Stable-ID test fixture provenance and packaging

`tools/test_fleet_identity.py` is a project-authored GPL-3.0-only test fixture, not
third-party code. Its `Synthetic_A`, `Synthetic_B`, and `Synthetic_C` names and
numeric class stand-ins are artificial test inputs, not a game catalogue or captured
player data. It extracts the legacy fleet mapping/build and retrieve bodies from the
local `src/hooks.cpp` and compiles them with synthetic registry/engine reads and
platform stand-ins. Its temporary ships file is generated by the test. It is not a
game execution, production registry evidence, or gameplay acceptance.

The tag release stages tracked `docs/` and `data/`, selected root files and built
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
