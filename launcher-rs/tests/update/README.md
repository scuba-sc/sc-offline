<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Update tests

`update.rs` is the test target of `crates/scol-update` (`cargo test -p scol-update`). A local fake serves release
metadata and zips and another fake stands in for the machine (game running, self-test, elevation), so nothing
touches the network or a real install. Scenarios: good update, tampered zip digest, tampered file digest, oversized
file and download, `..` paths, unlisted files, downgrade, interrupted swap then recovery, failed swap, failed
self-test, running game, `check_updates = off`, elevation, a C++ journal, and the catalogue fetch path.

Added with the command side of the launcher:

- the offer (`flow::offer_update`): the question text, no question without a console (`(not asking: no console to
  answer in)`), a running game refuses before the question, `--check` never asks, `--yes` never asks, a failed swap
  says so; the quiet form used before `play` prints nothing when there is nothing to install;
- the restart: after a swap the platform's `after_update` runs with the command line (a console run starts the new
  exe with it and returns its code, the window's child returns 10), and `play` ends there instead of going on with
  the old program; the `update` command ends with the new exe's code;
- the lock back-off: a file held without sharing makes the swap wait 100, 250, 500, 1000, 2000 and 4000 ms in turn,
  then give up and put everything back; a lock that goes away during the third wait lets the swap through; the retry
  helper waits only for sharing violation (32), lock violation (33) and access denied (5);
- `move_through` replaces the target and writes through (`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`);
- the self-test with a normal-user token (`Safer`, the C++ launcher's `SelfTestPasses`): it runs with that token or,
  when the token cannot be used, with this process's own rights. The test says which one it got; an elevated
  process was not available here, so only the not-elevated case was exercised.

## Old-launcher contract

Two tests run only when `SCOL_CPP_UPDATE_EXE` names a C++ `sc-offline.exe` built with `-DSCO_UPDATE_TEST`
(otherwise they print `SKIP`):

- the C++ exe runs `--apply-zip <zip> <tag> <sha256>` against a zip made here (same manifest format 1, same digests)
  whose new `sc-offline.exe` is `examples/standin.rs`, a stand-in that only answers `--self-test <tag>`. Checked:
  good update, new exe that fails its self-test (rolled back), tampered file (refused), `SCO_TEST_FAIL_AFTER=1`
  (rolled back);
- the Rust updater swaps in a zip whose new exe is the C++ exe, using the real `--self-test` launch, and rolls
  back when the exe is not the promised version.

Build that C++ exe: `cmake -S <sc-offline> -B <dir> -DSCO_HOST_SOURCE_DIR=<core export> "-DCMAKE_CXX_FLAGS=-DSCO_UPDATE_TEST -EHsc"`
then `cmake --build <dir> --config Release --target sc-offline`.

`--features test-hooks` adds `--apply-zip` to the Rust command, like the C++ test build. Never in a release build.
Nothing here ran in game.
