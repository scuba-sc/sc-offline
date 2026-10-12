# G1 host-source composition (incremental)

The shell can consume a local extracted `host-source-sc` kit with
`-DSCO_HOST_SOURCE_DIR=<extracted-source-root>`. This does not repin the published
SDK or submodule. Kits must provide `sco_game_host`; the published older pin
builds without it but **refuses offline startup, with no private fallback**.

The game-pack composition owns original-image signature resolution, required
synchronous quit interception, app startup, plugin tick admission/cadence, task
cancellation, early DataCore/CryPak discovery and producer drain. The product
retains Windows proxy forwarding, read-only OS inspection, message/window/input/
render policy, paths/configuration, launcher consent/restoration, and remaining
legacy feature engines pending their own migrations. Bootstrap now runs on the
worker, outside DLL loader lock. The WM_QUIT path stops before doing queued work.
Disabled/failed/crashed plugins have no host feature-tick fallback.

## Refusal boundaries (not a playable-host certification)

- OS module/process/service failures remain unknown, never anti-cheat absence.
  Detection is read-only; there is no anti-cheat manipulation or bypass.
- Live offline-session and effective official-network-block evidence have no
  verified collector in this shell. Both remain **unknown**, so real startup
  deliberately refuses. Launcher flags, patch success and EAC absence do not
  certify these conditions. No user/environment-variable override is provided.
- The current G1 early DataCore route rejects builtin inputs. This product still
  retains some builtins, so that combination also refuses. Finish externalization
  or first extend the core contract; do not disable DataCore or use the retired
  private quantum loader to circumvent that refusal.
- The quantum data file is retained; ordinary folder packaging is a separate
  migration. `quantum.drive` remains unavailable until its consumer can establish
  a specific applied-pack outcome. LOADED is not such an outcome.
- G8 control is not approved and the retained private hook installer is not called.
- Legacy offline patches/fake services are still staged migration work. This is
  **not full G1/6A closure**. No Windows, game, network, launcher-restoration or
  renderer acceptance is claimed.

## Compile-only development target

```sh
cmake -S tools/offline-host -B <outside-build-root> \
  -DSCO_HOST_SOURCE_DIR=<extracted-source-root>
cmake --build <outside-build-root> --target sc_offline_host_composition
```

This builds the actual portable product seam and its G1 dependencies, not the
Windows DLL. Windows x64/MSVC host build: **NOT VERIFIED on macOS**. Lifecycle,
startup/shutdown ordering, repeated-stop and synthetic task-cancellation tests:
**NOT RUN** under the owner's speed-over-proof order.
