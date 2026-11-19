# Plugin zip corpus

Good and hostile plugin archives and `plugin.ini` files, shared by the Rust verifier
(`crates/scol-plugins`) and the SDK Python tool (`sdk/tools/sco-plugin-verify.py` in sco-core).

- `generate.py` writes `cases/`, `manifests/` and `expected.tsv`. `generate.py --check` fails if the
  files on disk differ. Archives are built byte by byte with the standard library. Payloads are
  inert test bytes.
- `expected.tsv` holds the verdict for each file. It is written by hand in `generate.py`, not
  copied from either tool.
- Rust: `cargo test -p scol-plugins --locked --offline --test corpus`.
- Python: build `sco-plugin-check` from the sco-core `sdk/` folder with its CMake file, then
  `python run_python.py --sdk <sco-core>/sdk --checker <path to sco-plugin-check>`.
- `PYTHON-TOOL.txt` pins the sco-core commit and file hashes the two tools were last agreed on.

CI runs the Rust corpus test (inside `cargo test --locked`) and `generate.py --check`. The Python
parity run is manual: it needs a `sco-plugin-check` built from sco-core, which CI does not build.
Rerun it whenever the verifier, the corpus or the pinned sco-core tool changes.

Only the pass or refuse verdict is compared with Python for refused archives. Message text is not
guaranteed identical (quoting of names and number formatting differ). The Rust test checks the exact
Rust error code against `expected.tsv`.

Both tools passed all files at the pinned commit. Static checks only. None of this has been run in game; see [docs/status.md](../../../docs/status.md).
