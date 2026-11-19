# Gear presets: saved outfits

Gear presets saves a name for what you are wearing and compares it later with what you wear then. It only reads and reports: it never changes your gear.

The plugin is not in the 0.8.0 release package, and capture does not work in the product anyway. Capturing needs the capability `game.inventory.layout`, which Core ships off and the launcher has no switch for. The page then says "Couldn't read what you're wearing right now, so nothing was saved."

## Saved outfits page

Open **Saved outfits** in the **Character** section of the menu (key **M**). It has two sections and a list of differences:

- **Save what you're wearing**: an outfit name, **Check what I'm wearing** and **Save outfit**.
- **Saved outfits**: the list, **Compare with what I'm wearing**, **Delete outfit** and **Refresh list**.
- **What is different**: after a comparison, one plain sentence per slot.

Comparison works on categories, not on exact item classes or item instances. Storage ports are kept as context and never produce gear actions. An incomplete reading cannot be saved and is never treated as empty.

## Commands

| Command | What it does |
| --- | --- |
| `gear_presets.capture` | Reads your current layout. |
| `gear_presets.save <name>` | Saves the captured layout under a name. |
| `gear_presets.list` | Lists saved outfits with their ids. |
| `gear_presets.compare <id>` | Compares one saved outfit with what you wear now. |
| `gear_presets.delete <id>` | Deletes one saved outfit. Ids are not reused. If storage reports an error, refresh before retrying, because the result may be unknown. |

Presets are kept in the plugin's own storage through `sco.storage`.

## Settings

None.

## What it needs

- Services: `game.inventory` 1.2 (the layout reading) and `sco.storage`, plus `sco.widgets` 1.0 for the page.
- Capability: `game.inventory.layout`.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_inventory.h`, `sco_storage.h` and `scosdk/game/inventory_layout.hpp`. Without that header the plugin compiles to nothing and the configure step refuses an older SDK.

```sh
cmake -S plugins/gear_presets -B build-gear-presets -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_GEAR_PRESETS_TESTS=ON
cmake --build build-gear-presets --config Release
ctest --test-dir build-gear-presets -C Release --output-on-failure
```

The model test `gear-presets-model` runs on its own. The storage and workflow tests also need `-DSCO_CORE_HOST_TEST_ROOT=<core source>` and `-DSCO_CORE_HOST_BUILD_DIR=<core build>` from the same Core source as the SDK. They link a stand-in host and ship no backend. The project's CI runs all three.
