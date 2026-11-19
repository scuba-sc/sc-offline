# Build: place props and buildings

Build places a prop, a building piece or an NPC in front of you and removes what you placed. You pick from a list on the **Build** page of the in-game menu.

## Build page

Open **Build** in the **Build** section of the menu (key **M**). The sections follow the order you use them:

- **Choose an item**: a category, a search across all categories, a list in pages of 64, and a Squadron 42 shortcut. Names are cleaned for reading. Changing the category, search or page clears the highlight but not the item chosen for aiming.
- **Adjust placement**: how far in front of you to place it, 0 to 100 m. When a Squadron 42 item is selected, its in-front and distance settings appear too.
- **Place**.
- **Things you placed**: **Undo last** and **Remove all**.
- **Status**.

Aiming (**Start aiming**, **Stop aiming**, **Aim with this item**) is not available in 0.8.0. It needs the capabilities `game.world.free_camera` and `game.world.raycast_filtered`, which Core ships off and the launcher has no switch for. The page says so, and building is Place only. **F6** runs `build.toggle`, which refuses for the same reason.

## Commands

| Command | What it does |
| --- | --- |
| `build.place <object> <ahead>` | Places a class name or a full `ObjectContainers/.../name.socpak` path 0 to 100 m ahead of you. |
| `build.undo` | Removes the last thing you placed. |
| `build.clear` | Removes everything you placed. |
| `build.status` | Counts what you placed and what is being removed. |
| `build.toggle`, `build.aim`, `build.camera <x> <y> <z>` | Aiming mode. They refuse while the two capabilities above are off. |
| `build.select <object> <reach>`, `build.reach <metres>`, `build.rotate`, `build.drop` | Aiming-mode controls. Same refusal. While aiming is active the left mouse button, R, `[`, `]` and Backspace work, only while the game window has focus. |

`Undo` and `clear` report that removal was requested, not that the object has disappeared. An NPC is removed through the actors service, never as a plain entity. The plugin keeps at most 128 placements and removal records per game session. Placements are removed when the plugin unloads, and nothing is saved across sessions.

The plugin also publishes a read-only service, `build.state`, that other plugins can read.

## Settings

The plugin has no settings of its own. It reads one list through `sco.content`: exactly one enabled data pack must supply `lists/buildables.txt`. The `build_catalog` pack ships it, copied from `data/buildables.txt`.

The format is `[category]` lines followed by raw object identifiers, with `#` comments and blank lines. Printable ASCII only. Limits: 1 MiB per file, 24 categories (23 bytes each), 4096 rows, 511 bytes per row, 1023 bytes per line. A missing, ambiguous, oversized or malformed list shows a message on the page and loads nothing. An empty list is valid.

To use your own list, with the game and launcher closed:

1. Create `data/plugins/my_build_catalog/lists/buildables.txt` from your file.
2. Create `data/plugins/my_build_catalog/plugin.ini` with `id = my_build_catalog`, `name = My Build catalog`, `version = 1.0.0`, `api = 1.1` and `kind = data`, one per line.
3. Run `sc-offline.exe plugins disable build_catalog`, or put an empty file named `disabled` in `data/plugins/build_catalog/`.
4. Start the game. Keep a copy of the original; two enabled lists refuse as ambiguous.

On start, the plugin selects the first usable row of the list so that Place works at once. An item you select yourself works without a list.

## What it needs

- Services: `game.prefabs`, `game.entities`, `game.actors`, `game.owned`, `game.world`, `teleport.spatial`, `sco.content` 1.0, `sco.widgets` 1.0 and `sco.ui` (the F6 key).
- Capabilities for placing: `game.entities.prefabs`, `game.entities.spawn`, `game.actors.spawn_npc` (NPCs only) and `game.world.raycast`. Aiming also needs `game.world.free_camera` and `game.world.raycast_filtered`.
- A missing service or capability makes the command refuse with a reason. There is no private fallback.

Seen in game (owner, build 4.10.200.58746): an Outposts item was placed. Not run in game. Undo, clear, NPC placement and aiming. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_prefabs.h`, `sc_owned.h`, `sc_world_control.h`, `sco_content.h` and `sco_widgets.h`.

```sh
cmake -S plugins/build -B build-build -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_BUILD_TEST=ON
cmake --build build-build --config Release --target sc-offline-build
ctest --test-dir build-build -C Release --output-on-failure
cmake --install build-build --config Release --prefix <stage> --component build
```

The tests are `build-picker` and `build-reset`. `python tools/check_build_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs them from a verified SDK archive, loads the real module with stand-in services and checks the install folder and compiler dependencies. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). `tools/test_build_core_integration.sh <core-source> <core-build> <build-module>` connects the module to Core's world-control and ownership code with stand-in providers.

Callbacks reopen each service and check the full table extent and null members before use. Only a successful panel publication installs its row mapping, so a callback from an older panel or an earlier load cannot place anything.
