# Independent MIT-wire peer fixture

Configure/build this directory separately with `SCO_WIRE_INCLUDE_DIR` pointing at
`sc_ipc.h`, `sc_titanlink.h`, `sc_voxel_bridge.h` from an identified SDK. These three
headers alone are sufficient; no sco-core library, `sco_api.h`, product include path,
host submodule, plugin loader or game is required. It is never installed or released.

```text
cmake -S tools/bridge-peer -B peer-build -DSCO_WIRE_INCLUDE_DIR=<wire-headers>
cmake --build peer-build --target bridge-peer
```

On Windows: `bridge-peer titanlink [seconds]` / `bridge-peer voxel [seconds]`.
The owner must enable/start the separately built optional plugin. The fixture opens,
never creates, the channel, validates version/directions, exchanges bounded messages,
beats and refuses stale owners. Ctrl+C stops it; owner timeout observes peer loss.
On development platforms only the independent wire endpoint compiles; `--wire-info`
reports versions. Native transport otherwise refuses explicitly, never emulates game
or cross-process results. Windows runtime and paired-game behavior are NOT VERIFIED.

The fixture remains project GPL-3.0-only. Its dependency boundary is the independently
MIT-licensed wire, **not a relicensing of project code**. Preserve the MIT notices in
the three published headers. This fixture does not include any game data or second
peer game's code; synthetic wall/match responses are not gameplay acceptance.
