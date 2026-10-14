# Ordinary spawn — public SDK Vehicles picker

Current picker/copy-list installation and the one targeted module fixture:
[Public pickers](../../docs/pickers.md). **These sources use `sco_widgets.h` and
`sco_content.h`, which the pinned published `sdk-v2.2.0` ships** (`sdk-v2.0.0`
did not). The obtain/proof recipes below use that archive.

Trusted native code, **GPL-3.0-only**. SDK **2.2.0**, frozen plugin ABI **1.1**,
Star Citizen game pack **0.5.0**. This source builds independently of the host.
No private headers, engine memory, custom spawner, network or ImGui dependency.
The Vehicles panel uses public `sco.widgets`: class filtering/paging, manual
class, height, seat-name/pilot/none, Flight Ready, owned boarding/cancel/clear and
Infinite ship ammo through the external creative command (reverts on refusal).
Picker actions use the same command handlers/jobs, not legacy engine helpers.
Keep `data/ships.txt` as the existing caller-supplied class list;
`spawn.pack_list` additionally reads enabled `lists/ships.txt` through G14's
public `sco.content` service. This addition requires `sco_content.h`, which the
pinned published SDK 2.2.0 ships (SDK 2.0.0 predated it).
There is no private-index or filesystem fallback.

## Build, check, install (three stages)

1. Obtain published `sdk-v2.2.0` **`sco-sdk-2.2.0.zip`** and its release
   **`SHA256SUMS`**. The combined archive supplies core and game public headers.
   Do not point the plugin at `external/sco-core/include` or host `src/`.
