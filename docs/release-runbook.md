# Release runbook: 0.8.0

This page is the ordered procedure for the person who releases sc-offline 0.8.0. Each step says what to run and what it proves. Nothing here has been run yet: the steps that depend on SDK 2.4.1 need that SDK to be published first.

**The package CI builds is a new build.** The release job rebuilds every binary from the merge commit and packages them. Its hashes differ from any package built on a development machine. No run in game carries over byte for byte, so the first in-game checks (step 10) are a real test of this package. [Status](status.md) says what has been run in game so far.

## Release candidate

Before the final release the maintainer wants testers to try a **release candidate**: SDK tag `sdk-v2.4.1-rc.1` on `scuba-sc/sco-core` and product tag `v0.8.0-rc.1` on `scuba-sc/sc-offline`, both published as GitHub **pre-releases**. The final `sdk-v2.4.1` and `v0.8.0` come later, with steps 1 to 12 below, after testers have reported. Nothing in a candidate has been run in game.

The candidate is tagged on the pushed branch, not on a merge commit on `main`. The branch (`feature/rust-launcher`) shares no history with `main` in a shallow clone, so merging first is not an option. `build.yml` runs on pushes to `main`, on tags `v*`, on pull requests and on manual dispatch, so a push of the branch alone starts nothing.

What the candidate changes in the rules:

- The version is `0.8.0-rc.1` (`src/version.h`, `launcher-rs/Cargo.toml` and `Cargo.lock`, the version resource, the manifest). The tag must be `v` plus `SCO_VERSION`, or the package tool and the `release` job refuse.
- Both launchers order versions as `0.7.8` < `0.8.0-rc.1` < `0.8.0-rc.2` < `0.8.0`. A 0.7.x launcher and a launcher at `0.8.0-rc.1` are not offered a candidate on the `stable` channel.
- The `release` job accepts a pre-release product tag (it contains `-rc.<n>`) on the Core commit of a published SDK **pre-release** `sdk-v<version>-rc.<n>` (not a draft). A final product tag still needs a final SDK tag that is a full release. `tools/check_release_gate.py` decides this and `tools/test_release_gate.py` tests it.
- The archives of `sdk-v2.4.1-rc.1` are named for the plain version (`sco-sdk-2.4.1.zip`), as those of `sdk-v2.4.0-rc.2` are. Only the tag has `-rc.1`.
- The release is created as a pre-release, never as Latest, with its own text for testers.

