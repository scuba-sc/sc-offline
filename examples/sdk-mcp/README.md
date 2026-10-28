# Tutorial-owned SDK consumers

Use [sco-core's SDK/MCP tutorial](https://github.com/scuba-sc/sco-core/blob/main/docs/sdk-mcp-tutorial.md) for static discovery, archive build, checking and owner installation/lifecycle steps. Some documentation embedded in the published RC2 archive predates its publication; see the [validation receipt](rc2-validation.md) for the actual release and limitations.

Download the combined `sco-sdk-2.4.0.zip` and `SHA256SUMS` from [SDK 2.4.0 RC2](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2). The archive SHA-256 is `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`. Verify it and its extracted inventory before running kit code. Copyable tutorial source and the archive proofs can be run outside this checkout:

```sh
python3 tools/check_sdk_tutorial.py --archive /absolute/path/sco-sdk-2.4.0.zip \
  --sums /absolute/path/SHA256SUMS --work /absolute/new/path/tutorial-dev
```

Add `--prepare-only` to copy the source without verifying the archive or running builds. The new `owned-source/plugins/{tutorial_npc,tutorial_inspector}/` contains editable public-SDK code with distinct IDs, commands and exports metadata, plus independently copied fixtures and shared picker header. Neither copy replaces production feature folders or links host registration. Public ABI identifiers remain unchanged. Preserve `LICENSE` when editing or distributing these project-authored sources. The script never moves the product's published SDK pin.

After verifying and extracting the combined archive, check the actual released discovery server and workflow bundle:

```sh
python3 examples/sdk-mcp/check_rc2_mcp.py --sdk /absolute/path/sco-sdk-2.4.0 \
  --transcript /absolute/new/path/mcp-transcript.jsonl \
  --scaffold /absolute/new/path/rc2_probe
```

The optional transcript contains full requests and replies, including generated code. The optional scaffold directory is created and populated, but not built. Review its files before using the kit's CMake check target. This check is pinned to RC2's combined kit; it is not an archive authentication step or a live-host check.

The [receipt](rc2-validation.md) records real published-archive and synthetic results, including the full tutorial proof's remaining dependency-trace blocker. Windows/MSVC, game install/reload/cleanup and runtime MCP are **not verified**. Not run in game.