2. Run the isolated proof from the product checkout, with a new work directory
   **outside** it:

   ```sh
   python3 tools/check_spawn_sdk.py --archive /path/to/sco-sdk-2.2.0.zip \
     --sums /path/to/SHA256SUMS --work /path/to/new-spawn-proof
   ```

   POSIX developers can append `--sanitizers` for the same dynamically loaded
   workflow under ASan/UBSan and a deliberately broken partial-pointer guard.
   The ordinary module is dependency-traced, installed and checked before this
   development-only instrumentation. The proof reuses the inspector's exact
   public-header inventory, compiler traces and checksum verification; it does
   not add a private-header exception. POSIX negative controls reject private
   host/product headers, unresolved host symbols and a transitive `SYSTEM` header.

   For a manual Windows build after verifying/extracting the SDK:

   ```powershell
   cmake -S plugins/spawn -B spawn-build -A x64 `
     -DSCO_SDK_INCLUDE_DIR=C:/verified-sdk/include -DSCO_WORKFLOW_TESTS=ON
   cmake --build spawn-build --config Release --target spawn-module-test
   ctest --test-dir spawn-build -C Release -R '^spawn-module-workflow$' --output-on-failure
   cmake --install spawn-build --config Release --prefix spawn-stage --component spawn
   ```

3. On Windows install `stage/data/plugins/spawn/{plugin.ini,spawn.dll}` into
   `data/plugins/spawn/`. The folder/manifest/export ID is `spawn`, independently
   versioned **1.0.0**, enabled without a `disabled` marker. Require `plugins = on`;
   the launcher may disable the folder. **Host integration must remove the old
   `spawn` built-in registration and ordinary-spawn tick fallback before installing
   this matching ID.** A missing/disabled plugin must not trigger the private path.
   POSIX `.so` development modules and rewritten checker manifests are not
   Windows release artifacts. Never copy them into a game installation.

## Commands

All commands and the owner-scoped `tick` subscription execute on the documented
SDK game-thread path. No workers or private thread handoff are created.

| Command | Typed arguments / behavior |
| --- | --- |
| `spawn.pack_list <ordinal:int>` | Copies one enabled `lists/ships.txt` startup snapshot; zero-based matching-pack ordinal. Discovery order and duplicate names are retained, not merged or silently overwritten. Empty lists succeed; binary lists, insufficient output capacity, missing/short/null service methods and provider failures refuse. Does not spawn a class; the public Vehicles picker consumes the same catalog snapshots. |
| `spawn.ship <class:string> <height:float>` | Existing ID, argument names/types/count preserved. Class length 1–63; finite height 0–10000 m along the player's current zone Z axis. Returns the final owned session ID before it streams in. No automatic boarding. |
| `spawn.ship_options <class:string> <height:float> <seat:string> <power:bool>` | `none` means no boarding; `pilot` selects the known usable pilot seat; another string requires **every word**, case-insensitively, in the copied seat name. Power is accepted only with `pilot`. Boarding providers/capabilities are preflighted before spawn. |
| `spawn.board <id:int> <seat:string> <power:bool>` | Choose a seat later on an ID this plugin still owns. Uses the same boarding workflow; refuses foreign/retrieved/crew IDs. `none` is not a board target. |
| `spawn.status` | Reports workflow pending/ready/boarding/removing counts. Ready means workflow completed, not guaranteed ongoing visibility. Missing observation provider is unavailable, never absence. |
| `spawn.cancel <id:int>` | Stops seat/power retries immediately, then requests owned removal, even before streaming. Cleanup refusal retains a cancelling ticket for retry. |
| `spawn.remove <id:int>` / `spawn.clear` | Removes only ordinary entities this plugin spawned, never NPCs or foreign vehicles. Accepted removal remains a bounded pending-removal ticket; disappearance is not certified. |
| `spawn.startup` | Explicitly unavailable: destination-specific startup requires **G2/G3** player relocation/full-pose discovery, which this plugin does not use. No Daymar/start-ship/private fallback. |

Example shape: `spawn.ship_options synthetic_ship 30 pilot true`. The fixture's
`synthetic_ship` is not a promised class in any game build; use a class from your
locally held list. Volatile IDs preserve all 64 bits in the signed integer wire
argument; do not persist them or volatile zone handles.

## Ownership and failure policy

**Intentional retained ownership:** ordinary ships are spawned by
`game.entities.spawn(self, ...)`, after `spawn.entities.class_exists`, using one
fresh `teleport.spatial.player_pose` in the same zone. They remain owned through
streaming absence and successful boarding, and are removed on cancel/clear/unload.
There is no `keep` call, arbitrary delete or owner transfer. This changes the old
unowned spawn's unload lifetime deliberately so cleanup is available.

`spawn.entities.spawn_as` does **not** grant `game.entities.despawn` ownership in
the pinned provider. This plugin therefore never spawns via `spawn_as` and then
cross-deletes its ID. Likewise `game.entities` ownership alone is not the pinned
vehicle provider's `spawn_as` power authority: Flight Ready waits for confirmed
local-player occupancy of the pilot seat **and a fresh `player_ship == owned ID`**.
Power-only requests on a retrieved/foreign/unboarded vehicle are not offered.
Existing fleet registration and retrieved-vehicle movement authority remain the
host/game pack's responsibility, not a plugin registration or ownership shortcut.

One player boarding job at a time; **64 tickets per loaded session**, including
pending removal. A bounded copied batch of at most **64 seats** must be complete:
partial/oversized lists, unusable/unknown seats, unknown/foreign occupants and
missing player/provider state wait, never evict. Seating acceptance is not
completion; later ticks confirm occupancy. Seat/power mutations retry at most once
per 1000 ms, so an accepted asynchronous link is not reset every frame. Stream/seat/power work expires after
**60000 ms from its first game tick**, with wrap-safe 32-bit tick arithmetic.
Timeout stops the job and requests removal. Provider reopen failure/null/partial
callbacks do not authorize mutation. Provider cleanup refusal remains pending
until retry or host owner release; it is logged on unload.

Each callback checks the full `offsetof + sizeof(member)` extent, its non-null
pointer, service query result and required capability. Borrowed provider pointers
are reopened and validated; no table/zone/player actor is persisted between ticks.
Unload twice is inert. The real host must withdraw commands/subscriptions and
release outstanding entity ownership after unload. Native faults are
**restart-required**, not safely recoverable/reloadable corruption.

## Evidence boundary

`spawn-module-workflow` independently loads the real native module with synthetic
public providers. It covers delayed visibility and links, class refusal, owner
attribution/foreign rejection, pilot/named/choose-later boarding, dashboard retry,
timeout across tick wrap, cancellation during cleanup refusal, quotas,
missing/full-null/physically truncated tables, failed reopen, registration failures,
unload twice and close/reopen/reload. Host registration/release is a fixture
stand-in, **not** actual runtime guard/release-hook proof.

Published checker with absent providers must report unavailable and **0 failures**.
The staged folder must contain exactly its manifest and one enabled module.
Windows/MSVC exports/loading, real host fault containment/cleanup, renderer widgets,
several real vehicle classes, streaming/seat/power behavior and owner-run offline
Windows acceptance are **NOT VERIFIED / not run in game**. Do not claim this
standalone change by itself removes the legacy host spawner or closes G2/G3.
