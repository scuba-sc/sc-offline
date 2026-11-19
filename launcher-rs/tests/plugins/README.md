<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Plugin manager proof run

`creative-proof.py` runs the whole plugin chain with the real creative plugin in a scratch
install: zip with the SDK layout, index built and signed with the sc-plugins tools (TEST key),
then `catalogue list`, `catalogue install`, `enable`, `list`, a replayed older index, `remove`,
with a file-tree diff after every step. `creative-proof.transcript.txt` is the output of the last
run. The creative files were taken from an existing build (read only), not built here. `creative.dll` was never loaded.
None of this has been run in game; see [docs/status.md](../../../docs/status.md).

```text
cargo build -p scol-plugin-manager --features test-trust
python tests/plugins/creative-proof.py --creative-dir <folder with creative.dll and plugin.ini> \
  --index-tools <sc-plugins checkout> --scratch D:/t/pm-proof-1 \
  --scol-pm <target dir>/debug/scol-pm.exe --transcript out.txt
```

`settings_proof.py` checks `plugins settings <id> [name=value ...] [--reset]` with a second SQLite implementation
(Python's `sqlite3`, not the copy inside the exe): the values land in `data\storage\<id>.db`, table
`sco_kv(key TEXT PRIMARY KEY, value BLOB) WITHOUT ROWID`, keys `sco.settings.<name>`, values the blobs
`<type>:<canonical text>`; a row written by Python in that shape is read back by the exe; a refused value leaves the
file byte for byte the same; `--reset` deletes the kept rows. Synthetic plugin under `D:\t`, no game.

```text
python tests/plugins/settings_proof.py [--rust <sc-offline.exe>]
```

Without `--source`, `plugins catalogue ...` goes to the product URL through WinHTTP, but only when a trust key is
built in (`scol_contract::product::CATALOGUE_TRUST_KEYS`); this build has none, so it prints `no catalogue is
published for this build yet` and makes no request (the crate tests `settings_and_net.rs` and the CLI tests check
that, and that `check_updates = off` stops the request when a key is there).

The `test-trust` feature adds `--test-trust-key <file>`. A release build has no such option and
trusts only the keys compiled into scol-catalogue.
