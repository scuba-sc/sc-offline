# Owned build module (SDK 2.4.0 consumer)

Trusted native GPL-3.0-only feature, plugin ABI 1.1. This module requires public
G4/G9 headers/services, which the published sdk-v2.4.0 ships.

## Build and install

1. Extract an SDK containing `sc_prefabs.h`, `sc_owned.h`, `sc_world_control.h` and
   ordinary entities/actors/world/spatial services (with its `SHA256SUMS`).
2. Configure `cmake -S plugins/build -B build-owned -DSCO_SDK_INCLUDE_DIR=<SDK>/include`
   and build `cmake --build build-owned --config Release --target sc-offline-build`.
3. Install `cmake --install build-owned --config Release --component build --prefix <stage>`;
   copy `data/plugins/build` to the offline host's data/plugins root and explicitly
   enable plugin loading. Windows requires the MSVC x64 DLL, not a development `.so`.

No build built-in or host feature tick fallback remains. The external Build module
owns the public build commands and the read-only, game-thread-only `build.state`
observation service. Consumers reopen it per use and guard complete callback
extents/nullness; the actual provider validates object names.

## Grouped Build and SQ42 picker

The same external owner publishes `build.picker` through public `sco.widgets`
1.0, written for players (see "Writing a page for players" in docs/plugin-ui.md).
Sections, in the order they are used: Choose an item (category, word search across
all categories, 64-item pages, Squadron 42 shortcut), Adjust placement (distance in
front of the player, plus the Squadron 42 in-front/distance settings when such an
item is selected), Place, Things you placed (Undo last, Remove all) and Status.
Names are cleaned for display only (prefix, `.socpak` and underscores removed;
categories capitalised); commands receive the complete original value, including
duplicate rows and any pipe characters. Panel messages are plain sentences; the
command output and `mod.log` keep the technical text. Changing category, search or
page clears the highlight, not the item chosen for aiming.

The aim controls (Start/Stop aiming, Aim with this item, and the "Aiming with" line)
are published only while the free-camera prerequisites are present, or while a
camera lease is still held so it can be stopped. Otherwise the page says that
aiming is not available yet and building is Place only. F6 still runs `build.toggle`.
Build lists do not use the Loadout catalogue's pipe-field format.

The picker reads exactly one enabled public `lists/buildables.txt` List using
`sco.content` 1.0. Release staging and the CMake `build_catalog` component provide the
default in `data/plugins/build_catalog/`. An existing top-level
`data/buildables.txt` is preserved and is not imported automatically.

To use your edited list, stop the host and launcher, create a separate
`data/plugins/my_build_catalog/lists/`, and copy the legacy file to
`lists/buildables.txt` there. Create `data/plugins/my_build_catalog/plugin.ini`
with `id = my_build_catalog`, `name = My Build catalog`, `version = 1.0.0`,
`api = 1.1`, and `kind = data` on separate lines. Create an empty `disabled`
file in `data/plugins/build_catalog/` to disable the bundled pack, and restart.
Keep the original as a backup and do not overwrite the bundled pack. Multiple
enabled lists refuse as ambiguous; there is no raw-file fallback.

The format is `[category]` followed by raw object identifiers, with `#` comments
and blank lines. Copied bytes are bounded to 1 MiB and parsed atomically: at most
24 distinct categories (23 bytes each), 4096 rows, 511 bytes per row and 1023
bytes per input line. Identifiers and categories use printable ASCII. Missing,
ambiguous, oversized or malformed lists produce an explicit panel/status
message and no partial catalogue. An empty list is valid. Search is limited to
63 printable ASCII bytes. Row ordering and duplicate identifiers are retained.

On start, absent a successful explicit `build.select`, the module copies the first
row admissible under the same selection rules from the loaded unique catalogue.
Missing, empty, ambiguous, malformed, or all-inadmissible catalogues refuse before
camera acquisition. An explicit successful selection and reach work without a
catalogue. Stop remains available for an active or retained camera lease. A first
NPC is not skipped; NPC preview may still refuse under the public ownership rules.

