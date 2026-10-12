# SDK-only inspector and owned NPC workflows

These features have SDK-only implementations under `plugins/`. Inspector now
ships as an external native plugin only. NPC remains built in for this step;
`src/workflow_adapters.cpp` is host-only and supplies NPC's registration and tab.
The external modules
use only their feature sources and the public SDK headers; neither the product
version, private menu/teleport globals nor `sco/plugins.h` are plugin inputs.
Feature metadata is versioned independently of the product. No service or base
ABI changes are required. The host dependency is published `sdk-v2.0.0` at
`cd9a686bda4184f1befbdf0b74b503a1565540dd` (SDK 2.0.0, plugin ABI 1.1,
Star Citizen game pack 0.3.0).

Step **1B removes only inspector's production built-in registration**. The
release installs `data/plugins/inspector/plugin.ini` and `inspector.dll`, enabled
without a `disabled` marker. `plugins = off` or disabling this folder leaves no
inspector command or fallback. Ten built-ins remain, including NPC. An external
NPC module is still a test input; never install its folder alongside the normal
NPC built-in. This does not turn the host shell into a plugin.

Standalone targets stage modules/manifests under
`<plugin-build>/test-plugins/{npc,inspector}/`. Windows produces `npc.dll` and
`inspector.dll`; macOS/Linux development builds use their native module suffix
and are **not Windows game-loadable artifacts**. The dynamic-loader tests take
the actual built path. Release packaging copies only the native inspector
variant; Lua and the NPC test module are not additional release features.
Owner policies and future boundaries: [decisions.md](decisions.md).

## Worked tutorial: standalone native inspector

### Layout and manifest

Copyable source is `plugins/inspector/`: `inspector.cpp`/`inspector.h` implement
reads, `external.cpp` provides the three native exports, and `plugin.ini`
declares `id = inspector`, `version = 1.0.0`, `api = 1.1`, `kind = native`,
`entry = inspector.dll`. There are no required game capabilities in the manifest:
loading without providers is deliberate, so the read command can explain missing
capabilities. No private host adapter, product version or ImGui headers are needed.
`inspector.lua`, `lua-plugin.ini` and `inspector_test.lua` are the optional Lua
alternative and its synthetic test, not files required by the native install.

The tutorial needs CMake 3.20+, Python 3 and a C/C++20 compiler. Use Windows x64
MSVC for the game DLL; macOS/Linux builds are development/checker artifacts only.
Obtain sc-offline source containing this step separately and set `SOURCE` below
to that checkout. Plugin build output lives outside the checkout; the host still
uses its tagged submodule independently.

### Stage 1 — Obtain and verify the published SDK

