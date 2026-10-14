# Owned build module (SDK 2.2.0 consumer)

Trusted native GPL-3.0-only feature, plugin ABI 1.1. This module requires public
G4/G9 headers/services, which the pinned published sdk-v2.2.0 ships
(sdk-v2.0.0 did not).

## Build and install

1. Extract an SDK containing `sc_prefabs.h`, `sc_owned.h`, `sc_world_control.h` and
   ordinary entities/actors/world/spatial services (with its `SHA256SUMS`).
2. Configure `cmake -S plugins/build -B build-owned -DSCO_SDK_INCLUDE_DIR=<SDK>/include`
   and build `cmake --build build-owned --config Release --target sc-offline-build`.
3. Install `cmake --install build-owned --config Release --component build --prefix <stage>`;
   copy `data/plugins/build` to the offline host's data/plugins root and explicitly
   enable plugin loading. Windows requires the MSVC x64 DLL, not a development `.so`.

No build built-in or host feature tick fallback remains. The retained host Build
and SQ42 grouped pickers only forward public commands (the Build tab is temporarily
rendered under `loadout.build`, using that existing host UI owner). Its legacy
buildables.txt categories/strings are preserved; the actual provider validates names.
The UI observation service `build.state` is plugin-owned, read-only, game-thread-only;
consumers reopen it per use and guard complete callback extents/nullness.
Moving this remaining host rendering/data facade to an external UI is still owed.

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

Local macOS module configure/compile/link and portable host command-facade object
compile only. Tests/checker/archive boundary proofs/reviews **NOT RUN** by owner
speed order. Windows/MSVC/SEH, actual game, streamed prefab cleanup, collision,
filtered rays, camera restore, rendering and default release staging **NOT VERIFIED**.
The release workflow downloads sdk-v2.2.0, which ships these headers, but does not
stage this module: release-folder wiring is required before shipping.
