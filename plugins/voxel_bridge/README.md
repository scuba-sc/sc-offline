# Optional voxel bridge (SDK consumer)

Independent native plugin ID/version: `voxel_bridge` / `1.0.0`; ABI 1.1.
GPL-3.0-only project code. The `sc_ipc.h` / `sc_voxel_bridge.h` wire remains
SDK-provided MIT code; no wire/header or peer-game copy is vendored here.

**Disabled by default and outside normal release.** Configure this directory
alone; it refuses being added to a host/default-plugin build. Its optional
install component always carries `disabled`. Host built-in removal, dependency
pin promotion and release composition are separate integration work. Do not
install alongside a built-in with the same ID.

## Build, check, install

1. Obtain a checksum-verified combined core/game SDK archive containing
   `sc_game_spatial.h` (`game.spatial` full-pose conversion). Do not use a host
   checkout as an include path. This development consumer needs the new public
   contract; the immutable published SDK 2.0.0 baseline is not claimed to contain
   it. The repository's published dependency pin is unchanged.
2. Run the focused archive proof (outside the host checkout):
   ```sh
   python3 tools/check_voxel_bridge_sdk.py --archive /path/to/sdk.zip \
     --sums /path/to/SHA256SUMS --work /path/to/new-proof-directory
   ```
   It copies only this plugin and its fixture outside the checkout, builds and
   dynamically loads the actual module, validates compiler dependencies and
   private-header/symbol/transitive-SYSTEM negative controls on POSIX, installs
   the disabled folder, and enables **only a checker copy** for missing-provider
   checking. This is not game execution or Windows transport evidence.
3. Install only the optional component:
   ```sh
   cmake -S plugins/voxel_bridge -B /path/to/build \
     -DSCO_SDK_INCLUDE_DIR=/path/to/verified-sdk/include
   cmake --build /path/to/build --config Release --target sc-offline-voxel_bridge
   cmake --install /path/to/build --config Release --prefix /path/to/stage \
     --component voxel_bridge
   ```
   Keep the folder disabled until an owner deliberately enables it in an
   admitted offline host. Windows modules are built with MSVC; development
   `.so`/`.dylib` modules are not Windows game-loadable artifacts. Do not bundle
   either bridge or a paired game in a default release.

## Preserved commands and policy

- `voxel_bridge.toggle`: open host-owned local `sco.ipc` channel `link`, or close
  it and initiate owned-crate cleanup. `Ctrl+F9` uses optional public `sco.ui`.
- `voxel_bridge.anchor`: set one building area at the observed player feet and
  orientation, remove old crates, increment its epoch and send `AREA`.
- `voxel_bridge.status`: link/area, counts, pending cleanup, provider refusal and
  explicit unavailable feature reasons.

The wire layout is unchanged (`VXB1`, version 1; 8 MiB). Block `(0,64,0)` is at
anchor feet, x right, y up, -z forward. Up to eight owned `game.entities` crates
spawn per tick; at most 4096 solid keys (including pending cleanup) are retained.
The **provider's global quota (currently 1024 owned entities) can refuse earlier**;
refusals are counted/not hidden behind private spawn. A replaced section removes
cleared blocks; zero-ID unspawned tickets are cancelled, not endlessly queued.
One 8x8 ground region is scanned per tick with public zone-local
`game.world.raycast`; outbound ground positions are converted back into the
same voxel frame. The cache refreshes after three seconds. Cross-zone player
position **and quaternion** use `game.spatial.pose_to_zone`, never the old
position-only/direction approximation. Absent full-pose conversion pauses that
cross-zone observation explicitly. Body orientation remains the retained pose
source, not a guessed camera view.

`SOLIDS` epochs, section bounds, payload lengths and finite peer coordinates are
checked. Unknown/wrong-sized records are skipped; malformed raw ring/state
closes the channel. A heartbeat age of 7999 ms is live; 8000 ms closes and cleans
owned crates. Initial no-peer state remains waiting, without crate mutations.
Closed-channel/provider failures close safely. Local peers are trusted to request
bounded crate placement, **never command execution or player relocation**.

Ownership is from `game.entities.spawn` to its matching `despawn`, never legacy
NPC deletion or `spawn_as` ownership. Successful despawn means initiation, not
observed disappearance; no `alive=0` inference or ID reuse is made. Refused
cleanup IDs remain bounded retry tickets; opening a new link refuses while they
remain. Unload stops the tick/hotkey, closes the channel and requests cleanup;
the public provider's owner-release contract handles anything still pending.
No entity is `keep`-released. No actor or foreign entity removal is introduced.

## Settings and explicit unavailable features

The retained keys `block`, `size`, `pivot`, `radius` are manifest settings read
through `sco.settings` when opening. Defaults/ranges match the old config;
`block=none` disables crate spawning. **Automatic import of old
`data/voxel_bridge.txt` is unavailable**: there is no public arbitrary host-path
reader. Copy those four values deliberately into declared settings; the old
file is never modified or deleted. Without `sco.settings`, declared defaults
are used. This limitation is not a claim of automatic migration.

- **Follow the voxel player / local-player relocation: unavailable. G2 mutation
  is not approved.** No relocation service is opened and no private fallback
  exists. The peer receives the Star Citizen player's pose instead.
- The retained **Voxel** controls/status are a renderer-neutral public
  `sco.widgets` panel (`voxel_bridge.voxel`), not a private ImGui tab. Open/close
  and re-anchor buttons call the same command implementations. Peer world/age,
  sanitized status/log, area/feet, counts, block settings and refusal replies
  remain visible. Without complete `sco.widgets` methods, the panel is explicitly
  unavailable; commands still work. Actual Windows rendering is NOT VERIFIED.
- Focus and view dimensions are explicitly unknown (wire focus flag and width/
  height stay zero); no private product/window helper is used.
- Input takeover, hiding the body, per-frame callbacks, NPC mirroring/hits,
  textured voxel overlay, light matching, peer game console commands and multiple
  simultaneous areas remain the prior unported/unavailable feature inventory.
  There is no new private implementation or peer-game dependency.

Native plugins are trusted code, not a sandbox. The host/game pack must enforce
admitted offline/EAC/session/thread/ownership state. Synthetic fixture readiness
never overrides those checks.

## Evidence boundary

`voxel-bridge-module-workflow` dynamically loads the real module using only
extracted SDK headers and synthetic public providers. IPC provider stand-ins
execute the real MIT wire's seqlock/ring/heartbeat checks on an aligned in-process
mapping. It covers absent providers and reload/unload twice; handshake/version
refusal; area, exact crate pose and decoded 64-column ground; quaternion frame
conversion and unavailable conversion; cancelled pending keys while paused;
sanitized peer logs; public panel buttons and hotkey cleanup; malformed
payloads/coordinates/epochs/raw ring; heartbeat
boundary; closed peer; pending owned cleanup; and physically partial/full-sized
null callbacks. POSIX archive checking proves module link/dependency boundaries,
not production loader crash guards or game provider behavior.

**NOT VERIFIED:** MSVC/Windows loader and dependency traces; actual cross-process
Windows mapping/ACLs; host/game owner-release hook with rejected cleanup;
renderer/panel/focus/view behavior; owner-run gameplay on an exact build; physical
crate disappearance after initiation. These are not certified by synthetic green.
