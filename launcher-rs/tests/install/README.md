<!-- SPDX-License-Identifier: GPL-3.0-only -->
# install and uninstall parity

`compare.py` runs the built C++ `sc-offline.exe` and the Rust library (through `install_probe`, an
example program of `scol-install`) on identical synthetic worlds and compares, after every command:
exit code, text, the whole file tree (content hashes) and `data\launcher.log`.

```
cmake -S <sc-offline tree> -B <build dir> -DSCO_HOST_SOURCE_DIR=<sco-core tree>
cmake --build <build dir> --config Release --target sc-offline
cd launcher-rs && cargo build -p scol-install --example install_probe
python tests/install/compare.py --cpp <build dir>/bin/Release/sc-offline.exe [--rust <install_probe.exe>] [--slow]
```

- 42 scenarios (one more with `--slow`): fresh install with and without `default_1.xml`, the player's
  xml backed up and restored, a foreign `dinput8.dll` set aside and put back, a second foreign DLL
  refused, a crash leftover (marker plus backups) that keeps its backups, an old install without a
  marker, `--dry-run` for both commands, uninstall in every marker state, the game running (a
  stand-in process, see below), Easy Anti-Cheat active with and without `--skip-eac-check`, a missing
  `dinput8.dll`, `--game` spellings, `game =` in the ini, ini warnings, the old Discord switch, the
  remembered game path (dry run only), a rotated `launcher.log`, a failed DLL copy (see the known limit below). `--slow` adds a read-only DLL that
  cannot be deleted: 30 tries, then exit 3.
- Everything lives under `--work` (default `D:/t/install-parity`, emptied first). No real game folder
  is used and every game path is checked to be under the scratch folder. The game is a plain file
  named `StarCitizen.exe`. `ProgramData`, `ProgramFiles(x86)`, `APPDATA` and `SystemRoot` point into
  the world. The two discovery scenarios use `--dry-run` because discovery also reads this PC's
  registry and folders.
- The running-game scenarios copy `ping.exe` to the scratch `Bin64\StarCitizen.exe`, run it for the
  length of one command and end it. It is not the game.
- The C++ launcher runs its copy in a hidden helper. In the scratch folder the helper needs no
  administrator rights, so no UAC prompt appears and nothing on this PC changes.

Not compared, and why:

- PC changes (firewall rules, the hosts line, the EAC rename) and their undo: another part of the
  port. No `pc-changes.txt` record is created, so `uninstall` never has one to undo. `Io::pc` is the
  hook for it.
- Starting the game (`play`) and the update check.
- Elevation. The Rust `run` does the helper's work in its own process and cannot ask for
  administrator rights; `needs_elevation` says when the C++ launcher would. A game folder that is not
  writable is therefore not covered.
- The usage text after an unknown argument, and the folder picker.
- File times and attributes.
- Non-ASCII paths. Text parity is ASCII only: the C++ launcher prints wide paths as mojibake in the
  "C" locale, the Rust run prints UTF-8. The file effects are the same.

Known limit shared with the C++ launcher (kept for parity, pinned by the scenario
`install_dll_copy_fails_leaves_xml_behind`): install does not roll back. If the player has no
`default_1.xml`, the mod's one is copied first. When the `dinput8.dll` copy then fails, that xml stays,
no marker is written, and `uninstall` only says it left the xml in place because it has no record of
it. Other half-done states (a foreign DLL set aside, an xml backup made) are put back by `uninstall`
even without a marker. A fix (write the marker before the first change, or delete the fresh xml when
the DLL copy fails) must change both launchers together, so it is an owner decision and not done here.

Line endings are normalised, the world path is replaced by `@W` and times by `<T>`.

Source and synthetic evidence only. None of this has been run in game; see [docs/status.md](../../../docs/status.md).
