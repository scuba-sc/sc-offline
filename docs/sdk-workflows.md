# Published-SDK inspector and owned NPC workflows

For distinct author-owned IDs rather than replacing release features, use the
[copyable SDK/MCP tutorial](../examples/sdk-mcp/README.md). It supplies
`tutorial_npc` and `tutorial_inspector` copies and keeps static discovery separate
from optional default-off runtime MCP. The published RC2 tutorial proof now passes:
MCP discovery/version/refusal checks, generated Linux plugin synthetic checks,
NPC/inspector fixture cases, two full compiler dependency traces (including
system headers), private foreign-mutation controls, and rejection of separate
SDK/source symlink escapes. `tools/check.sh` reported zero new diagnostics across
65 translation units using local extracted authenticated Debian Clang/MinGW.
That Debian run needs a non-default authenticated kernel-header include root;
the default Debian layout still refuses, and no tracer code was changed. Windows,
actual-host loading, runtime MCP, and in-game acceptance remain **NOT RUN**.

The newer [loadout/SQ42 consumer](../plugins/loadout/README.md) uses G6 typed
equip, G7 **read-only** cvars, public copied content and optional widgets. Their
headers ship in the pinned published SDK 2.4.0 (SDK 2.0.0 lacked them). Its build
is separate from the published tutorial below; release staging is pending.
No loadout built-in or legacy host fallback remains; quantum control is deferred.

The already external, shipped-off creative plugin has its own
[published-SDK build/check/install and settings/lifecycle guide](../plugins/creative/README.md).
Its focused `creative-module-workflow` tests the actual module; reversible cleanup
uses a synthetic provider release-hook stand-in, not Windows/game certification.

Steps 1B/1C ship inspector and NPC as enabled external native plugins under
`data/plugins/{inspector,npc}/`, not production built-ins. No host linkage,
registration or fallback remains for either. Seven built-ins and six external
features (npc, inspector, crew, spawn, creative, discord) preserve 13 baseline feature units;
two optional bridges are separate. Neither `plugins = off` nor a disabled feature
folder resurrects a hidden implementation. Creative stays off and launcher behavior is unchanged. Step 1D separately
sets Discord folder metadata off and refuses presence pending privacy/transport
approval; see [Discord review and proof](../plugins/discord/README.md).

