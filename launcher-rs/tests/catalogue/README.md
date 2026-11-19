# Catalogue test fixtures

Used by `crates/scol-catalogue/tests/`. Synthetic only. None of this has been run in game; see [docs/status.md](../../../docs/status.md).

`golden/` holds a signed index made by the sc-plugins tools (`tools/build_index.py` and
`tools/sign_index.py`, from `tests/fixtures/` there) with made-up plugin names, and the TEST key
pair that signed it:

- `index.json`, `index.json.sig`: copied byte for byte from sc-plugins `tests/fixtures/`. The Rust
  client must accept them with the TEST public key. That is the cross-language check: Python wrote
  and signed, Rust verifies. If sc-plugins changes the format, regenerate and copy both files.
- `TEST-ONLY-public.hex`, `TEST-ONLY-private-seed.hex`: the TEST key pair. The private half is
  public on purpose. The tests also use it to sign the indexes they make in memory (tampered,
  expired, wrong sequence and so on). It must never be added to `PRODUCTION_KEYS`.

The zip files the install tests use are the shared corpus in `../zip-corpus/cases/`
(`good-minimal`, `good-lua-plugin`, `good-data-pack` and a hostile one). The tests read each zip's
facts from the verifier and build the index entry from them, so they do not copy zip bytes here.

`.gitattributes` keeps the golden files byte exact.
