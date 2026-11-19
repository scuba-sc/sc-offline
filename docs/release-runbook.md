# Release runbook: 0.8.0

This page is the ordered procedure for the person who releases sc-offline 0.8.0. Each step says what to run and what it proves. Nothing here has been run yet: the steps that depend on SDK 2.4.1 need that SDK to be published first.

**The package CI builds is a new build.** The release job rebuilds every binary from the merge commit and packages them. Its hashes differ from any package built on a development machine. No run in game carries over byte for byte, so the first in-game checks (step 10) are a real test of this package. [Status](status.md) says what has been run in game so far.

## 1. Preconditions

- **SDK 2.4.1 is published and verified.** There is a full release `sdk-v2.4.1` on `scuba-sc/sco-core` (not a draft, not a pre-release). Its `SHA256SUMS` check out against the downloaded assets, and `release.json` names the tag's commit.
- **Game pack 0.8.1** is the game pack of that SDK. The release's `sco-sdk-game-sc-0.8.1.zip` shows it.
- `src/version.h` says `0.8.0`. The tag in step 8 must equal it, or the package tool refuses.
- `git status` is clean on the branch you release from, and `CHANGELOG.md` has the 0.8.0 entry.

Until the SDK is published, the release job stops at its first step. That is deliberate ([why](build.md#ci)).

## 2. Move the Core pin to the tagged commit

```bash
git submodule update --init external/sco-core external/sc-creative
git -C external/sco-core fetch origin tag sdk-v2.4.1
git -C external/sco-core checkout sdk-v2.4.1
git -C external/sco-core rev-parse HEAD            # must equal: git -C external/sco-core rev-parse 'sdk-v2.4.1^{commit}'
cat external/sco-core/sdk/VERSION                  # must say 2.4.1
git add external/sco-core
```

This proves the host is built from exactly the Core that the SDK release was built from. The release job checks the same thing and refuses otherwise.

## 3. Move every SDK pin

```bash
mkdir sdk-assets
gh release download sdk-v2.4.1 -R scuba-sc/sco-core -D sdk-assets
(cd sdk-assets && sha256sum -c SHA256SUMS)
python tools/bump_sdk_pin.py --release-json sdk-assets/release.json --sha256sums sdk-assets/SHA256SUMS
python tools/bump_sdk_pin.py --check --release-json sdk-assets/release.json --sha256sums sdk-assets/SHA256SUMS
```

[`tools/bump_sdk_pin.py`](../tools/bump_sdk_pin.py) rewrites the SDK version, tag, game pack, archive hashes, source commit and publication run id in `.github/workflows/build.yml`, `tools/check_*_sdk.py`, `tools/contracts_sdk_check.py`, `tools/ci-stable-sdk-preflight.sh`, `tools/test_sdk_generators.py` and `docs/sdk-workflows.md`, and the `sdk-v` tag in CMake comments. It prints each change. The second command must report that every pin is at SDK 2.4.1.

Then do what it lists as follow-ups by hand. In `docs/sdk-candidate.md`, turn "Target for 0.8.0: SDK 2.4.1 (not yet published)" into a "Published SDK 2.4.1" section with the hashes it printed, and read each page it says still names another version.

## 4. Regenerate and check

```bash
python tools/capabilities.py --core external/sco-core
python tools/capabilities.py --core external/sco-core --check
python tools/check_capability_names.py --core external/sco-core
python tools/check_menu_wording.py
python tools/check_host_wiring.py
```

`capabilities.py` rewrites `launcher-rs/crates/scol-contract/src/capabilities.json` and `docs/capabilities.md` from the Core's status registry. If either file changes, the launcher binary changes with it. Commit them with the pin move.

## 5. Push and open a pull request

```bash
git switch -c release/0.8.0
git commit -am "Pin sco-core and the SDK to the published 2.4.1 release"
git push -u origin release/0.8.0
gh pr create --base main --title "sc-offline 0.8.0"
```

## 6. Jobs that must be green

| Job | What it proves |
| --- | --- |
| `check` | Host wiring, menu wording, capability names, the script tests, the package tests (`test_package_release.py`, `test_check_package_tree.py`) and `test_bump_sdk_pin.py`. |
| `no-scans`, `sdk-headers` | No memory scans in the product source, and only public SDK headers in the plugins. |
| `build` | Release x64 builds with MSVC. **The two-DLL split check:** `dinput8.dll` has no Rust menu hook and `dinput8-rust-menu.dll` has it, and both forward `DirectInput8Create`, are x64 and import no dynamic C runtime. |
| `rust-launcher` | The **Rust launcher job**: the capability table is current, clippy with warnings as errors, every crate's tests, status, dry-run play, install and uninstall compared with the C++ launcher, the golden contract files, the exe's version against `src/version.h`, no dynamic C runtime in the exes, `cargo deny`. It uploads `sc-offline.exe` and `sc_offline_menu.dll`. |
| `launcher-mcp`, `launcher-localization`, `install-aliases` | The read-only MCP server, the launcher's text catalogues, and game-folder discovery. |
| `development-sdk-proofs` | The plugin consumers build against the Core the gitlink selects. |
| **The plugin jobs:** `inspector-sdk`, `npc-sdk`, `loadout-sdk`, `contracts-sdk`, `external-sdk` (discord, crew, spawn, build, mining, teleport, quantum), `creative-sdk` | Each plugin builds, passes its tests and the SDK's plugin checker against the published SDK archive and its `SHA256SUMS`, and stages its folder. |
| `release` (tags only) | **The package job.** See step 8. |

None of these jobs runs the game. A green run proves builds, checks and synthetic tests, not behaviour in game.

## 7. Merge

Merging is the maintainer's decision. Merge when the jobs above are green and you have read the diff. Release from the merge commit on `main`.

## 8. Tag and let the release job run

```bash
git switch main && git pull
git tag -a v0.8.0 -m "sc-offline 0.8.0" <merge commit>
git push origin v0.8.0
```

The `release` job then:

1. refuses unless `external/sco-core` is the commit of the published `sdk-v2.4.1` release, the Core there says SDK 2.4.1, and every pin agrees (`tools/bump_sdk_pin.py --check`);
2. downloads the launcher, DLL and plugin artifacts the jobs above built;
3. runs `tools/package_release.py --check-tree`. That assembles the folder and zip, writes `manifest.json`, reads the zip back against it, and runs `tools/check_package_tree.py` (commands and lists resolve, no build-machine path in a binary, every relative Markdown link resolves inside the package, `docs/handoff/` is absent);
4. creates the GitHub release **as a pre-release**, with the zip and `dinput8.dll`.

## 9. Verify the published zip

Do this before anyone is told to download it.

```bash
gh release download v0.8.0 -R scuba-sc/sc-offline -p 'sc-offline-v0.8.0.zip' -D check
gh release view v0.8.0 -R scuba-sc/sc-offline --json assets --jq '.assets[] | [.name, .digest]'
python -c "import hashlib,sys; print(hashlib.sha256(open('check/sc-offline-v0.8.0.zip','rb').read()).hexdigest())"
unzip -q check/sc-offline-v0.8.0.zip -d check
python tools/release-manifest.py check check/sc-offline-v0.8.0
python tools/check_package_tree.py --tree check/sc-offline-v0.8.0
```

- The zip's hash equals GitHub's digest.
- **Manifest check:** every file matches `manifest.json`, and no file is unlisted. The manifest's `commit` is the merge commit.
- **Package tree check:** it must print `PASS`. This also runs the **binary path scan** (no drive letter and user folder, home folder, cargo registry or toolchain path in any `.exe` or `.dll`) and the link check.
- Open `docs/launcher.md` in the unpacked folder and follow one link into the repository: it should open the file at tag `v0.8.0`.

## 10. First checks in game

Do these in order, from the lowest risk. Use a game folder you can restore, and stop at the first failure. Each one is a run in game of this exact package. Record the result in [Status](status.md) afterwards.

1. **The default path: the new launcher and the classic menu.** Extract the zip, leave `menu = imgui`, run `sc-offline.exe` and press Play. Expect one administrator prompt, the firewall rules for `StarCitizen.exe`, the RSI Launcher and `CrashHandler.exe` (and **no** multiplayer rule, because `multiplayer = off`), the hosts-file line and the Easy Anti-Cheat rename. The game starts, the classic menu opens on **M**, and gear equip from the Gear page attaches items. Close the game: `sc-offline.exe status` must then show no PC change left over.
2. **The classic launcher.** Run `sc-offline-classic.exe play` the same way. This is the launcher the earlier runs used. Then run `sc-offline-classic.exe uninstall` and check `status` again.
3. **The new menu.** Set `menu = rust`, run `sc-offline.exe` and Play. The launcher installs `dinput8-rust-menu.dll` as `dinput8.dll`. Check that the menu opens on **M**, gives the keyboard and mouse back to the game, and resizes. Set `menu = imgui` again afterwards.

Also run once, before promoting: **Stop** from the window and `sc-offline.exe stop`; and, if the 0.7.x launcher you have supports `update_channel = prerelease`, an update from 0.7.x to 0.8.0 on a copy of an old install.

## 11. Mark the release latest

The update channel `stable` reads `/releases/latest`, and GitHub never returns a pre-release there. The launcher also refuses a pre-release on that channel. So the workflow's pre-release stays invisible to updating players until you promote it:

```bash
gh release edit v0.8.0 -R scuba-sc/sc-offline --prerelease=false --latest
```

Promote only after steps 9 and 10 passed. From that moment every 0.7.x player who has `check_updates = on` is offered 0.8.0. If you promote before the in-game checks, the release notes and the CHANGELOG must say the build is untested, as [Release policy](build.md#release-policy) describes.

## 12. Rollback

**A player returns to 0.7.x.**

1. Run `sc-offline.exe uninstall` (or `sc-offline-classic.exe uninstall`). It takes the mod out of the game folder and undoes the PC changes. `status` must show nothing left over.
2. Download the 0.7.x zip you want (for example 0.7.8.3) from the [releases page](https://github.com/scuba-sc/sc-offline/releases) and extract it over the folder. `sc-offline.ini` and `data\` (wallet, saved places) are kept. The 0.7.x launcher warns about settings it does not know and carries on. Plugin folders that only 0.8.0 had stay in `data\plugins\`; remove them if 0.7.x logs a problem with one. This path has not been run.
3. Turn `check_updates` off in `sc-offline.ini` if you do not want the launcher to offer 0.8.0 again.

**Pull the release.**

1. Make an earlier release the latest: `gh release edit v0.7.8.3 -R scuba-sc/sc-offline --latest`. Players who have not updated stop being offered 0.8.0.
2. Mark 0.8.0 as a pre-release again: `gh release edit v0.8.0 -R scuba-sc/sc-offline --prerelease`. Or delete it with `gh release delete v0.8.0 -R scuba-sc/sc-offline --cleanup-tag`, which also removes the tag and cannot be undone; delete only on the maintainer's decision.
3. Say so in the CHANGELOG and the release notes.

Players who already updated stay on 0.8.0: the updater only offers a newer version, so it cannot move them back. They follow the steps above by hand.