The external modules use their feature sources and public SDK headers only, not
private host registration, product version, host include trees or new ImGui inputs.
The old NPC tab is removed pending renderer-neutral widgets ([sco-core #139](https://github.com/scuba-sc/sco-core/issues/139)); Crew and ordinary spawn are also external command-first consumers; their
legacy product picker tabs are removed. Legacy `src/npc.cpp` is untouched. `src/workflow_adapters.cpp` is removed.

Use published SDK 2.4.0 release candidate 2, tag **`sdk-v2.4.0-rc.2`**, combined **`sco-sdk-2.4.0.zip`**, verified against
release **`SHA256SUMS`**: SDK 2.4.0, plugin ABI 1.1 and Star Citizen game pack 0.8.0
are distinct versions. The tag identifies RC2 regardless of GitHub's release
classification. The RC2 release is currently marked `prerelease=false`. Nothing
new in 2.4.0 has been run in game. Every external plugin uses this archive;
the embedded host independently pins unpublished development source
`1166c9f12d7327dd50a58b1d9261e7d6cc21bef6` (see [SDK candidate coordinates](sdk-candidate.md)).

The expanded inspector uses that same published RC2 by default; no older unpublished candidate
lock is needed. The proof validates the archive and full extracted inventory before building.
`plugins/CMakeLists.txt` also checks all inspector contract headers at configure time. An explicitly
selected non-default version/hash requires the matching archive, rather than accepting host headers.
The inspector, NPC, loadout, creative, Discord, crew, mining, spawn, build, contracts,
teleport and quantum proof CLIs accept an explicit unpublished preflight with `--candidate`,
`--sdk-version`, `--sdk-sha256`, `--archive-sha256`, the full Core commit in `--sdk-source`,
and `--sdk-profile legacy`. The two digests must agree with the archive bytes and supplied
checksum inventory. The extracted inventory, compatibility profile and component source
commits, and catalogue, guides and workflow source identities must match before use.
These checks bind the supplied identity to archive contents; they do not independently
verify source rights or grant upstream release authorization. A development preflight
does not turn an unpublished archive into a published candidate or make an installed-host
capability claim. Run the matching connected quantum, build and contracts fixtures
documented by those consumers when a matching host-source build is available.

Windows inspector, NPC and build proof scripts accept `--generator` for a locally installed toolchain
(for example `--generator "Visual Studio 17 2022"`). Their default remains **Visual Studio 18 2026**,
matching hosted CI. POSIX proofs use Unix Makefiles. This changes toolchain selection, never the
SDK source/archive identity or dependency-isolation rules.

Standalone modules/manifests stage under `<plugin-build>/test-plugins/{npc,inspector}/`.
Windows emits `npc.dll` and `inspector.dll`; macOS/Linux emit development-only
native modules, not Windows game-loadable files. Release installation contains
only each native module and manifest, enabled without a `disabled` marker. Lua is
a non-default inspector example, not another release feature. See [decisions](decisions.md).

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

Download **`sco-sdk-2.4.0.zip`** (the combined legacy core+Star Citizen SDK) and
**`SHA256SUMS`** from the
[published SDK 2.4.0 RC2 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
(SDK 2.4.0 release candidate 2; the archive is named for the SDK version, the tag carries `-rc.N`).
Its SHA-256 is `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`.
Use an empty work directory. In Developer PowerShell for Visual Studio:

```powershell
$Work = Join-Path $PWD "inspector-dev"
New-Item -ItemType Directory -Path $Work -ErrorAction Stop | Out-Null
Set-Location $Work
$Release = "https://github.com/scuba-sc/sco-core/releases/download/sdk-v2.4.0-rc.2"
Invoke-WebRequest "$Release/sco-sdk-2.4.0.zip" -OutFile "sco-sdk-2.4.0.zip"
Invoke-WebRequest "$Release/SHA256SUMS" -OutFile "SHA256SUMS"
python -c "import hashlib,pathlib; p=pathlib.Path('sco-sdk-2.4.0.zip'); rows=[l.split() for l in pathlib.Path('SHA256SUMS').read_text().splitlines() if l.strip()]; hashes=[r[0] for r in rows if r[1].lstrip('*')==p.name]; assert len(hashes)==1 and hashlib.sha256(p.read_bytes()).hexdigest()==hashes[0], 'SDK archive checksum mismatch'"
if ($LASTEXITCODE -ne 0) { throw "Do not extract an unverified SDK" }
python -m zipfile -e sco-sdk-2.4.0.zip sdk
$SDK = Join-Path $Work "sdk/sco-sdk-2.4.0"
```

Check the archive's `VERSIONS.txt` and `COMPATIBILITY.json`: SDK 2.4.0,
plugin ABI 1.1, game pack 0.8.0. The archive root is `sco-sdk-2.4.0/`, with
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

### Stage 3 — Install, enable and exercise Inspector

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
`inspector.read` through the host command registry on the game thread for a fresh
player read. When the host offers `sco.widgets`, the optional `inspector.main`
panel exposes Diagnostics, Inventory, Actions and Parameters. Use **Refresh** or
`inspector.refresh` to capture diagnostics and inventory explicitly; drawing,
tab selection and cached paging do not poll providers. Commands remain usable
without widgets. See [read and snapshot behavior](#read-only-inspector). Before
player spawn, dependent fields must remain unknown.

For this external consumer, `sco.reload inspector` is the running-host reload
route: unload removes its optional panel and clears captured state; the host
withdraws its owner-scoped commands. A fresh load registers commands and, when
available, the panel starts **Not sampled**. Rebuild while the module is unloaded
or with the game closed, not by overwriting a loaded Windows DLL. Disabling the folder
applies at next game start; `plugins = off` prevents the external inspector from
loading. Neither mode resurrects a built-in inspector. Queried native service
providers can be session-lifetime/restart-only; this consumer example does not
promise provider hot reload or recovery after native corruption.

**Owner Windows/in-game acceptance is NOT RUN.** Record the exact game build,
host and plugin versions and the checks in [status.md](status.md) before claiming
engine behavior or in-game reload safety. CI/checker/synthetic proof does not
replace that run; releases remain human decisions.

## Worked tutorial: standalone native NPC

### Stage 1 — Obtain and verify

Follow inspector Stage 1 above in a fresh `npc-dev` work directory. Download the
same `sdk-v2.4.0-rc.2` combined `sco-sdk-2.4.0.zip` and release `SHA256SUMS`,
verify the archive **before extraction**, and use only the extracted SDK `include/`.
CMake 3.20+, Python 3 and C/C++20 are required; the game module requires Windows
x64/MSVC. `plugins/npc/{npc.cpp,npc.h,external.cpp,plugin.ini}` are the copyable
feature sources. Manifest: `id = npc`, independent version `1.1.0`, `api = 1.1`,
`kind = native`, `entry = npc.dll`. No required game capabilities prevent load;
individual commands report missing services/capabilities.

### Stage 2 — Isolated build and checker

With `$Work`, `$SDK` and `$SOURCE` set as in Stage 1, in Developer PowerShell:

```powershell
$PluginBuild = Join-Path $Work "plugin-build"
$CheckerBuild = Join-Path $Work "checker-build"
cmake -S "$SOURCE/plugins" -B "$PluginBuild" -A x64 -DSCO_SDK_INCLUDE_DIR="$SDK/include" -DSCO_WORKFLOW_TESTS=ON
cmake --build "$PluginBuild" --config Release --target sc-offline-npc npc-module-test
cmake -S "$SDK" -B "$CheckerBuild" -A x64
cmake --build "$CheckerBuild" --config Release --target sco-plugin-check
ctest --test-dir "$PluginBuild" -C Release -R '^npc-module-workflow$' --output-on-failure
& "$CheckerBuild/Release/sco-plugin-check.exe" "$PluginBuild/test-plugins/npc" --invoke npc.observe
```

Stop on any nonzero exit. The checker is compiled from the published zip's
`tools/sco-plugin-check.c`; its missing-service fixture must report unavailable,
not manufacture visibility. The named module test loads the actual native module
against synthetic public providers, not the game or a native safety sandbox.

On macOS/Linux use inspector Stage 2's POSIX commands with targets
`sc-offline-npc npc-module-test`, CTest regex `^npc-module-workflow$`, folder `npc`
and invocation `npc.observe`. Rewrite `entry` **in a separate checker copy** to
the actual emitted `.so`/`.dylib`, exactly as that example does. Never rename a
non-Windows development module to `.dll` or install it into the game.

### Stage 3 — Install, enable and invoke

With the game closed, install only the Windows x64 output:

```powershell
$Product = "C:/mods/sc-offline"
$Destination = Join-Path $Product "data/plugins/npc"
# Back up any existing NPC folder before replacing it.
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item "$PluginBuild/test-plugins/npc/plugin.ini" $Destination
Copy-Item "$PluginBuild/test-plugins/npc/npc.dll" $Destination
```

Keep `plugins = on` in `sc-offline.ini`; enable NPC in the launcher's Plugins page
if a preserved `disabled` marker exists. Fresh release folders have none. Start
only through the existing offline launcher and read `data/mod.log` for the load
or exact refusal. Invoke `npc.spawn`, `npc.observe`, `npc.remove` and `npc.clear`
through the host command registry on the game thread. No NPC tab, hotkey, console
transport or new ImGui integration is installed. Folder disable applies at next
start; `plugins = off` blocks the external module. There is no built-in fallback.

Normal `sco.reload npc` is the running-host external-consumer route, not a promise
of provider hot reload or safe gameplay. Rebuild only with the module unloaded or
the game closed; never overwrite a loaded Windows DLL. **Native faults are
restart-required, not safe reload/recovery. In-game acceptance: NOT RUN.** Record
exact build/archetype and the [pending checks](status.md) before certifying behavior.

## Read-only inspector

Run `inspector.read` through the host command registry. The reply and plugin log
show the current player actor/entity IDs, health current/maximum, zone-local
position and quaternion rotation. Each `inspector.read` re-queries the public
actor/entity services and retains no provider table or session ID across these
calls. There is no mutation or required UI.

`inspector.refresh` captures copied diagnostics and inventory on request and
advances the snapshot generation. `inspector.status`, `inspector.stats`,
`inspector.gamerules` and `inspector.inventory` read those captured sections;
they do not refresh them. An unrefreshed snapshot is historical, not live. A
failed or unavailable section replaces its previous captured values instead of
presenting them as current. Action lookups/counts and parameter definitions are
acquired explicitly through their commands or the optional panel controls;
parameter definitions are metadata, not live GameRules values. See the
[Inspector command reference](../plugins/inspector/README.md#snapshot-commands-and-optional-panel)
for arguments, paging and refusal results.

- Before the player spawns, IDs and dependent readings are unknown.
- Missing providers, capabilities, null methods or incomplete method extents
  are reported as unavailable, not as zero-valued readings.
- A health maximum of zero is **unknown**, per the game contract. It is not a
  zero-sized health pool and is never used as a denominator.
- Failed calls, non-finite values, zero zones and invalid rotations are not
  promoted to valid data. These are observations, not new engine fields.
- Unload removes the optional widget panel and clears copied snapshots and
  borrowed widget/API/owner state; the host withdraws owner-scoped commands.
  There is no background polling or worker.

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

Use only the checksum-verified published combined SDK, not host source headers
or a locally repackaged checkout. The tutorial's named module workflows provide
host-independent synthetic proof. No host-only built-in adapter harness remains in this migration.

CI invokes `tools/check_npc_sdk.py` and `tools/check_inspector_sdk.py`, each with
`--archive <published.zip> --sums <release-SHA256SUMS> --work <new-directory-outside-checkout>`.
They verify archive and extracted checksums, copy feature/test sources into an
isolated root, build/install the module and published checker, run only the named
module workflow and invoke `npc.observe` / `inspector.read` with services absent.
Compiler dependency traces check exact published header destinations, including
system-marked includes; MSVC traces include evaluated toolchain IncludePath.
POSIX negative controls reject private host/product headers, unresolved private
symbols and transitive foreign headers. Strict Mach-O/ELF links reject private
symbols; Windows links enforce this normally. The install inventory contains
only the manifest and native module, with no disabled marker or example/test files.

NPC synthetic cases cover exported entrypoints and actual dynamic loading,
truncated/null methods, absent capabilities/provider/player, delayed visibility,
foreign IDs, pre-stream removal, invalid pose, partial batches, bounded capacity,
high-bit IDs, failed registration and repeated unload. Inspector covers valid
reads and explicit unavailable/unknown fields. These are not actual game-provider,
Windows rendering, real-host fault-containment or gameplay checks. **In-game
acceptance: NOT RUN.** Native faults require restart; synthetic success cannot
establish safe reload/recovery, seat-regression closure or game support. See
[status](status.md) for the owed exact-build checks.