Only successful panel replacement installs its row mapping and callback cookie.
Failed publication pauses actions until a successful retry; callbacks from an
older publication, unload or reload cannot place a new selection. Unload removes
this owner's panel. Public service callbacks are reopened and checked for full
member extent and nullness rather than treated as a permanent provider lease.
The retained host/legacy picker sources are not rebound by this module.

## Commands and authority

- `build.place object ahead`: supplied class or exact canonical
  `ObjectContainers/.../name.socpak`, grounded 0..100 m ahead of the player. No
  private spawner fallback. Missing/disabled `game.world` refuses safely.
- `build.undo`, `build.clear`: G9 owner-scoped retained removal. NPCs use actors
  ownership and G9 remove_npc, never entities deletion. Receipts mean REQUESTED,
  not disappearance; no `alive=0` pruning or completion claim.
- `build.toggle`, `build.aim`, `build.camera x y z`: G5 only. Both production
  capabilities are OFF: no engine adapter, so aim/freecam refuse without private
  rows, camera fields, skip proxies or guessed controls. Relative camera steps
  are bounded by the public 10 m contract when a future adapter is enabled.
- `build.select object reach`, `build.reach metres`, `build.rotate`, `build.drop`:
  public preview policy for a future enabled G5 adapter. Windows left click/R/[ ]/
  Backspace retain OS-input command policy while active and focused. Filter IDs include only
  this plugin's entities-owned marker/ghost, not player/NPC/foreign IDs. Prefabs
  use a marker and a 500 ms stillness-delayed ghost; a ghost transfers to retained
  placement without legacy keep. NPC preview refuses because actors ownership
  is not entities skip authority. Use ordinary build.place for NPC placement.
- `build.status`: retained/removal-requested counts and disabled G5 state.

G4 spawn success is initiation, never confirmation of streamed materialization
or collision. Placements retain removal authority only while this plugin is
loaded; ordinary unload removes preview, pending and retained placements through
public providers/host owner release. **No across-reload persistence.** Receipt and
pending slots count against a bounded loaded-session quota (128), not recycled
from streaming absence. Legacy `entities.keep` is never called.

Unrelated legacy NPC/contracts/optional bridge placement helpers remain in the
host until those callers have public replacements; the build module neither
includes, links nor falls back to them. Existing private spawner prefab helpers
used by other features are not deleted by this lane.

## Evidence

`tools/check_build_sdk.py` extracts and verifies the published SDK archive,
builds and loads this actual module with copied synthetic content/widgets,
checks public command routing and the existing missing-world/G5 refusals, runs
the picker fixture with ASan/UBSan on supported POSIX hosts, checks installation
and compiler dependencies, and retains four private-dependency negative controls.
A missing-publication red arm must fail the same fixture. These are source and
synthetic integration checks, **not run in game**.

Windows/MSVC validation is recorded separately by the source review. Streamed
prefab cleanup, collision, filtered rays, camera restore, and rendering remain
unverified. The release workflow uses the `player-offline` profile, which includes
the Build module, and downloads sdk-v2.4.0, which ships its required headers. The
development release workflow remains refused; no release artifact or activation
is established by this staging configuration. This change enables no native
backend and changes no SDK pin. The existing Windows input focus issue remains
open; process focus alone does not establish that the Build controls are safe
while interacting with other UI.

For a connected no-game preflight, build the actual module against an extracted
checksum-verified SDK and build the matching Core source's `sco_host`,
`sco_runtime` and `sco_core` libraries. On POSIX,
`tools/test_build_core_integration.sh <core-source> <core-build> <build-module>`
connects the module to Core's `WorldControl` and `Owned` policy through synthetic
entity, ray and camera providers. It checks exclusive leases, foreign ownership,
preview cleanup refusal, provider withdrawal and owner release. Synthetic entity
removal is fixture behavior; this does not prove native cleanup, rendering or game
acceptance. Keep the exact SDK archive, Core source and module identities in the
receipt.
