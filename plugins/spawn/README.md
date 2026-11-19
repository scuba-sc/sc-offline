# Spawn: ships and vehicles

Spawn puts a ship or ground vehicle next to you and can seat you in the pilot seat and start it. You pick from a list on the **Ships & vehicles** page of the in-game menu.

## Ships & vehicles page

Open **Ships & vehicles** in the section of the same name (key **M**). The sections follow the order you use them:

- **Choose a ship**: a search box, a paged list, **Reload ship list** and a field for a typed ship name.
- **Spawn it**: **Height above you**, **After spawning** (*Get in as pilot* or *Stay outside*), **Start ready to fly** and **Spawn ship**. **Start ready to fly** is greyed out unless you choose *Get in as pilot*.
- **Ships you spawned**: **Get in**, **Remove ship**, **Remove all my ships** and **Check status**.
- **Last result**.

There is no start-place button, because `spawn.startup` can only refuse. The page has no hotkey and no creative controls; infinite ship ammo is on the Creative page.

The list comes from `lists/ships.txt` in the `picker_catalog` data pack (a copy of `data/ships.txt`), at most 63 bytes per class. To use your own list, copy it over that pack's `lists/ships.txt` with the game closed and keep a backup. A change needs a game restart. Optional `lists/ships.aliases.txt` files give readable labels. See [docs/pickers.md](../../docs/pickers.md).

An ASOP page, **Your own ships**, appears under Ships & vehicles only when Core's capability `game.asop.selection.file` is on. The 0.8.0 product never composes that provider, so the page is left out.

## Commands

| Command | What it does |
| --- | --- |
| `spawn.ship <class> <height>` | Spawns a ship `height` metres (0 to 10000) above you along your zone's up axis and returns its id before it streams in. It does not board. Class is 1 to 63 characters. |
| `spawn.ship_options <class> <height> <seat> <power>` | Spawns, then seats you. `none` means no boarding, `pilot` the pilot seat, and any other text must appear, every word, in the seat name. `power` works only with `pilot`. |
| `spawn.board <id> <seat> <power>` | Seats you in a ship this plugin spawned. Refuses ships it did not spawn. |
| `spawn.status` | Counts ships pending, ready, boarding and being removed. |
| `spawn.cancel <id>` | Stops boarding and requests removal, even before the ship streams in. |
| `spawn.remove <id>`, `spawn.clear` | Remove one ship, or all ships, this plugin spawned. Never NPCs or ships it did not spawn. |
| `spawn.pack_list <ordinal>` | Reads one `lists/ships.txt` from a data pack, by zero-based pack number. |
| `spawn.startup` | Always unavailable. |

The **Spawn ship** button runs `spawn.ship_options` with your choices; *Get in as pilot* is sent as `pilot`, *Stay outside* as `none`. The old `start_ship` and `start` keys in `sc-offline.ini` do not select a spawn.

Ships stay owned by this plugin until you remove them, cancel, or the plugin unloads. They are removed on unload. A removal that was accepted may not be visible yet. Flight Ready waits until you sit in the pilot seat. One boarding job runs at a time, and the plugin keeps at most 64 records per session, removals included. A seat or power request retries at most once per second. Automatic boarding gives up 60 s after its first game tick. A ship that already streamed in is then kept, because you may be inside it, and a ship that never streamed in is removed. Seats are read as a complete batch of at most 64. An incomplete list, an unknown seat or an unknown occupant waits and never evicts anyone.

## Settings

None.

## What it needs

- Services: `game.entities` 1.0, `spawn.entities`, `game.vehicles` 1.0, `game.actors` 1.0, `teleport.spatial` 1.0, `sco.content` 1.0, `sco.widgets` 1.0.
- Capabilities: `spawn.ship`, `game.entities.spawn`, `game.entities.transform`, `game.actors.local_player`, `game.vehicles.seats`, `game.vehicles.seat`, `game.vehicles.flight_ready`.
- A missing service or capability makes the action refuse with a reason. Boarding services are checked before the ship is spawned.

Run in game (maintainer, build 4.10.200.58746): a ship spawned, the maintainer boarded it and Flight Ready worked. Not run in game. Removal, several ship classes and the ASOP page. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sco_widgets.h` and `sco_content.h`.

```sh
cmake -S plugins/spawn -B build-spawn -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-spawn --config Release --target sc-offline-spawn spawn-module-test
ctest --test-dir build-spawn -C Release --output-on-failure
cmake --install build-spawn --config Release --prefix <stage> --component spawn
```

The test is `spawn-module-workflow`; `asop-selection-workflow` runs too when the SDK has `sc_asop_selection.h`. `python tools/check_spawn_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs them from a verified SDK archive. Add `--sanitizers` on POSIX, and `--candidate` with the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). On Windows pass `-A x64` to the configure step.

Ships are spawned with `game.entities.spawn` after `spawn.entities.class_exists`, from one fresh `teleport.spatial` player pose. The plugin never uses `spawn_as`: that call does not give it the right to despawn, and it does not give the power authority Flight Ready needs. Flight Ready therefore waits for you to sit in the pilot seat and for `player_ship` to equal the plugin's ship id. Ids are 64-bit and must not be saved. Each callback checks the full table extent and null members, and services are reopened on every tick. Native faults need a restart.