Download **`sco-sdk-2.0.0.zip`** (the combined legacy core+Star Citizen SDK) and
**`SHA256SUMS`** from the immutable
[`sdk-v2.0.0` release](https://github.com/scubamount/sco-core/releases/tag/sdk-v2.0.0).
Use an empty work directory. In Developer PowerShell for Visual Studio:

```powershell
$Work = Join-Path $PWD "inspector-dev"
New-Item -ItemType Directory -Path $Work -ErrorAction Stop | Out-Null
Set-Location $Work
$Release = "https://github.com/scubamount/sco-core/releases/download/sdk-v2.0.0"
Invoke-WebRequest "$Release/sco-sdk-2.0.0.zip" -OutFile "sco-sdk-2.0.0.zip"
Invoke-WebRequest "$Release/SHA256SUMS" -OutFile "SHA256SUMS"
python -c "import hashlib,pathlib; p=pathlib.Path('sco-sdk-2.0.0.zip'); rows=[l.split() for l in pathlib.Path('SHA256SUMS').read_text().splitlines() if l.strip()]; hashes=[r[0] for r in rows if r[1].lstrip('*')==p.name]; assert len(hashes)==1 and hashlib.sha256(p.read_bytes()).hexdigest()==hashes[0], 'SDK archive checksum mismatch'"
if ($LASTEXITCODE -ne 0) { throw "Do not extract an unverified SDK" }
python -m zipfile -e sco-sdk-2.0.0.zip sdk
$SDK = Join-Path $Work "sdk/sco-sdk-2.0.0"
```

Check the archive's `VERSIONS.txt` and `COMPATIBILITY.json`: SDK 2.0.0,
plugin ABI 1.1, game pack 0.3.0. The archive root is `sco-sdk-2.0.0/`, with
`include/`, `CMakeLists.txt` and `tools/sco-plugin-check.c`. Hash verification
identifies the downloaded artifact; it is not a gameplay or trust guarantee.
Do **not** replace `include/` with `external/sco-core/include` or locally
repackage the dependency to stand in for the published asset.

### Stage 2 — Build and check in isolation

Set the checkout path explicitly. The plugin and checker are independent builds:

```powershell
$SOURCE = "C:/src/sc-offline"
$PluginBuild = Join-Path $Work "plugin-build"
$CheckerBuild = Join-Path $Work "checker-build"
cmake -S "$SOURCE/plugins" -B "$PluginBuild" -A x64 -DSCO_SDK_INCLUDE_DIR="$SDK/include" -DSCO_WORKFLOW_TESTS=ON
cmake --build "$PluginBuild" --config Release --target sc-offline-inspector inspector-module-test
cmake -S "$SDK" -B "$CheckerBuild" -A x64
cmake --build "$CheckerBuild" --config Release --target sco-plugin-check
ctest --test-dir "$PluginBuild" -C Release -R '^inspector-module-workflow$' --output-on-failure
& "$CheckerBuild/Release/sco-plugin-check.exe" "$PluginBuild/test-plugins/inspector" --invoke inspector.read
```

Stop on any nonzero command exit; do not install a failed build/check. The
checker executable is built from **the published zip's tool**, not the host's
private source. Its default fixture has no capabilities or game services:
`inspector.read` must give bounded unavailable/unknown readings, without mutation.
A successful checker exit means its checks passed, not that health or transforms
were obtained from the game. The named module test dynamically loads the actual
module against synthetic public providers, including successful reads, truncated
or null methods, absent services/player and unknown maximum health.

macOS/Linux equivalent after the same release/hash verification and extraction:

```sh
set -eu
# Absolute paths; WORK is outside SOURCE and SDK is the extracted archive root.
cmake -S "$SOURCE/plugins" -B "$WORK/plugin-build" \
  -DSCO_SDK_INCLUDE_DIR="$SDK/include" -DSCO_WORKFLOW_TESTS=ON
cmake --build "$WORK/plugin-build" --target sc-offline-inspector inspector-module-test
cmake -S "$SDK" -B "$WORK/checker-build"
cmake --build "$WORK/checker-build" --target sco-plugin-check
ctest --test-dir "$WORK/plugin-build" -R '^inspector-module-workflow$' --output-on-failure
mkdir -p "$WORK/check-plugins"
cp -R "$WORK/plugin-build/test-plugins/inspector" "$WORK/check-plugins/inspector"
python3 -c 'import pathlib,re,sys; p=pathlib.Path(sys.argv[1]); modules=[x for x in p.iterdir() if x.suffix in (".so",".dylib")]; assert len(modules)==1; ini=p/"plugin.ini"; ini.write_text(re.sub(r"(?m)^entry[ \t]*=.*$", "entry = "+modules[0].name, ini.read_text()))' "$WORK/check-plugins/inspector"
"$WORK/checker-build/sco-plugin-check" "$WORK/check-plugins/inspector" --invoke inspector.read
```

These platforms emit their own module suffix, not `inspector.dll`. The separate
checker copy above changes `entry` to the actual emitted filename; never rename
a development module to `.dll` and call it a Windows build.
The module test already takes the actual native path. Native plugins run trusted
code; a stand-in checker is not a sandbox or a real-host fault-containment test.

### Stage 3 — Install, enable and exercise the command

Use only the **Windows x64** output for installation. With the game closed:

```powershell
$Product = "C:/mods/sc-offline"
$Destination = Join-Path $Product "data/plugins/inspector"
# Back up any existing inspector folder before replacing it.
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item "$PluginBuild/test-plugins/inspector/plugin.ini" $Destination
Copy-Item "$PluginBuild/test-plugins/inspector/inspector.dll" $Destination
```

Keep `plugins = on` in `sc-offline.ini`; enable Inspector in the launcher's
Plugins page if a preserved `disabled` marker exists. Fresh release folders have
no marker. Start only through the existing offline launcher and inspect
`data/mod.log` for native inspector load or the precise refusal. Invoke
`inspector.read` through the host command registry; no new menu tab or hotkey is
installed. Before player spawn, dependent fields must remain unknown.

For this command-only consumer, `sco.reload inspector` is the running-host reload
route: unload withdraws its owner-scoped command, then a fresh load registers it
and future reads reopen services. Rebuild while the module is unloaded or with
the game closed, not by overwriting a loaded Windows DLL. Disabling the folder
applies at next game start; `plugins = off` prevents the external inspector from
loading. Neither mode resurrects a built-in inspector. Queried native service
providers can be session-lifetime/restart-only; this consumer example does not
promise provider hot reload or recovery after native corruption.

**Owner Windows/in-game acceptance is NOT RUN.** Record the exact game build,
host and plugin versions and the checks in [status.md](status.md) before claiming
engine behavior or in-game reload safety. CI/checker/synthetic proof does not
replace that run; releases remain human decisions.

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

The restricted Lua counterpart is `plugins/inspector/inspector.lua`, a
**non-default companion example** with the existing `inspector_lua` ID and
`inspector_lua.read` command. With the game closed, copy the script to
`data/plugins/inspector_lua/inspector.lua` and rename `lua-plugin.ini` to
`plugin.ini` in that folder. This language example is not an additional baseline
feature or a release default. It uses only the
existing `sco.game.actors` and
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

Use the **published**, checksum-verified SDK from the tutorial above. The
combined legacy archive contains core and game public headers; an approved,
exact-compatible core+game archive composition can also be used. Do not package
a local source checkout as a substitute for this release-consumer proof, or
point the plugin build at the host source include tree.

```sh
# SDK_INCLUDE is the verified published archive's include/ directory.
cmake -S plugins -B build/workflows \
  -DSCO_SDK_INCLUDE_DIR="$SDK_INCLUDE" \
  -DSCO_WORKFLOW_TESTS=ON
cmake --build build/workflows --target \
  inspector-module-test
ctest --test-dir build/workflows \
  -R '^inspector-module-workflow$' --output-on-failure
python3 tools/test_workflow_boundary.py --sdk-include "$SDK_INCLUDE"
lua plugins/inspector/inspector_test.lua
```

The host header path is given **only** to the optional built-in adapter tests;
shared feature libraries and external modules compile against the extracted
SDK. Leave it unset to build only SDK-only targets. On macOS/Linux,
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` enables the targeted sanitizer
run. The legacy workflow boundary probe builds both development modules. For the
shipped inspector, CI uses `tools/check_inspector_sdk.py --archive <published.zip>
--sums <release-SHA256SUMS> --work <new-directory-outside-checkout>`. It verifies
archive and extracted checksums, copies only feature/test sources into an isolated
root, builds/installs inspector and the published checker, and runs the named
module workflow and missing-service checker invocation. Two full compiler traces
are checked against exact published header destinations (including system-marked
includes); MSVC traces use the evaluated toolchain IncludePath. POSIX controls
reject a host header, product header, unresolved private symbol and system-marked
transitive foreign header. Strict links reject private symbols on Mach-O/ELF;
Windows links enforce this normally. The install inventory contains only the
manifest and native module, with no disabled marker or test/example files.

The native tests exercise built-in forwarding, exported entrypoints and actual
dynamic module loading against synthetic public providers, including truncated
and null methods, absent capabilities, no player, unknown health maximum,
delayed visibility, foreign IDs, pre-stream removal, invalid pose, partial
batches, bounded capacity, high-bit IDs, failed registration and repeated unload.
These are host-independent conformance checks, **not** a full product build or
sco-core real-host provider integration.

**Not run in game.** Windows/MSVC host and isolated external-plugin builds
remain required in CI; owner acceptance is still NOT RUN. Exact-game-build
acceptance must still record the chosen
archetype/build and inspect player fields, owned spawn/visibility/removal and
unload cleanup in each mode. No game support certification, seat regression
closure, tag or release follows from the synthetic checks.
