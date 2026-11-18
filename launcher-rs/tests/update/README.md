<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Update tests

`update.rs` is the test target of `crates/scol-update` (`cargo test -p scol-update`). A local fake serves release
metadata and zips and another fake stands in for the machine (game running, self-test, elevation), so nothing
touches the network or a real install. Scenarios: good update, tampered zip digest, tampered file digest, oversized
file and download, `..` paths, unlisted files, downgrade, interrupted swap then recovery, failed swap, failed
self-test, running game, `check_updates = off`, elevation, a C++ journal, and the catalogue fetch path.

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
