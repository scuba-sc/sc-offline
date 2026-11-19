# SDK and MCP tutorial sources

Copyable plugin sources with their own IDs, plus the scripts that prove them against a published SDK. For the tutorial itself (static discovery, building from an archive, checking, installing and reloading), use [sco-core's SDK and MCP tutorial](https://github.com/scuba-sc/sco-core/blob/main/docs/sdk-mcp-tutorial.md). Some documents inside the published SDK 2.4.0 archive still describe an earlier snapshot. Use the release manifest for the package identity.

Download the combined `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the [2.4.0 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0). The archive's SHA-256 is `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc`. Verify the archive and its extracted inventory before you run any kit code. You can run the copy and the proofs outside this checkout:

```sh
python3 tools/check_sdk_tutorial.py --archive /absolute/path/sco-sdk-2.4.0.zip   --sums /absolute/path/SHA256SUMS --work /absolute/new/path/tutorial-dev
```

Add `--prepare-only` to copy the source without verifying the archive or building. The copy script reads and writes UTF-8 explicitly.

The copy goes to `owned-source/plugins/{tutorial_npc,tutorial_inspector}/`: editable code against the public SDK, with its own IDs, commands and export metadata, and its own copies of the fixtures and the shared picker header. Neither copy replaces a production plugin folder or links to host registration. Public ABI identifiers are unchanged. Keep `LICENSE` when you edit or distribute these sources. The script never moves the product's SDK pin.

## Checking the earlier snapshot

`check_rc2_mcp.py` is pinned to the 2026-10-28 SDK snapshot (source `844e136...`). Run it only to reproduce the [dated receipt](rc2-validation.md). Do not run it against the 2.4.0 archive, whose source is `6106fa...`. No MCP transcript against 2.4.0 is recorded here. Use the upstream tutorial for current discovery steps.

```sh
python3 examples/sdk-mcp/check_rc2_mcp.py --sdk /absolute/path/rc2/extracted/sco-sdk-2.4.0   --transcript /absolute/new/path/rc2-mcp-transcript.jsonl   --scaffold /absolute/new/path/rc2_probe
```

The transcript holds full requests and replies, including generated code. The scaffold folder is created and filled but not built. Read its files before you use the kit's CMake check target. The check is not an archive authentication step or a live-host check.

The receipt records real archive and synthetic results, including the Linux toolchain setup for the full tutorial proof. Windows and MSVC, game install, reload and cleanup, and runtime MCP were not verified. Not run in game; see [status.md](../../docs/status.md).
