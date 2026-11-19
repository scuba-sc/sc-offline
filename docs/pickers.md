# Vehicles, Crew and NPC pickers

The Vehicles, Crew and NPCs pages belong to the `spawn`, `crew` and `npc` plugins. They use only public `sco.widgets`, `sco.content` and game services. The menu shell draws the pages with its own ImGui adapter on the game-thread frame. A plugin never includes renderer headers or calls private menu or spawner code.

If widgets are missing, the commands still work. If content is missing, you can still type a class by hand. If a game provider is missing, the action is refused, with no fallback.

## Catalogs and your own lists

The `picker_catalog` data pack has no code of its own. Packaging installs copies of `data/ships.txt` and `data/npcs.txt` in it:

```
data/plugins/picker_catalog/plugin.ini
data/plugins/picker_catalog/lists/ships.txt
data/plugins/picker_catalog/lists/npcs.txt
```

The original files stay as they are. To use a list you edited, copy it into the pack's `lists/` folder while the game is closed, keep a backup, and restart. Nothing imports a base file on its own. Other enabled packs may supply the same `lists/` names. Disabling the pack's folder does not disable the three plugins: their manual fields and commands still work. `plugins = off` removes the plugins and the packs, with no hidden fallback.

**How lists are read.**

- Blank lines, comments and invalid or oversized identifiers are skipped. Ship classes are at most 63 bytes and NPC archetypes at most 127.
- Discovery order, row order and duplicates are kept, each labelled with its pack.
- Filtering ignores ASCII case and requires every space-separated word. Lists show 64 rows a page with the match and page count. Non-ASCII UTF-8 text displays as written but is not case-folded.

**Aliases.** A pack may add readable labels in `lists/ships.aliases.txt` or `lists/npcs.aliases.txt`. Each row has two tab-separated fields: an exact class identifier, then a label. For example, with a literal tab between the fields:

```text
synthetic_shuttle	Synthetic Shuttle
```

That example names no real class, and no alias file ships by default. An alias changes only the label and the search text. The label still shows the exact class identifier and the source pack, and choosing a row sends the exact identifier to the game.

Alias files are optional and bounded: labels up to 59 UTF-8 bytes, lines up to 255 bytes, 8192 rows, 64 matching files and 1 MiB in total. Labels must be valid UTF-8 without ASCII or C1 control characters or bidirectional formatting characters (zero-width joiners are kept). The composed label must fit the SDK's 255-byte widget text limit, or that row keeps its original label. A duplicate identifier within one file invalidates the aliases. The same label from several packs is accepted; conflicting labels mark the identifier ambiguous and leave its original labels. If reading or parsing is incomplete, all aliases are ignored and the base catalog still works. Aliases are presentation text, not game data. The loadout page's own aliases are a separate catalog.

The list order is the copied catalogue order, not the old ship list ranked by size.

## What each page does

- **Vehicles.** Select or type a class, a height of 0 to 10000 m and a seat (`pilot`, words from a seat name, or `none`). Flight Ready needs the pilot seat. Select a ship you spawned in this session to board it later, cancel it or remove it. Clear affects only ships this plugin spawned. Infinite ship ammo calls `creative.ship_ammo` and reverts with a message if the game refuses. Choosing a spawn destination is not available.
- **Crew.** Target the ship you are aboard, refresh the copied seats, and pick a seat or enter words from its name. Seats show in 32-row pages within the 128-seat copied batch. A provider's `more` flag is shown and never treated as a complete list. **Sit** moves you. **Add** puts one crew-owned NPC in the selected usable empty seat. **Fill** fills every empty usable seat. **Stand** moves only you or crew-owned occupants. **Remove** and **Clear** never delete you, foreign actors or NPCs of the NPC plugin.
- **NPCs.** A shared archetype catalogue with its own selection and ownership. Count 1 to 10, **Spawn** near your current position, **Observe**, **Remove** for a selected NPC and **Clear**. Owned NPCs are paged, and a started removal is not a disappearance. The page has no ground ray and no crew-wide removal.

**M**, **F6**, **F7/F8** and **F9/Ctrl+F9** are not rebound by these pages, and no NPC hotkey exists.

## Building and checking the pickers

Build the plugins against the extracted, checksum-verified `sco-sdk-2.4.0.zip` of the published SDK 2.4.0 (`sdk-v2.4.0`), which ships the widgets and content headers. Do not add the host checkout to a plugin's include paths. One fixture loads the three real modules against synthetic public providers:

```sh
cmake -S plugins -B picker-build -DSCO_SDK_INCLUDE_DIR=<extracted-sdk>/include \
  -DSCO_SDK_VERSION=<version> -DSCO_SDK_SHA256=<archive sha256> -DSCO_SDK_ARCHIVE=<sco-sdk-<version>.zip> \
  -DSCO_PICKER_TEST=ON -DSCO_WORKFLOW_TESTS=OFF
cmake --build picker-build --target picker-module-test
ctest --test-dir picker-build -R '^picker-module-workflow$' --output-on-failure
cmake --install picker-build --prefix stage --component picker_catalog
```

It checks catalogue, filter and page events, alias search and exact-identifier selection, duplicate packs, ambiguous aliases, malformed data, incomplete reads, owned seat actions, refusal of foreign actors, missing widgets, content and game services, creative refusal and revert, and page removal and reload. None of that is renderer or game evidence. Not run in game. Windows rendering, DPI, real streaming and provider cleanup are covered in [Status](status.md).
