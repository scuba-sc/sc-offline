# Voxel bridge: link to a local voxel game (optional)

The voxel bridge links the game to a voxel-building game running on the same PC. Blocks placed there appear in Star Citizen as crates around an anchor point you set, and the ground around the anchor is sent back.

This plugin is off by default and is not in the 0.8.0 release package. The folder `data/plugins/voxel_bridge/` holds an empty `disabled` file, so it does not load until you remove that file. Native plugins run with the game's rights and are not sandboxed.

## Voxel link page

When the plugin is enabled, the page **Voxel link** appears under **More plugins** in the menu (key **M**). It has **Open the bridge** or **Close the bridge** (**Ctrl+F9**) and **Move the area here**, and shows the peer's world and age, status, area and feet position, counts, block settings and refusal text. Without a complete widgets service the page says it is unavailable and the commands still work.

## Commands

| Command | What it does |
| --- | --- |
| `voxel_bridge.toggle` (**Ctrl+F9**) | Opens the local link channel, or closes it and starts removing the crates. |
| `voxel_bridge.anchor` | Sets the building area at your feet and facing, removes the old crates and sends the area to the other game. |
| `voxel_bridge.status` | Reports the link, the area, the counts, pending removals and any refusal. |

Block (0, 64, 0) sits at your feet at the anchor, with x to the right, y up and -z forward. Up to eight crates are spawned per tick for solid blocks, and at most 4096 blocks are kept, pending removals included. Core's own limit on owned entities (1024 now) can refuse earlier, and refusals are counted. One 8x8 ground region is scanned per tick with `game.world.raycast`, and the cache refreshes every three seconds. The link counts the other side as alive up to 7999 ms after its last heartbeat, and 8000 ms closes it and cleans up the crates. A malformed or wrong-sized record is skipped, and a damaged ring or state closes the channel. The other game can request bounded crate placement and never command execution or moving you.

Crates are owned by this plugin and removed through the entities service. A removal that was accepted may not be visible yet. Refused removals are retried, and a new link refuses while any remain. Unloading stops the tick and the key, closes the channel and asks for cleanup.

Not available: following the voxel player or moving you, input takeover, hiding your body, per-frame callbacks, NPC mirroring and hits, textured voxel overlay, light matching, console commands from the other game, and several areas at once. Focus and view size are sent as unknown.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `block` | `CargoBox_050x050x050_Metal` | Crate class. `none` turns crates off. |
| `size` | 0.5 | Metres per block, 0.1 to 4. |
| `pivot` | 0 | Crate origin inside the block, -1 to 1. |
| `radius` | 2 | Ground scan radius in 8x8 regions, 1 to 4. |

The old `data/voxel_bridge.txt` is not imported: copy its four values into the settings (`sc-offline.exe plugins settings voxel_bridge ...`) and keep the old file. Without `sco.settings` the defaults apply.

## What it needs

- Services: `sco.ipc` 1.0, `game.entities` 1.0, `game.world`, `game.spatial` 1.0 (full-pose conversion), `teleport.spatial` 1.0, `sco.settings`, `sco.widgets` 1.0, `sco.ui` (Ctrl+F9).
- Without the full-pose conversion, the cross-zone observation pauses and says so.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build in its own tree; it refuses to be added to a host build. Use an extracted SDK 2.4.1 `include/` directory that holds `sc_game_spatial.h` and `sco_ipc.h`. The wire header `sc_voxel_bridge.h` (`VXB1`, version 1, 8 MiB) is MIT-licensed SDK code, and this plugin is GPL-3.0-only. See [docs/bridges.md](../../docs/bridges.md) for the wire and the peer fixture.

```sh
cmake -S plugins/voxel_bridge -B build-voxel -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-voxel --config Release --target sc-offline-voxel_bridge voxel-bridge-module-test
ctest --test-dir build-voxel -C Release --output-on-failure
cmake --install build-voxel --config Release --prefix <stage> --component voxel_bridge
```

The install holds the module, `plugin.ini` and `disabled`. The test is `voxel-bridge-module-workflow`. `python tools/check_voxel_bridge_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive, checks the compiler dependencies and, on POSIX, the private-header negatives, and loads an enabled copy only inside the checker. Services are queried afresh on every use, and a native fault needs a restart.
