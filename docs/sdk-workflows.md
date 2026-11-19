# Build plugins against the published SDK: inspector and NPC

This page walks through building, checking and installing two plugins, `inspector` and `npc`, from their sources in `plugins/`, using only a verified SDK archive. Use it as a pattern for your own plugin. Every check here uses synthetic or absent providers. Not run in game; see [status.md](status.md).

For plugins of your own with distinct IDs, copy the [SDK and MCP tutorial sources](../examples/sdk-mcp/README.md) instead. They supply `tutorial_npc` and `tutorial_inspector`.

## Where plugins come from

The host registers no built-in features. The release stages 11 external plugin folders under `data/plugins/`. Inspector and NPC are two of them, enabled and not linked into the host. `plugins = off` or a `disabled` marker does not bring a hidden implementation back. Discord ships disabled and refuses presence pending a review; see its [README](../plugins/discord/README.md).

- Creative lives in its own repository, [`sc-creative`](https://github.com/scuba-sc/sc-creative) (the `external/sc-creative` submodule). It has its own [build, check and install guide](https://github.com/scuba-sc/sc-creative/blob/main/plugins/creative/README.md), and CI runs its `tools/check_creative_sdk.py` before staging.
- The [loadout plugin](../plugins/loadout/README.md) uses typed equip, read-only cvars, copied content and optional widgets. It is a development consumer and is not staged.
- Modules use their own sources and public SDK headers only. They use no private host registration, product version, host include path or new ImGui input. The old NPC tab is gone until renderer-neutral widgets cover it ([sco-core #139](https://github.com/scuba-sc/sco-core/issues/139)). Crew and ordinary spawn are command-first plugins and have no product picker tabs.

## Which SDK

Use the published SDK 2.4.0: tag `sdk-v2.4.0`, combined archive `sco-sdk-2.4.0.zip`, checked against the release's `SHA256SUMS`. SDK 2.4.0, plugin ABI 1.1 and game pack 0.8.0 are different version numbers. The release binds the archive to Core source `6106fa3639cbcc28f78a1ded8de2707de019f4d3`. [SDK coordinates](sdk-candidate.md) lists every archive hash.

The host itself builds from the Core at the `external/sco-core` gitlink, a development revision of the 2.4.1 line. The 2.4.0 archive and its checks do not certify that pairing.

To prove a plugin against an unpublished development archive, pass these options to the proof script. The published defaults do not change.

| Option | Meaning |
| --- | --- |
| `--candidate` | Allow an unpublished archive |
| `--sdk-version` | The version label |
| `--sdk-sha256`, `--archive-sha256` | Both must match the archive bytes and the checksum list |
| `--sdk-source` | The full Core commit |
| `--sdk-profile legacy` | The archive profile |

Most proof scripts accept them: inspector, npc, loadout, creative, discord, crew, mining, spawn, build, contracts, teleport and quantum. The script checks the extracted inventory, the compatibility profile, the component source commits, and the catalogue, guide and workflow source identities before it uses the archive. These checks tie the archive you give to its contents. They do not check source rights or grant release authority. `plugins/CMakeLists.txt` also checks the inspector's contract headers at configure time.

The Windows proofs for inspector, npc, build, teleport and quantum take `--generator` for a local toolchain, for example `--generator "Visual Studio 17 2022"`. The default is Visual Studio 18 2026, as in CI. Teleport and quantum accept Visual Studio generators only (2019, 2022 or 2026), because their Windows dependency traces need MSBuild. POSIX proofs use Unix Makefiles. The generator changes the toolchain, never the SDK identity or the dependency rules.

## Worked tutorial: standalone native inspector

The copyable source is `plugins/inspector/`. `inspector.cpp` and `inspector.h` do the reads, `external.cpp` provides the three native exports, and `plugin.ini` says `id = inspector`, `version = 1.0.0`, `api = 1.1`, `kind = native`, `entry = inspector.dll`. The manifest requires no game capability on purpose: the plugin loads without providers, so `inspector.read` can explain what is missing. It needs no private host adapter, product version or ImGui header. `inspector.lua`, `lua-plugin.ini` and `inspector_test.lua` are an optional Lua example, not part of the native install.

You need CMake 3.20 or newer, Python 3 and a C and C++20 compiler. The game DLL needs Windows x64 with MSVC. On macOS and Linux the result is a development artifact only.

### 1. Get and verify the SDK

Download `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the [2.4.0 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0). The archive's SHA-256 is `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc`. Use an empty work folder. In a Developer PowerShell:

```powershell
$Work = Join-Path $PWD "inspector-dev"
New-Item -ItemType Directory -Path $Work -ErrorAction Stop | Out-Null
Set-Location $Work
$Release = "https://github.com/scuba-sc/sco-core/releases/download/sdk-v2.4.0"
Invoke-WebRequest "$Release/sco-sdk-2.4.0.zip" -OutFile "sco-sdk-2.4.0.zip"
Invoke-WebRequest "$Release/SHA256SUMS" -OutFile "SHA256SUMS"
python -c "import hashlib,pathlib; p=pathlib.Path('sco-sdk-2.4.0.zip'); rows=[l.split() for l in pathlib.Path('SHA256SUMS').read_text().splitlines() if l.strip()]; hashes=[r[0] for r in rows if r[1].lstrip('*')==p.name]; assert len(hashes)==1 and hashlib.sha256(p.read_bytes()).hexdigest()==hashes[0], 'SDK archive checksum mismatch'"
if ($LASTEXITCODE -ne 0) { throw "Do not extract an unverified SDK" }
python -m zipfile -e sco-sdk-2.4.0.zip sdk
$SDK = Join-Path $Work "sdk/sco-sdk-2.4.0"
```

Check `VERSIONS.txt` and `COMPATIBILITY.json` in the archive: SDK 2.4.0, plugin ABI 1.1, game pack 0.8.0. The archive root `sco-sdk-2.4.0/` holds `include/`, `CMakeLists.txt` and `tools/sco-plugin-check.c`. A matching hash identifies the file you downloaded. It is not a trust guarantee. Do not replace `include/` with `external/sco-core/include`, and do not repackage the SDK yourself.

### 2. Build and check in isolation

Set `$SOURCE` to your sc-offline checkout. The plugin and the checker are separate builds, and their output goes outside the checkout.

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

Stop on any failing command. Do not install a failed build. The checker is built from the archive's own tool, not from host source. Its default fixture offers no capabilities and no game services, so `inspector.read` must return bounded unavailable or unknown readings and change nothing. A passing checker means its checks passed. It does not mean health or positions came from a game. The named module test loads the real module against synthetic public providers, including good reads, truncated or null methods, missing services or player, and an unknown maximum health.

On macOS or Linux, after the same download and hash check:

```sh
set -eu
# WORK is outside SOURCE. SDK is the extracted archive root. Use absolute paths.
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

These platforms emit their own module suffix, not `inspector.dll`. The checker copy changes `entry` to the file name that was built. Never rename a development module to `.dll`. A native plugin runs trusted code, so the checker is neither a sandbox nor a test of fault containment in a real host.

### 3. Install and use

Install only the Windows x64 output, with the game closed:

```powershell
$Product = "C:/mods/sc-offline"
$Destination = Join-Path $Product "data/plugins/inspector"
# Back up any existing inspector folder before replacing it.
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item "$PluginBuild/test-plugins/inspector/plugin.ini" $Destination
Copy-Item "$PluginBuild/test-plugins/inspector/inspector.dll" $Destination
```

Keep `plugins = on` in `sc-offline.ini`. If a `disabled` marker is left over, enable Inspector on the launcher's Plugins page. A fresh release folder has no marker. Start the game through the launcher and read `data/mod.log` for the load line or the exact refusal. Run `inspector.read` through the host command registry on the game thread to read the player fresh. Before the player spawns, dependent fields stay unknown.

When the host offers `sco.widgets`, the optional `inspector.main` panel shows Diagnostics, Inventory, Actions and Parameters. **Refresh** or `inspector.refresh` captures diagnostics and inventory. Drawing, switching tabs and paging cached rows do not query the providers. The commands work without widgets.

`sco.reload inspector` reloads it in the running host. Unloading removes the panel and clears captured state, and the host withdraws the plugin's commands. A fresh load registers the commands again, and the panel starts as **Not sampled**. Rebuild with the module unloaded or the game closed. Never overwrite a loaded DLL. A disabled folder applies at the next game start, and `plugins = off` stops the plugin from loading. Neither brings back a built-in inspector. Service providers it queries can last for the whole session or need a restart, so the plugin promises no provider reload and no recovery after a native fault.

Record the game build, host and plugin versions, and the checks you ran in [status.md](status.md) before you claim in-game behavior. A synthetic check does not replace that run.

## Worked tutorial: standalone native NPC

The sources are `plugins/npc/{npc.cpp,npc.h,external.cpp,plugin.ini}`. The manifest is `id = npc`, `version = 1.1.0`, `api = 1.1`, `kind = native`, `entry = npc.dll`. No required capability blocks loading. Each command reports the services it lacks.

Follow inspector step 1 in a fresh `npc-dev` folder with the same archive. With `$Work`, `$SDK` and `$SOURCE` set, in a Developer PowerShell:

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

The checker comes from the archive's `tools/sco-plugin-check.c`. With services missing it must report unavailable and must not invent visibility. On macOS and Linux, use the inspector commands with targets `sc-offline-npc npc-module-test`, CTest pattern `^npc-module-workflow$`, folder `npc` and invocation `npc.observe`, and change `entry` in a separate checker copy.

Install the Windows x64 output into `data/plugins/npc` as above, with the game closed and any existing folder backed up. Run `npc.spawn`, `npc.observe`, `npc.remove` and `npc.clear` through the host command registry on the game thread. The plugin installs no tab, hotkey, console transport or ImGui code. A disabled folder applies at the next start, and `plugins = off` blocks the plugin. There is no built-in fallback.

`sco.reload npc` reloads it in the running host. It does not promise provider reload or safe gameplay. Native faults need a restart.

## Inspector commands

`inspector.read` shows the player's actor and entity IDs, current and maximum health, zone-local position and quaternion rotation. It re-queries the public actor and entity services on each call and keeps no provider table or session ID. It changes nothing and needs no UI.

`inspector.refresh` captures copied diagnostics and inventory and advances the snapshot generation. `inspector.status`, `inspector.stats`, `inspector.gamerules` and `inspector.inventory` read the captured sections and do not refresh them. A snapshot you have not refreshed is old, not live. A section that fails or is unavailable replaces its earlier values; it never shows them as current. Action lookups, counts and parameter definitions are fetched on request through their commands or the panel controls. Parameter definitions are metadata, not live GameRules values. Arguments, paging and refusal results are in the [Inspector command reference](../plugins/inspector/README.md#commands).

- Before the player spawns, IDs and dependent readings are unknown.
- A missing provider or capability, a null method or an incomplete method extent shows as unavailable, never as zero.
- A health maximum of zero means unknown, as the game contract says. It is not a zero-size pool and is never a denominator.
- A failed call, a non-finite value, a zero zone or an invalid rotation is not shown as data.
- Unload removes the panel and clears copied snapshots and borrowed widget, API and owner state. There is no background polling or worker.

The Lua version, `plugins/inspector/inspector.lua`, is an optional example with the ID `inspector_lua` and the command `inspector_lua.read`. With the game closed, copy the script to `data/plugins/inspector_lua/inspector.lua` and rename `lua-plugin.ini` to `plugin.ini` in that folder. It is not a release default. It reads through `sco.game.actors` and `sco.game.entities` only, handles an absent optional adapter, and adds no mutation or spatial binding. Its test uses synthetic sandbox functions, not the bundled sco-lua runtime.

## NPC commands

All commands run on the game thread through the host command registry.

| Command | Result |
| --- | --- |
| `npc.spawn <archetype> <count>` | 1 to 10 NPCs near a fresh player pose. The returned IDs are logged at once as pending streaming. |
| `npc.observe` | Read-only counts of pending, visible, removing and unknown NPCs, plus the transforms of visible owned IDs, in the log |
| `npc.remove <id>` | Requests removal of one ID this plugin still owns |
| `npc.clear` | Requests removal of every NPC this plugin still owns. It never removes crew-wide or foreign entities. |

Give an archetype that fits the game build when you call it. The public tests use `synthetic_actor`, an artificial fixture. The plugin spawns and removes through `game.actors` and never removes an actors-owned NPC through `game.entities.despawn`. A pose must be finite, have a valid nonzero quaternion and name a nonzero streamed zone. Position and zone are used at once and not kept between commands. A partial batch reports how many were spawned and returns the failing result. Those IDs stay owned and are cleaned up normally.

`SCO_OK` from a removal means removal started, not that the NPC is gone. Removal can be requested before the NPC streams in. After the provider accepts it, the plugin gives up local ownership and does not remove it twice. `alive=0` is ambiguous (not streamed, unavailable, or the wrong thread), so it never confirms disappearance and never recycles a removal ticket. At most `SC_ACTORS_MAX_NPCS` session tickets are kept during one load, and accepted-removal tickets count against that local bound until unload. This is separate from the provider's global live-NPC quota. A foreign ID is refused without a change call. A failed removal keeps ownership for a retry, and a provider `NOT_FOUND` releases the local claim.

Unload requests removal of the remaining owned IDs once, then forgets local state. If a capability or provider is unavailable, the log says host owner-release cleanup is still needed. A repeated unload does nothing. The host's crash and unload cleanup stays authoritative. The plugin does not promise provider hot reload, safety of retained tables, or survival after the process ends. Seat workflows and the separate NPC-seat-removal regression are not covered.

## What CI runs

CI runs `tools/check_npc_sdk.py` and `tools/check_inspector_sdk.py`, each with `--archive <published.zip> --sums <release-SHA256SUMS> --work <new-folder-outside-the-checkout>`. They verify the archive and extracted checksums, copy the plugin and test sources into an isolated root, build and install the module and the published checker, run only the named module workflow, and call `npc.observe` or `inspector.read` with services absent. Compiler dependency traces check the exact published header destinations, including system-marked includes. MSVC traces include the evaluated toolchain include path. On POSIX, negative controls reject private host and product headers, unresolved private symbols and transitive foreign headers. Strict Mach-O and ELF links reject private symbols, and Windows links enforce this normally. The install folder holds only the manifest and the native module, with no `disabled` marker and no example or test files.

On Windows the player-plugin checkers also take `--generator`, `--toolset` and `--try-compile-config`. A local Visual Studio 2022 proof can use `--generator "Visual Studio 17 2022" --toolset "v143,host=x64" --try-compile-config Release`. The toolset and try-compile option apply to both the plugin and the SDK checker configurations. Build, test and install still use Release. The TitanLink proof keeps its published SDK pin.

The NPC cases cover the exported entry points and real dynamic loading, truncated or null methods, absent capabilities, provider or player, delayed visibility, foreign IDs, removal before streaming, invalid pose, partial batches, bounded capacity, high-bit IDs, failed registration and repeated unload. The inspector cases cover good reads and explicit unavailable or unknown fields. None of this uses a game provider, Windows rendering, real-host fault containment or gameplay. Native faults need a restart, and a passing synthetic check does not show safe reload, recovery or game support.
