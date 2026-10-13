# Vehicles, Crew and NPC pickers

These panels belong to the external `spawn`, `crew` and `npc` modules. They use
public `sco.widgets`, `sco.content` and game services only. The neutral host shell
renders copied panels with its own ImGui adapter on the game-thread frame;
plugins never include renderer headers or invoke private menu/spawner helpers.
Missing widgets leave commands available. Missing content leaves manual class
input available. Missing game providers refuse actions, without a fallback.

## Catalogs and existing user lists

The supporting `picker_catalog` data pack has no native entry and is not another
baseline feature. Packaging installs copies of the existing `data/ships.txt` and
`data/npcs.txt` as:

```
data/plugins/picker_catalog/plugin.ini
data/plugins/picker_catalog/lists/ships.txt
data/plugins/picker_catalog/lists/npcs.txt
```

The original files remain unchanged for other callers and existing user edits.
For an existing installation, copy your edited base lists into this pack's
`lists/` directory while the game is closed; keep backups. There is no implicit
base-file import or private filesystem read. Catalog edits take effect at the
next host startup. Other enabled packs may supply the same `lists/` names.
Disabling this data folder does not disable the three native features; their
manual fields and commands still work. `plugins = off` removes external features
and data packs, with no hidden picker or gameplay fallback.

Catalog parsing skips blank/comment lines and invalid/oversized identifiers.
Ship classes are bounded to 63 bytes, NPC archetypes to 127 bytes. The shared
parser preserves discovery order, row order and duplicates with pack labels.
Filtering is case-insensitive and requires every space-separated word; lists
have 64-row pages and a visible match/page count. The total catalog limit is
8192 accepted rows per feature; reaching it is reported. The public service's
file/pack limits still apply. This is copied catalog order, not the old private
size-ranked ship list; no public ship-size contract is invented.

## Actions and ownership

- **Vehicles:** select or type a class, height 0–10000 m and seat (`pilot`, words
  from a seat name or `none`). Flight Ready requires pilot boarding. Select an
  owned session ship to board later or cancel/remove it; Clear affects only
  spawn-owned ships. Existing bounded boarding/power jobs are unchanged. Infinite
  ship ammo invokes `creative.ship_ammo` and reverts with a message if refused.
  Destination startup remains visibly unavailable under the G2 policy.
- **Crew:** target the ship currently aboard, refresh copied seats, and select a
  seat or enter name words. Seat tables use 32-row pages within the existing
  128-seat copied batch. A provider's `more` flag is shown, never treated as a
  complete list. Sit uses the same local-player job, while selected-seat actions
  use a fresh copied index. Add spawns one crew-owned NPC into the selected usable
  empty seat; Fill processes empty usable seats. Stand moves only the player or
  crew-owned occupants; Remove/Clear never delete player, foreign or NPC-plugin
  actors. Pending reservations and existing retry/cleanup quotas remain in force.
- **NPCs:** shared archetype catalog, independent selection and ownership; count
  1–10, Spawn near the current pose, Observe, selected-ticket Remove and Clear.
  Owned tickets are paged, and removal started is never disappearance. This
  panel does not add the historical ground ray or crew-wide removal authority.

**M**, **F6**, **F7/F8**, **F9/Ctrl+F9**, and the existing SQ42 command shortcuts
are not rebound by these pickers. SQ42 ordinary ships still invoke
`spawn.ship_options`; enemy-wing orchestration remains deferred. No NPC hotkey
is invented. Player/SQ42/Travel/Build/bridge policies belong to their modules.

## Build and evidence boundary

The published pin remains `sdk-v2.0.0`; it lacks the widgets/content headers.
These sources require an extracted, checksum-verified newer integration SDK for
local development until publication and pin promotion are approved. Do not add
the host checkout to a plugin's include paths.

One targeted fixture loads the actual three modules against synthetic public
providers (not engine state):

```sh
cmake -S plugins -B picker-build -DSCO_SDK_INCLUDE_DIR=<extracted-sdk>/include \
  -DSCO_PICKER_TEST=ON -DSCO_WORKFLOW_TESTS=OFF
cmake --build picker-build --target picker-module-test
ctest --test-dir picker-build -R '^picker-module-workflow$' --output-on-failure
cmake --install picker-build --prefix stage --component picker_catalog
```

The fixture checks catalog/filter/page events, duplicate pack identity, owned
selected-seat actions, foreign refusal, missing widgets/content/game services,
creative refusal/revert, destination refusal and panel removal/reload. The host
ImGui adapter also compiles against the product's renderer declarations locally.
**Windows/MSVC host build, actual rendering/DPI, game session, real streaming and
provider ownership cleanup remain NOT VERIFIED.** Release staging still needs an
approved matching SDK; no publication or gameplay certification is implied.
