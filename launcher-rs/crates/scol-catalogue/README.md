# scol-catalogue

The plugin catalogue client of the sc-offline Rust launcher ([docs/rust-launcher.md](../../../docs/rust-launcher.md#plugins-and-the-catalogue)). No network code, no UI, no file writes, and it never loads plugin code. The network is `scol-update`'s WinHTTP code behind the `Fetcher` trait, and the file writes are `scol-plugin-manager`'s.

Not run in game. See [docs/status.md](../../../docs/status.md). The tests are synthetic.

## Pipeline

1. `index::open_index(bytes, sig_text, trust, now_unix, last_sequence)`: size cap, Ed25519
   signature over the exact bytes (before any parsing), strict parse (unknown field, duplicate key,
   bad value, unsorted or repeated id are errors), expiry, and no rollback below `last_sequence`.
2. `policy::check_entry` / `policy::offered`: id not reserved (`sco`, `host`, `menu`, `game`), review
   level `first-party` or `reviewed`, plugin api major equal and minor not newer, SDK range admits the
   host SDK (SemVer precedence: `2.4.1-dev.6` is older than `2.4.1`).
3. `fetch::Fetcher` (a trait the launcher implements), `fetch::fetch_index`, `fetch::download_zip`:
   HTTPS only, host on an allowed list, byte caps (the zip cap is the size the signed index declares).
4. `install::verify_download`: size and SHA-256 from the index first, then the `scol-plugins` zip
   verifier, then a check that the zip names the same id, version, kind and api as the index.
5. `install::plan_install`: where the files go under `data/plugins/<id>/`, the empty `disabled`
   marker, the new `data/launcher/installed.json` text, a native-code warning for native plugins.
   Local folders are not replaced without a confirm, downgrades need a confirm. The plan is data;
   `scol-plugin-manager` stages, journals and swaps.

`cli::run_with(args, io, trust)` and `cli::run(args, io)` are the command entries (`list`, `plan`);
they read files you name and print.

## Trust root

`trust::PRODUCTION_KEYS` is `scol_contract::product::CATALOGUE_TRUST_KEYS`. It is empty until the owner generates the signing key offline and adds its public half. With no key every index is refused (`no-trust-keys`). The TEST key under
`launcher-rs/tests/catalogue/golden/` is for tests only and must never be added there.

## Signature format

Signed message: `sc-plugins-index-v1` and a newline, then the exact bytes of `index.json`.
`index.json.sig`: `sc-plugins-index-sig-v1 <key id> <signature>` and a final newline. Key id: first 16
hex digits of SHA-256 of the 32-byte public key. The index is written by `tools/build_index.py` in
sc-plugins and signed by `tools/sign_index.py`; the golden pair in `tests/catalogue/golden/` comes from
those tools and the tests check that this crate accepts it.
