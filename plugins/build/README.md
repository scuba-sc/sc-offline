# Owned build module (SDK 2.4.0 consumer)

Trusted native GPL-3.0-only feature, plugin ABI 1.1. This module requires public
G4/G9 headers/services, which the pinned published sdk-v2.4.0-rc.1 ships
(sdk-v2.0.0 did not).

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
1.0. It supplies categories, case-insensitive word search across all groups,
64-row pages, selection, reach, preview/start/stop, near-player placement,
Undo/Clear, and an SQ42 shortcut with in-front/ahead controls. Search labels
include their group. Changing category, search or page clears selection. Labels
are shortened only for display; commands receive the complete original value,
including duplicate rows and any pipe characters. Build lists do not use the
Loadout catalogue's pipe-field format.

The picker reads exactly one enabled public `lists/buildables.txt` List using
`sco.content` 1.0. To use a legally held, user-owned list, explicitly copy it to
`data/plugins/<your-pack>/lists/buildables.txt` while the host is stopped and
provide an ordinary `kind = data` manifest. Restart after edits or enabling a
pack. The original `data/buildables.txt` remains unchanged; this consumer neither
imports it automatically nor stages a new pack in release folders.

The format is `[category]` followed by raw object identifiers, with `#` comments
and blank lines. Copied bytes are bounded to 1 MiB and parsed atomically: at most
24 distinct categories (23 bytes each), 4096 rows, 511 bytes per row and 1023
bytes per input line. Identifiers and categories use printable ASCII. Missing,
ambiguous, oversized or malformed lists produce an explicit panel/status
message and no partial catalogue. An empty list is valid. Search is limited to
63 printable ASCII bytes. Row ordering and duplicate identifiers are retained.

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
prefab cleanup, collision, filtered rays, camera restore, rendering and default
release staging remain unverified. The release workflow downloads
sdk-v2.4.0-rc.1, which ships these headers, but does not stage this module:
release-folder wiring is required before shipping. This change enables no native
backend or default and changes no SDK pin.
