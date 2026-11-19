# Bridge peer fixture

A stand-in for the other game, used to test the TitanLink and voxel_bridge plugins ([bridges](../../docs/bridges.md)). It builds on its own with only three MIT wire headers from an identified SDK: `sc_ipc.h`, `sc_titanlink.h` and `sc_voxel_bridge.h`. It needs no sco-core library, `sco_api.h`, product include path, host submodule, plugin loader or game. It is never installed or released.

```text
cmake -S tools/bridge-peer -B peer-build -DSCO_WIRE_INCLUDE_DIR=<wire-headers>
cmake --build peer-build --target bridge-peer
```

On Windows, run `bridge-peer titanlink [seconds]` or `bridge-peer voxel [seconds]` after you enable and start the matching plugin. The fixture opens the channel and never creates it. It checks the version and directions, exchanges bounded messages, beats, and refuses stale owners. Ctrl+C stops it, and the plugin then sees the peer leave after its timeout. `--wire-info` prints the wire versions. On other platforms only the wire endpoint compiles, and the native transport refuses. The fixture never fakes game or cross-process results.

The fixture is GPL-3.0-only. Only the wire headers are MIT, and they do not relicense project code. Keep the MIT notices in the three headers. The fixture contains no game data and no code from the other game. Its synthetic wall and match replies are not gameplay results. Windows runtime and paired-game behavior are not verified.
