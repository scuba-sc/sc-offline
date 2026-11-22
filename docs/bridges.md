# Bridges: Titanfall 2 and a voxel game

TitanLink and voxel_bridge are independent, optional native plugins: [`plugins/titanlink`](../plugins/titanlink/README.md) and [`plugins/voxel_bridge`](../plugins/voxel_bridge/README.md). Both install with a `disabled` marker. Neither bridge, the peer fixture nor bridge data is part of the release package, and `dinput8.dll` links and registers neither. The retired host options `SCO_BRIDGE_*` refuse to configure instead of creating a hidden fallback. Not run in game; see [status.md](status.md).

## Build, install and enable

1. Get an identified SDK that contains the IPC and spatial reads and full-pose conversion. The published SDK 2.4.1 release candidate 1 archive (`sco-sdk-2.4.1.zip`, tag `sdk-v2.4.1-rc.1`) does (`sco_ipc.h`, `sc_game_spatial.h`). Use no private host headers.
2. Configure each bridge on its own:

   ```text
   cmake -S plugins/titanlink -B titan-build -DSCO_SDK_INCLUDE_DIR=<sdk>/include
   cmake --build titan-build --target sc-offline-titanlink
   cmake --install titan-build --prefix bridge-stage --component titanlink
   cmake -S plugins/voxel_bridge -B voxel-build -DSCO_SDK_INCLUDE_DIR=<sdk>/include
   cmake --build voxel-build --target sc-offline-voxel_bridge
   cmake --install voxel-build --prefix bridge-stage --component voxel_bridge
   ```
3. Copy only the folder you want from `bridge-stage/data/plugins/` into the host's plugin folder. Remove its `disabled` marker to enable it. `plugins = on` must be set. Keep it disabled until the maintainer has accepted it on Windows, offline.

TitanLink has the commands `titanlink.pilot`, `titanlink.titan`, `titanlink.embark`, `titanlink.unlink` and `titanlink.status`, the key F9 (opens the link), a panel, and an owned Windows overlay and local launcher. The voxel bridge has `voxel_bridge.toggle`, `voxel_bridge.anchor` and `voxel_bridge.status`, the key Ctrl+F9, a panel, owned crate placement and bounded `game.world` ground scans. Each plugin's README covers settings, transport and cleanup, and what is unavailable.

What the bridges do not do:

- Relocation, follow, movement and camera-cvar changes are refused, with no private fallback.
- Poses use full pose conversion.
- Crate cleanup uses `game.entities.spawn` and `despawn` ownership, never private NPC deletion.
- A missing service refuses its operation.
- `titanlink.txt` and `voxel_bridge.txt` are not imported automatically, because no public root or content route exists. Back the files up and copy the setting names over by hand. No legacy file is overwritten or deleted.
- Multiplayer is not part of the bridges. `SCO_MULTIPLAYER=ON` refuses, and Core's private-session support stays off. This local IPC work never allows hosting, joining or private networking.

## Peer fixture

[`tools/bridge-peer`](../tools/bridge-peer/README.md) builds on its own with only `sc_ipc.h`, `sc_titanlink.h` and `sc_voxel_bridge.h` (MIT). It needs no host, `sco_api.h` or runtime library. It stays GPL-3.0-only; the MIT wire headers do not relicense project code. It ships no game code or assets.

On Windows, start `bridge-peer titanlink [seconds]` or `bridge-peer voxel [seconds]` after you enable the plugin. It sends synthetic match and wall replies, not gameplay results. Ctrl+C stops its heartbeat. `--wire-info` prints the wire versions. On other platforms the native transport refuses.

Not verified: Windows and MSVC builds, cross-process IPC, GPU and shared handles, a late peer exit, launcher and overlay cleanup, and gameplay. The fixtures are synthetic. No game, network or anti-cheat step was run.

## The wire

