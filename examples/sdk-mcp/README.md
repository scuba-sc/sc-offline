# Tutorial-owned SDK consumers

Use [sco-core's SDK/MCP tutorial](https://github.com/scuba-sc/sco-core/blob/main/docs/sdk-mcp-tutorial.md) for static discovery, archive build, checking and owner installation/lifecycle steps. Some documentation in the published SDK 2.4.0 archive still reflects earlier RC2 snapshots; use the current release manifest for package identity. The [dated RC2 receipt](rc2-validation.md) records historical MCP evidence, not a stable-release MCP run.

Download the combined `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the [published stable SDK 2.4.0 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0). The archive SHA-256 is `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc`. Verify it and its extracted inventory before running kit code. Copyable tutorial source and the archive proofs can be run outside this checkout:

```sh
python3 tools/check_sdk_tutorial.py --archive /absolute/path/sco-sdk-2.4.0.zip \
  --sums /absolute/path/SHA256SUMS --work /absolute/new/path/tutorial-dev
```

Add `--prepare-only` to copy the source without verifying the archive or running builds. The new `owned-source/plugins/{tutorial_npc,tutorial_inspector}/` contains editable public-SDK code with distinct IDs, commands and exports metadata, plus independently copied fixtures and shared picker header. Neither copy replaces production feature folders or links host registration. Public ABI identifiers remain unchanged. Preserve `LICENSE` when editing or distributing these project-authored sources. The script never moves the product's published SDK pin.

The checker below is pinned to the 2026-10-28 RC2 source (`844e136…`) and is retained to reproduce the dated receipt. Do not run it against the stable archive, whose source is `6106fa…`. A stable-release MCP transcript is not recorded here; use the upstream SDK tutorial for current discovery steps.

The optional transcript command from the historical receipt is:

```sh
python3 examples/sdk-mcp/check_rc2_mcp.py --sdk /absolute/path/rc2/extracted/sco-sdk-2.4.0 \
  --transcript /absolute/new/path/rc2-mcp-transcript.jsonl \
  --scaffold /absolute/new/path/rc2_probe
```

The optional transcript contains full requests and replies, including generated code. The optional scaffold directory is created and populated, but not built. Review its files before using the kit's CMake check target. This historical check is pinned to RC2's combined kit; it is not an archive authentication step or a live-host check.

The [receipt](rc2-validation.md) records real published-archive and synthetic results, including the Linux toolchain setup used for the full tutorial proof. Windows/MSVC, game install/reload/cleanup and runtime MCP are **not verified**. Not run in game.
