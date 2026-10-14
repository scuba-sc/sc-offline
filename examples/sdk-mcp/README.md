# Tutorial-owned SDK consumers

Use [sco-core’s SDK/MCP tutorial](https://github.com/scubamount/sco-core/blob/main/docs/sdk-mcp-tutorial.md) for the end-to-end static discovery, archive build, checker, named dynamic fixture and owner installation/lifecycle steps.

Copyable source is generated from this checkout by:

```sh
python3 tools/check_sdk_tutorial.py --archive /absolute/path/sco-sdk-2.2.0.zip \
  --sums /absolute/path/SHA256SUMS --work /absolute/new/path/tutorial-dev --prepare-only
```

The new `owned-source/plugins/{tutorial_npc,tutorial_inspector}/` contains editable public-SDK code with distinct IDs, commands and exports metadata, plus independently copied fixtures. Neither copy replaces production feature folders or links host registration. Public ABI identifiers remain unchanged. Preserve `LICENSE` when editing/distributing these project-authored sources. The script never moves the product’s published SDK pin.

**This revision:** both tutorial modules compile against a checksum-verified local development SDK archive from sco-core `integ/r1`, outside the checkout. Published archive execution, checker, named dynamic fixture, static MCP transcript and stale/session negatives: **NOT RUN**. Windows/MSVC, game install/reload/cleanup and runtime MCP: **NOT VERIFIED**. The archive is local development evidence, not a release asset. See [status](../../docs/status.md) and [existing feature workflows](../../docs/sdk-workflows.md).
