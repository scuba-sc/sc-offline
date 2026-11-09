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
Filtering is ASCII case-insensitive and requires every space-separated word;
lists have 64-row pages and a visible match/page count. Non-ASCII UTF-8 alias
bytes display as authored but do not receive Unicode case folding.

Packs may optionally provide human-authored labels in `lists/ships.aliases.txt`
or `lists/npcs.aliases.txt`. Each non-comment row has exactly two tab-separated
fields: an exact class identifier, then a display label. For example, a pack
could contain the following row, where the separator is one literal tab:

```text
synthetic_shuttle	Synthetic Shuttle
```

That synthetic example does not assert a mapping for any real class. No alias
map ships by default. Aliases change only the picker label and search text. The
display label also includes the exact class identifier and source pack. The
picker retains the original source item/line for each row, including duplicates;
selection maps the displayed row back to its exact class identifier. The current
game service accepts only that identifier, so pack provenance is not part of the
spawn request. Without an alias, the existing `class [pack]` label and selection
behavior are unchanged. The loadout panel's existing source-attributed aliases
are a separate catalog and are not imported into these pickers.

Alias files are optional and bounded: labels are at most 59 UTF-8 bytes, lines
255 bytes, 8192 rows, 64 matching files, and 1 MiB aggregate across matching
files. Class identifiers use the same feature-specific 63/127-byte limits as
the base list. Labels must be valid UTF-8 without ASCII/C1 controls or
bidirectional formatting characters. Zero-width joiner/non-joiner characters
are preserved for scripts that use them. The composed label must also fit the
SDK's 255-byte widget text limit; if not, that row keeps its original label. A
duplicate identifier within one sidecar invalidates aliases; identical labels
from multiple packs are accepted, while conflicting labels mark that identifier
ambiguous and leave its original catalog labels in place. If enumeration,
reading, or parsing is incomplete or malformed, all optional aliases are ignored
while the base catalog remains available. These files are human-authored
presentation metadata, not authoritative game data. This is copied catalog
order, not the old private size-ranked ship list; no public ship-size contract
is invented.

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

The published stable SDK 2.4.0 release (`sdk-v2.4.0`) ships the widgets/content headers
(`sdk-v2.0.0` did not). Build these sources against the extracted,
checksum-verified `sco-sdk-2.4.0.zip`. Do not add
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

The fixture checks catalog/filter/page events, synthetic alias search and exact
identifier selection, duplicate pack identity, ambiguous aliases, malformed
data, incomplete-read fallback, owned selected-seat actions, foreign refusal,
missing widgets/content/game services, creative refusal/revert, destination
refusal and panel removal/reload. The host
ImGui adapter also compiles against the product's renderer declarations locally.
**Windows/MSVC host build, actual rendering/DPI, game session, real streaming and
provider ownership cleanup remain NOT VERIFIED.** Release staging still needs an
approved matching SDK; no publication or gameplay certification is implied.
