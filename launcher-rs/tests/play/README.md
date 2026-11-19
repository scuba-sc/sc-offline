<!-- SPDX-License-Identifier: GPL-3.0-only -->
# play, install and uninstall parity

Two scripts, both under `D:\t` only, neither starts the real game.

`compare_dryrun.py` runs the built C++ `sc-offline.exe` and the Rust `sc-offline.exe` with `--dry-run` on
the same synthetic worlds and requires the same exit code, text, file tree and `data\launcher.log`.
It refuses a command line without `--dry-run`. Covered: defaults, Easy Anti-Cheat active (handled, not
handled, skipped), every PC-change switch off, ini settings in the printed environment text, the
`exp_*` capability switches and an inherited `SCO_*` variable that is removed, `multiplayer_allow`, a
foreign `dinput8.dll` and a `default_1.xml`, leftovers of a crash (marker, mod DLL, `pc-changes.txt`,
hosts line), a missing `dinput8.dll`, a missing game, `--game` spellings, ini warnings, no ini, and
`install` / `uninstall` dry runs.

```
cmake --build <build dir> --config Release --target sc-offline
cd launcher-rs && cargo build -p scol-cli
python tests/play/compare_dryrun.py --cpp <build dir>/bin/Release/sc-offline.exe [--rust <sc-offline.exe>] [--only <substring>]
```

`real_standin.py` runs the real Rust `sc-offline.exe play` (real helper process, real named events, real
process start) against a synthetic world with `scol-play-standin.exe` as `StarCitizen.exe`: it sleeps and
exits. The three PC-change switches are off in the scratch ini, so nothing on this PC changes. It checks
that the mod is in Bin64 while the stand-in runs and gone after, twice in a row (the log of the first run
is kept as `launcher.log.1`).

```
python tests/play/real_standin.py [--rust <sc-offline.exe>] [--standin <scol-play-standin.exe>]
```

`stop_standin.py` runs the real Rust `sc-offline.exe stop` against a stand-in game that behaves like the game with
the mod loaded (`SCOL_STANDIN_STOPPABLE=1`: the two named events of the stop channel, one small window, an exit-work
marker file). Scenarios: nothing running; the mod answers and the window is closed after it; the mod does not
answer for ten seconds and the window is closed anyway; the game ignores WM_CLOSE and `stop --end` ends exactly
that process after the minute (about 70 s, `--skip-end` leaves it out); a stand-in under another folder is left
alone. The script checks the `[stop hh:mm:ss.mmm]` lines in `data\launcher.log`. It closes and ends only stand-ins
it started itself.

```
python tests/play/stop_standin.py [--rust <sc-offline.exe>] [--standin <scol-play-standin.exe>] [--skip-end]
```

The in-process tests with the fake host are `crates/scol-play/tests/flow.rs` (the whole flow),
`crates/scol-play/tests/picker_and_update.rs` (the game folder picker and the update check before `play`) and
`crates/scol-cli/tests/{cli,stop_and_plugins}.rs`. The folder dialog itself (`IFileOpenDialog`) is never opened by a
test: the host trait scripts the answer, and the real dialog is only checked for its v-table layout.

Source, build and synthetic evidence only. Not run in game.