**Who does each step.** *Agent can do* means local work in a worktree, or a read-only command (`gh release view`, `git ls-remote`, a download). *Maintainer only* means a write to GitHub or a release decision: the repository rules leave tagging and publication to the maintainer ([release policy](build.md#release-policy)). Confirm the destination before each of those.

| # | Step | Who |
| --- | --- | --- |
| C1 | The SDK candidate is published | **Done 2026-11-22** (`sdk-v2.4.1-rc.1`) |
| C2 | Move the Core pin | **Done** (gitlink `ef4061b`, uncommitted) |
| C3 | Move the SDK pins | **Done** (115 pins, `--check` agrees, uncommitted) |
| C4 | Check and commit | agent can do |
| C5 | Push the branch | maintainer only |
| C6 | Run the full CI on the branch | maintainer only (dispatch), agent can do (read the result) |
| C7 | Tag | maintainer only |
| C8 | What the `release` job does | automatic, from the tag |
| C9 | Verify the pre-release | agent can do |
| C10 | How testers get it | maintainer only (to announce) |
| C11 | Withdraw a bad candidate | maintainer only |

**C1. The SDK candidate is published. Done on 2026-11-22.** `sdk-v2.4.1-rc.1` is a GitHub pre-release on `scuba-sc/sco-core`, tag commit `ef4061b123a1f0b4f449e6c0257078d7348520e3`. The text below is kept for the re-check and for a later `-rc.2`. In `sco-core`, `release-candidate.yml` takes the prospective tag `sdk-v2.4.1-rc.1` (only from `main`), then `release.yml` publishes it (tag, run id and artifact id; it needs the `release` environment approval and skips its notify job for an `-rc` tag). Check it before anything below (agent can do):

```bash
gh release view sdk-v2.4.1-rc.1 -R scuba-sc/sco-core --json tagName,isDraft,isPrerelease --jq '[.tagName, .isDraft, .isPrerelease]'
git ls-remote https://github.com/scuba-sc/sco-core 'refs/tags/sdk-v2.4.1-rc.1*'
```

It must say `false` for the draft and `true` for the pre-release. The release gate refuses a candidate that gh reports as a full release. The earlier candidates `sdk-v2.4.0-rc.1` and `-rc.2` were published as full releases, so this is not automatic: mark it with `gh release edit sdk-v2.4.1-rc.1 -R scuba-sc/sco-core --prerelease` (maintainer only). The commit it names must hold `sdk/VERSION` `2.4.1` and an `sdk/DEVELOPMENT_VERSION` with `-dev.` in it: the `check` job builds a development SDK from the commit the pin selects and stops without that file.

**C2. Move the Core pin (agent can do). Done:** the gitlink is `ef4061b123a1f0b4f449e6c0257078d7348520e3` in the index, not yet committed. `external/sco-core` is a submodule that this worktree does not check out. `git update-index --cacheinfo` writes the gitlink straight into the index, which is what `git add` would do for a checked-out submodule, so it is the right command here. It touches only the index, and `git ls-files -s external/sco-core` shows the change.

```bash
CORE=$(git ls-remote https://github.com/scuba-sc/sco-core 'refs/tags/sdk-v2.4.1-rc.1^{}' 'refs/tags/sdk-v2.4.1-rc.1' | tail -1 | cut -f1)
echo "$CORE"                                            # 40 hex: the commit of the tag, peeled when the tag is annotated
git ls-files -s external/sco-core                       # 160000 <old commit> 0 external/sco-core
git update-index --cacheinfo 160000,"$CORE",external/sco-core
git ls-files -s external/sco-core                       # 160000 <the tag's commit> 0 external/sco-core
```

**C3. Move the SDK pins (agent can do). Done:** `tools/bump_sdk_pin.py` moved 115 pins to SDK 2.4.1, tag `sdk-v2.4.1-rc.1`, and `--check` agrees; not yet committed. The follow-up pages are updated, and `docs/sdk-candidate.md` has a "Published SDK 2.4.1 release candidate 1" section. With the published candidate's own `release.json` and `SHA256SUMS`:

```bash
mkdir sdk-assets
gh release download sdk-v2.4.1-rc.1 -R scuba-sc/sco-core -D sdk-assets
(cd sdk-assets && sha256sum -c SHA256SUMS)
python tools/bump_sdk_pin.py --release-json sdk-assets/release.json --sha256sums sdk-assets/SHA256SUMS
python tools/bump_sdk_pin.py --check --release-json sdk-assets/release.json --sha256sums sdk-assets/SHA256SUMS
python tools/bump_sdk_pin.py --print-tag               # sdk-v2.4.1-rc.1
```

The tool takes `tag` from `release.json` (`sdk-v2.4.1-rc.1`), reads the version from it (2.4.1), keeps the archive names plain and writes the tag with its suffix wherever a tag is pinned. It also turns the stable-host job's assertion into "this release is a pre-release". Read what it prints as follow-ups: `docs/sdk-candidate.md` should name the candidate as a candidate. Then regenerate and check as in step 4 (`tools/capabilities.py`, `check_capability_names.py`, `check_menu_wording.py`, `check_host_wiring.py`), and run `python tools/test_bump_sdk_pin.py`, `python tools/test_release_gate.py` and `python tools/test_version_agreement.py`.

**C4. Check and commit (agent can do).** `git status` must show only the intended changes. Commit on the branch, in this worktree: `git commit -am "Pin sco-core and the SDK to the sdk-v2.4.1-rc.1 candidate"`. Put the pinned Core commit in the message.

**C5. Push the branch (maintainer only).** Check that a remote points at `github.com/scuba-sc/sc-offline` with `git remote -v`, then:

```bash
git push -u <that remote> feature/rust-launcher
```

The order is deliberate: a push before the pins move would only put an unpinned commit on GitHub. The push starts no run.

**C6. Run the full CI on the branch before tagging (maintainer only).**

```bash
gh workflow run build.yml --ref feature/rust-launcher -R scuba-sc/sc-offline
gh run list --workflow build.yml --branch feature/rust-launcher --limit 1 -R scuba-sc/sc-offline
gh run watch <run id> --exit-status -R scuba-sc/sc-offline
```

Every job in step 6 must be green (the `stable-host-*` jobs and `release` do not run on a dispatch). `release` is the one job that needs a tag, so a green dispatch run does not prove the gate. The gate is tested by `tools/test_release_gate.py` in `check`.

**C7. Tag (maintainer only).** Tag the commit that ran green, and push the tag:

```bash
git tag -a v0.8.0-rc.1 -m "sc-offline 0.8.0-rc.1 (release candidate)" <commit that ran green>
git push <that remote> v0.8.0-rc.1
```

**C8. What the `release` job does.** It runs on the tag after every other job passed again. It (1) checks that the tag, `SCO_VERSION` and the Rust version agree (`v0.8.0-rc.1`, `0.8.0-rc.1`); (2) runs `tools/check_release_gate.py`: a candidate product tag, the pinned `sdk-v2.4.1-rc.1`, the pin check, the gitlink equal to the tag's commit, `sdk/VERSION` `2.4.1`, and the SDK release a pre-release and not a draft; (3) downloads the artifacts the other jobs built; (4) runs `tools/package_release.py --check-tree`, which writes `manifest.json` and checks the zip and the tree; (5) creates the GitHub release `v0.8.0-rc.1` as a pre-release, not Latest, with `sc-offline-v0.8.0-rc.1.zip`, `dinput8.dll` and the text for testers (what to test first, how to report, how to go back, and that the stable update channel does not offer it). Publishing a release also runs `discord.yml`, which posts it to the project's Discord channel as a pre-release (and a run of `build` posts that it started).

**C9. Verify the pre-release (agent can do).** Before anyone is told to download it:

```bash
gh release view v0.8.0-rc.1 -R scuba-sc/sc-offline --json tagName,isPrerelease,isDraft --jq '[.tagName, .isPrerelease, .isDraft]'
gh release view v0.8.0-rc.1 -R scuba-sc/sc-offline --json assets --jq '.assets[] | [.name, .digest]'
gh release download v0.8.0-rc.1 -R scuba-sc/sc-offline -p 'sc-offline-v0.8.0-rc.1.zip' -D check
python -c "import hashlib; print(hashlib.sha256(open('check/sc-offline-v0.8.0-rc.1.zip','rb').read()).hexdigest())"
unzip -q check/sc-offline-v0.8.0-rc.1.zip -d check
python tools/release-manifest.py check check/sc-offline-v0.8.0-rc.1
python tools/check_package_tree.py --tree check/sc-offline-v0.8.0-rc.1
gh api repos/scuba-sc/sc-offline/releases/latest --jq .tag_name
```

It must be a pre-release and not a draft, the zip's hash must equal the digest, the manifest and tree checks must pass, and the last command must still print `v0.7.7`: the candidate must not be Latest. Do not run `gh release edit --latest` on a candidate.

**C10. How testers get it (maintainer only, to announce).** They download `sc-offline-v0.8.0-rc.1.zip` from the pre-release page `https://github.com/scuba-sc/sc-offline/releases/tag/v0.8.0-rc.1` and extract it into its own folder. A tester on 0.7.x can also update in place: the 0.7.7 launcher has no `update --prerelease` option (its source has only the `update_channel` ini key), so they set `update_channel = prerelease` in `sc-offline.ini` and run `sc-offline.exe update`. From `0.8.0-rc.1` on, `sc-offline.exe update --prerelease` works. Both read the same release list and check the zip against GitHub's digest and `manifest.json`. The in-place path from 0.7.7 has not been run against a real install.

**C11. Withdraw a bad candidate (maintainer only).**

```bash
gh release delete v0.8.0-rc.1 -R scuba-sc/sc-offline --yes
git push <that remote> --delete v0.8.0-rc.1
```

Deleting the release and the tag cannot be undone (`gh release delete --cleanup-tag` removes the tag in the same command). To hide it without deleting, `gh release edit v0.8.0-rc.1 -R scuba-sc/sc-offline --draft`. Publish a fix as `v0.8.0-rc.2`, never as another `v0.8.0-rc.1`: a launcher that already installed `0.8.0-rc.1` is only offered a newer version. A tester who installed the candidate runs `sc-offline-classic.exe uninstall`, checks `sc-offline.exe status`, extracts the 0.7.7 zip into a folder, and sets `update_channel = stable` again in `sc-offline.ini`. A tester who updated in place stays on the candidate until a newer one is published or they go back by hand as above. If the SDK candidate is the problem, withdraw it in `sco-core` the same way and publish `sdk-v2.4.1-rc.2`, then move the pins again with C2 and C3.

**After the testers.** Set `SCO_VERSION` back to `0.8.0` and remove `SCO_VERSION_PRERELEASE` in `src/version.h`, set the Rust workspace version, `Cargo.lock` and `scol-install`'s `SCO_VERSION` to `0.8.0`, and change the pins in the tests (`grep -rn "0.8.0-rc.1"`). Remove the release-candidate notes from `README.md`, `docs/status.md` and `CHANGELOG.md`. Publish `sdk-v2.4.1` as a full release, run `tools/bump_sdk_pin.py` with its `release.json` (it moves the tag, hashes, commit and run id from the candidate to the final), and follow steps 1 to 12.

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

Then do what it lists as follow-ups by hand. In `docs/sdk-candidate.md`, turn "Published SDK 2.4.1 release candidate 1" into a "Published SDK 2.4.1" section with the hashes it printed (keep the candidate as history), and read each page it says still names another version.

## 4. Regenerate and check

```bash
python tools/capabilities.py --core external/sco-core
python tools/capabilities.py --core external/sco-core --check
python tools/game_builds.py --core external/sco-core
python tools/game_builds.py --core external/sco-core --check
python tools/check_capability_names.py --core external/sco-core
python tools/check_menu_wording.py
python tools/check_host_wiring.py
```

`game_builds.py` rewrites `launcher-rs/crates/scol-contract/src/game_builds.json` from Core's `sc_game_pack.h` and build register; the launcher window uses it to say whether the installed game build is the one the release was checked on. `capabilities.py` rewrites `launcher-rs/crates/scol-contract/src/capabilities.json` and `docs/capabilities.md` from the Core's status registry. If either file changes, the launcher binary changes with it. Commit them with the pin move.

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
