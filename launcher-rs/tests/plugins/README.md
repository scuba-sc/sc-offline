<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Plugin manager proof run

`creative-proof.py` runs the whole plugin chain with the real creative plugin in a scratch
install: zip with the SDK layout, index built and signed with the sc-plugins tools (TEST key),
then `catalogue list`, `catalogue install`, `enable`, `list`, a replayed older index, `remove`,
with a file-tree diff after every step. `creative-proof.transcript.txt` is the output of the last
run. The creative files were taken from an existing build (read only), not built here. Nothing was
run in game and creative.dll was never loaded.

```text
cargo build -p scol-plugin-manager --features test-trust
python tests/plugins/creative-proof.py --creative-dir <folder with creative.dll and plugin.ini> \
  --index-tools <sc-plugins checkout> --scratch D:/t/pm-proof-1 \
  --scol-pm <target dir>/debug/scol-pm.exe --transcript out.txt
```

The `test-trust` feature adds `--test-trust-key <file>`. A release build has no such option and
trusts only the keys compiled into scol-catalogue.
