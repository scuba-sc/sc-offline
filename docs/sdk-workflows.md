# SDK-only inspector and owned NPC workflows

These two features have one implementation each under `plugins/`, with two entry
adapters. `src/workflow_adapters.cpp` is host-only: it supplies the built-in
registration tables and attaches the product's NPC tab. The external modules
use only their feature sources and the public SDK headers; neither the product
version, private menu/teleport globals nor `sco/plugins.h` are plugin inputs.
Feature metadata is versioned independently of the product. No service or base
ABI changes are required, and the sco-core dependency pin is unchanged.

The standard product build registers `npc` and `inspector` as built-ins. For a
separate **test build**, `-DSCO_EXTERNAL_WORKFLOWS=ON` omits those two registrations
so the same logic can be loaded as native plugins. It does not turn the host
shell into a plugin. Do not install native folders with those IDs alongside the
normal built-ins: the loader rejects duplicate IDs. The external modules and
manifests are built under `build/test-plugins/{npc,inspector}/`; they are test
inputs, not additions to the release package. On Windows the entries are
`npc.dll` and `inspector.dll`. Development platforms use their native module
suffix; the dynamic-loader tests take the actual built path.

## Read-only inspector

Run `inspector.read` through the host command registry. The reply and plugin log
show the current player actor/entity IDs, health current/maximum, zone-local
position and quaternion rotation. Each read re-queries public services and
retains no provider table or session ID. There is no mutation or required UI.

- Before the player spawns, IDs and dependent readings are unknown.
- Missing providers, capabilities, null methods or incomplete method extents
  are reported as unavailable, not as zero-valued readings.
- A health maximum of zero is **unknown**, per the game contract. It is not a
  zero-sized health pool and is never used as a denominator.
- Failed calls, non-finite values, zero zones and invalid rotations are not
  promoted to valid data. These are observations, not new engine fields.
- Unload clears the borrowed API/owner; the host withdraws owner-scoped commands.
  No event subscription or asynchronous callback context is allocated here.

The restricted Lua counterpart is `plugins/inspector/inspector.lua`. Install it
in `data/plugins/inspector_lua/` with `lua-plugin.ini` renamed to `plugin.ini`.
It registers `inspector_lua.read` using only the existing `sco.game.actors` and
`sco.game.entities` read routes. An absent optional game adapter is handled;
Lua mutation and a spatial binding are not added. The standalone Lua test uses
synthetic sandbox functions, not the bundled sco-lua runtime.

## Owned NPC lifecycle

All commands run on the game thread, through the host command registry:

| Command | Result |
| --- | --- |
| `npc.spawn <archetype> <count>` | 1 to 10 NPCs near a fresh player pose; returned IDs are logged immediately as pending streaming |
| `npc.observe` | Read-only counts of pending, visible, removing and unknown, plus current transforms of visible owned IDs in the log |
| `npc.remove <id>` | Request removal of one ID still owned by this plugin |
| `npc.clear` | Request removal of all NPCs still owned by this plugin; no crew-wide or foreign-entity removal |

Supply an archetype appropriate to the actual game build at invocation time.
Public tests use `synthetic_actor`, an artificial fixture, not a game archetype.
The feature uses `game.actors` for both spawn and removal; it never removes an
actors-owned NPC through `game.entities.despawn`. Pose data must be finite, have
a valid nonzero quaternion and name a nonzero streamed zone. Position/zone data
are used immediately, not retained across commands. Partial batches report the
number already spawned and return the failing result; those IDs remain owned
and are cleaned up normally.

`SCO_OK` from removal means **removal started**, not that the NPC disappeared.
Removal can be requested before the NPC streams in. The feature relinquishes
local ownership after the provider accepts removal, and does not remove it
twice. `alive=0` is ambiguous (not streamed, unavailable or wrong thread), so it
never certifies disappearance or recycles a removal ticket. At most
`SC_ACTORS_MAX_NPCS` session tickets are retained during one feature load;
accepted-removal tickets still count against that conservative local bound
until unload. This is distinct from the provider's global live-NPC quota.
Foreign IDs are rejected without making a mutation call. Failed removal retains
ownership for retry; a provider `NOT_FOUND` relinquishes the local claim.

Unload requests removal of remaining owned IDs once, then forgets local state.
If a capability/provider is unavailable, the log says that host owner-release
cleanup is still required. Repeated unload is a no-op. The host's crash/unload
cleanup remains authoritative; this feature does not promise provider hot
reload, arbitrary retained-table safety or survival after process termination.
Seat workflows and the separate NPC-seat-removal regression are not covered.

## Targeted development proof

Package the SDK from the **pinned** dependency, then extract the resulting
`sco-sdk-*.zip` into an empty directory. The `legacy` profile contains the core
and game public headers needed here; an approved core+game installation can also
be used. Do not point the SDK-only proof at the host source include tree.

```sh
python3 external/sco-core/sdk/package.py --profile legacy --out build/sdk
# Extract the resulting zip; SDK_INCLUDE below is its include/ directory.
cmake -S plugins -B build/workflows \
  -DSCO_SDK_INCLUDE_DIR="$SDK_INCLUDE" \
  -DSCO_WORKFLOW_TESTS=ON \
  -DSCO_HOST_INCLUDE_DIR="$PWD/external/sco-core/include"
cmake --build build/workflows --target \
  npc-workflow-test inspector-test npc-module-test inspector-module-test \
  npc-builtin-test inspector-builtin-test
ctest --test-dir build/workflows \
  -R '^(npc|inspector)(-builtin|-module)?-workflow$' --output-on-failure
python3 tools/test_workflow_boundary.py --sdk-include "$SDK_INCLUDE"
lua plugins/inspector/inspector_test.lua
```

The host header path is given **only** to the optional built-in adapter tests;
shared feature libraries and external modules compile against the extracted
SDK. Leave it unset to build only SDK-only targets. On macOS/Linux,
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` enables the targeted sanitizer
run. The boundary probe copies just the feature sources into an isolated root,
builds both modules, inspects four compiler dependency traces and requires three
injected private-header/symbol controls to fail. It uses Unix Makefiles and a
C++ compiler, not MSVC. Strict module links reject unresolved host symbols on
both Mach-O and ELF; Windows links enforce this normally.

The native tests exercise built-in forwarding, exported entrypoints and actual
dynamic module loading against synthetic public providers, including truncated
and null methods, absent capabilities, no player, unknown health maximum,
delayed visibility, foreign IDs, pre-stream removal, invalid pose, partial
batches, bounded capacity, high-bit IDs, failed registration and repeated unload.
These are host-independent conformance checks, **not** a full product build or
sco-core real-host provider integration.

**Not run in game.** Windows/MSVC product and external-mode builds remain
required in CI. Exact-game-build acceptance must still record the chosen
archetype/build and inspect player fields, owned spawn/visibility/removal and
unload cleanup in each mode. No game support certification, seat regression
closure, tag or release follows from the synthetic checks.