Both channels follow `sc_ipc.h`: the 64-byte `sc_ipc_hdr` at offset 0 (magic `SCIO`, version, pids, epoch, both heartbeats, size, `layout_id` / `layout_version`, state), then seqlock **blocks** (`sc_ipc_block`, 16 bytes, then the struct) and single-producer **rings** (`sc_ipc_ring`, 192 bytes, then the capacity) at the offsets in the tables. The layouts are defined once, in sco-core's SDK: [`sc_titanlink.h`](../external/sco-core/include/sc_titanlink.h) and [`sc_voxel_bridge.h`](../external/sco-core/include/sc_voxel_bridge.h), MIT, plain C11 / C++20, with every size and offset pinned by `static_assert`s (and by sco-core's `tests/abi_titanlink.c` and `tests/abi_voxel_bridge.c`; [sco-core docs/ipc.md, Bridge layouts](../external/sco-core/docs/ipc.md#bridge-layouts)). Independent modules use extracted public SDK headers; the peer needs only those three MIT headers. The other side vendors the same headers from the SDK zip; its `TL_LAYOUT_VERSION` / `VX_LAYOUT_VERSION` must equal sc-offline's, or `sc_ipc_attach` refuses the channel (`SC_IPC_MISMATCH`). This page describes the same layouts in prose. Everything is little-endian, with natural alignment.

**What the other side must do:**

1. Open the mapping by name (`OpenFileMappingW` + `MapViewOfFile`; never create it), take the view's size from `VirtualQuery`, and call `sc_ipc_attach` with the layout id and version below, then `sc_ipc_ring_attach` for both rings.
2. Beat at least once a second (`sc_ipc_peer_beat`). TitanLink treats you as gone after 4 s without a beat, the voxel bridge after 8 s.
3. Re-attach on `SC_IPC_EPOCH` (sc-offline re-created the channel) and stop on `SC_IPC_GONE` (it closed it). `sc_ipc_check` tells you without touching a ring.
4. Treat everything sc-offline writes as untrusted too: `sc_ipc.h` bounds-checks the rings and blocks, but the values inside are yours to check.

sc-offline checks everything you write in the same way: flags are masked, every coordinate must be finite and in range, text is cut to its array and limited to printable ASCII, a message of the wrong size or an unknown type is skipped, and a ring that fails `sc_ipc.h`'s validation is ignored from then on (logged once).

### TitanLink: `Local\SCO_titanlink.link`

`layout_id` `0x314B4C54` ("TLK1"), `layout_version` 1, 64 KiB. Units are Titanfall's (39.37 per metre), angles are Source's (pitch, yaw, roll in degrees, pitch positive looking down), z up.

| Offset | What | Written by |
|---|---|---|
| `0x0100` | block `tl_sc_state` (80 bytes) | sc-offline, ~10 Hz |
| `0x0400` | block `tl_tf_state` (124 bytes) | Titanfall side |
| `0x0600` | block `tl_frame` (136 bytes) | Titanfall side |
| `0x1000` | ring, `SC_IPC_TO_PEER`, 16 KiB | sc-offline pushes |
| `0x5100` | ring, `SC_IPC_FROM_PEER`, 16 KiB | Titanfall side pushes |

**`tl_sc_state`:** `uint64 time_ms` (`GetTickCount64` when written; interpolate with `vel` between writes), `uint32 flags` (1 pilot mode, 2 Star Citizen in front, 4 anchored: the poses are valid), `uint32 buttons` (bit 0 attack, 1 zoom, 2 reload, 3 melee, 4 offhand, 5 ability, 6 C in the Titan, 7 V in the Titan, 8 next weapon, 9 X; 0 unless pilot mode and in front), `float eye[3]`, `float ang[3]`, `float feet[3]`, `float vel[3]` (units/s), `float fov_scale` (horizontal 4:3 field of view / 70, 0.6 to 2), `uint32 view_w, view_h` (render at this size), `uint32 reserved`. The anchor is your feet when pilot mode began; `feet` there equals your `floor_pt`.

**`tl_tf_state`:** `uint32 flags` (1 in a match, 2 in the Titan, 4 a Titan is parked, 8 embarking or disembarking), `uint32 pid` (your process; sc-offline moves its window only if it is `NorthstarLauncher.exe` or `Titanfall2.exe`), `float origin[3]`, `float eye_z`, `float titan_origin[3]`, `float titan_yaw`, `int32 clip` (-1 none), `uint32 shots` (a counter), `float damage`, `float titan_health` (0 to 1), `float floor_pt[3]` (where sc-offline's anchor is in the match), `float zoom_frac`, `float zoom_fov`, `char weapon[48]`. Coordinates beyond +-1,000,000 make the whole state invalid.

**`tl_frame`:** `uint64 handle[2]` (two D3D11 textures you share as legacy shared handles, so 32-bit values, with `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`, premultiplied alpha, alpha 0 shows Star Citizen, at most 8192 x 8192, a BGRA8/RGBA8 (sRGB or not), RGB10A2 or RGBA16F format), `uint32 tex_seq[2]` (bump after writing that texture; the larger is newer), `uint32 width, height, key_mode, presents`, `char status[96]` (shown in the tab). Keyed mutex: you write while holding key 0 and release with key 1; sc-offline acquires key 1 (without waiting) and releases key 0.

**sc-offline -> Titanfall** (ring types): 1 `START_MATCH` `{char map[64]; char mode[32];}` (once per link; `[a-z0-9_]` names, build your own command from them), 2 `CALL_TITAN` `{float drop[3]; float yaw;}`, 3 `EMBARK` (no payload), 4 `DISEMBARK`, 5 `PILOT_OFF` (release held buttons).
**Titanfall -> sc-offline:** 1 `LOG`, 1 to 200 bytes of text (no NUL needed), shown in `mod.log` and the tab.

### Voxel bridge: `Local\SCO_voxel_bridge.link`

`layout_id` `0x31425856` ("VXB1"), `layout_version` 1, 8 MiB. Coordinates are blocks: x east, y up, z south.

| Offset | What | Written by |
|---|---|---|
| `0x0100` | block `vx_sc_state` (64 bytes) | sc-offline, ~10 Hz |
| `0x0200` | block `vx_peer_state` (96 bytes) | voxel side |
| `0x1000` | ring, `SC_IPC_TO_PEER`, 1 MiB | sc-offline pushes |
| `0x101100` | ring, `SC_IPC_FROM_PEER`, 4 MiB | voxel side pushes |

**`vx_sc_state`:** `uint64 time_ms`, `uint32 flags` (1 spawned, 2 in front, 4 an area exists and the pose is in it, 8 paused: you're in another zone or over 2,000 blocks out, so don't follow), `uint32 epoch` (the building area), `double feet[3]`, `float yaw` (0 faces +z, 90 faces -x), `float pitch` (positive looking down), `float block_size` (metres), `uint32 view_w, view_h, reserved`.

**`vx_peer_state`:** `uint32 flags` (1 a world is loaded), `uint32 epoch` (the area you have applied), `double feet[3]` (informational), `char status[64]` (shown in the tab).

**sc-offline -> voxel** (ring types): 1 `AREA` `{uint32 epoch; uint32 reserved;}`, a new building area: forget what you sent and send every solid section again with this epoch. 2 `GROUND` `{int32 x0, z0; uint32 epoch; uint32 count;}` then `count` (at most 64) `{int32 x, z; float top; uint32 reserved;}`: Star Citizen's ground height (voxel y) in columns of the 8 x 8 region from (x0, z0). Columns without ground are left out.

**Voxel -> sc-offline:** 1 `SOLIDS` `{int32 sx, sy, sz; uint32 epoch; uint64 bits[64];}` (528 bytes), the solid blocks of the 16^3 section at (sx, sy, sz). Bit `i` (`bits[i / 64] >> (i % 64)`) is block x = `i & 15`, z = `(i >> 4) & 15`, y = `i >> 8` within it, and a section replaces what you sent for it before. Sections of another epoch, or outside x and z +-30,000 and y -64 to 320, are ignored. 2 `CLEAR` `{uint32 epoch; uint32 reserved;}` removes every crate of that epoch. 3 `LOG`, 1 to 200 bytes of text.

## Not included

These parts of the original bridges need game access that has no signature row or service yet, or a sco-core feature that does not exist, so they are left out:

- **Taking over the keyboard and mouse** while a bridge is active. The original patched the game's import of `GetRawInputData` / `GetRawInputBuffer`, a game hook with no row. Star Citizen still sees your keys in pilot mode, and the voxel game can't be controlled from Star Citizen. It would need an input-capture mode in a later `sco.ui` release.
- **Hiding your Star Citizen body** (and turning breathing off with it): vtable slots `0x360` / `0x370` with a byte check and no row.
- **Titanfall-style movement** (wallrunning, double jump, slide, Titan walking) that moved your Star Citizen character: it wrote your position through raw entity slots (`0x2B0`, `0x2C8`) and cast rays every frame. The match follows Star Citizen's own movement instead. The same goes for the voxel bridge's "follow the voxel player" mode.
- **Per-frame updates:** poses go out at the host tick's 10 Hz, and the other side interpolates. Per-frame updates wait for a `frame` event in sco-core.
- **Your camera** (third person, aiming pitch): sc-offline's camera read isn't a service, so poses use your body's facing and a fixed 1.62 m eye height.
- **Shots hitting Star Citizen NPCs** (TitanLink) and NPCs mirrored into the voxel game, with Minecraft hits removing them: these need the camera, ray casts against entities, and entity reads by raw slot.
- **Drawing the voxel world** (textured meshes, avatars, block hiding behind walls) in an overlay with Star Citizen's camera. Without it the crates are the voxel world's only picture in Star Citizen.
- **Light matching** (tinting Titanfall's picture with Star Citizen's light through desktop duplication), the Titan landing shake and the zoom-to-`cl_fov` handover.
- **Typing commands into the voxel game** (`/gamemode`, `/give`, `/summon` for zombies and horses). The voxel side can set itself up.
- **Several building areas** at once: there is one, moved with `voxel_bridge.anchor`.
