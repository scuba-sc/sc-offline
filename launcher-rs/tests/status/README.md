<!-- SPDX-License-Identifier: GPL-3.0-only -->
# status parity

`compare_status.py` runs the built C++ `sc-offline.exe` and the Rust `scol.exe` on the same synthetic
folders and requires the same text and exit code for `help`, `status` and the argument errors. It never
starts `play`, `install`, `uninstall` or `update`.

```
cmake --build <build dir> --config Release --target sc-offline
cd launcher-rs && cargo build --release
python tests/status/compare_status.py --cpp <build dir>/bin/Release/sc-offline.exe [--machine]
```

- Each scenario gets its own folder under `--work` (default `D:/t/status-parity`, emptied first) with a
  fake game (a plain file named `StarCitizen.exe`), and redirects `ProgramData`, `ProgramFiles(x86)`,
  `APPDATA` and `SystemRoot` into it. Covered: missing game, game without the mod, install marker, a
  foreign `dinput8.dll`, a leftover `pc-changes.txt` record, plugins with and without a `disabled`
  marker, build manifest and file-date versions, Easy Anti-Cheat states, hosts file states, ini
  warnings, `--game` spellings, and discovery through the remembered path and the RSI Launcher files
  (including two games and the same install under two spellings, one a junction).
- `status` must write nothing: the whole scenario tree is compared before and after each run, for both
  programs. The Rust exe is also scanned for network DLL imports.
- The Rust usage text has two lines the C++ usage lacks, because the C++ launcher has no `stop` and no `plugins`
  command (`scol_cli::CPP_USAGE_LINES_NOT_HERE`). `compare_status.py` drops exactly those two lines from the output
  before comparing; any other difference in the usage still fails. The crate test
  `the_usage_differs_from_the_cpp_usage_by_exactly_the_two_rust_only_lines` reads `kUsage` from `launcher.cpp`
  and checks the same.
- Line endings are normalised (the C++ launcher prints CRLF to a pipe) and the version banner is
  compared with the version replaced. Paths are ASCII.
- The registry and the fixed drives cannot be redirected. A real install on the PC can show up as an
  extra "also found" line in both programs; it is only looked at. `--machine` adds the scenarios with
  nothing synthetic to find.
- The bounded drive search runs only when nothing cheaper finds a game, so it is covered by a unit
  test on a synthetic tree (`scol-install`, `drive_search_depth_skips_and_budget`), not compared with the
  C++ launcher.

Source and synthetic evidence only. Not run in game.
