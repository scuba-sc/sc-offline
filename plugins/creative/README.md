# Creative: opt-in published-SDK native plugin

Creative **1.1.0** is a command-first consumer of the published **sdk-v2.4.0-rc.2**
combined `sco-sdk-2.4.0.zip`: plugin ABI **1.1**, `game.creative` **1.0**, Star
Citizen game pack **0.8.0**. Its module, manifest and empty `disabled` marker
install under `data/plugins/creative/`. It stays **off by default**. It also
adds the menu's **Creative** tab through `sco.widgets` 1.0 (see below). No ImGui,
private host symbols, engine pointers, network access or new SDK ABI is used.
Native code is trusted code: the checker is not a sandbox.

## Three stages: obtain, build/check, install

1. Obtain `sco-sdk-2.4.0.zip` and the adjacent release `SHA256SUMS` from the
[published SDK 2.4.0 RC2 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2).
   Do not substitute a locally repackaged SDK or a source-checkout include tree.
2. From this checkout, run the isolated proof below with a **new directory outside
   the checkout**. It verifies the release archive and every extracted inventory
   file, copies only this feature and its test, builds the actual module, runs
   one named dynamic-module fixture, traces both production translation units,
   installs the folder and uses the archive's `sco-plugin-check` without game
   providers. It checks an explicitly enabled copy; the staged installation
   retains its `disabled` marker. POSIX additionally rejects private host and
   product headers, unresolved host symbols and system-marked transitive foreign
   includes. Windows uses Visual Studio 2026 x64 and captures full MSVC includes.

   ```sh
   python3 tools/check_creative_sdk.py \
     --archive "$ASSETS/sco-sdk-2.4.0.zip" --sums "$ASSETS/SHA256SUMS" \
     --work "$WORK/creative-proof" --sanitize
   ```

   Define `ASSETS` as the downloaded assets directory and `WORK` as an external
   scratch directory. Omit `--sanitize` on Windows; use `python` in Developer
   PowerShell and the same three path arguments. Sanitizers instrument only the
   POSIX module fixture, not the shipped/checker module. Compiler/link failures
   and nonzero checker/test exits are stop conditions.

   Alternatively configure `plugins/creative` alone with
   `-DSCO_SDK_INCLUDE_DIR=<verified-sdk>/include -DSCO_WORKFLOW_TESTS=ON`.
   Build `sc-offline-creative` and `creative-module-test`; run only
   `ctest --test-dir <build> -C Release -R '^creative-module-workflow$' --output-on-failure`.
   Install with `cmake --install <build> --config Release --prefix <stage> --component creative`.
3. With the game closed, back up any existing folder and install the **Windows
   x64** staged `data/plugins/creative/` into the product. Keep the marker until
   the user explicitly enables Creative on the launcher's Plugins page. The
   host also needs `plugins = on` and its existing offline admission checks.
   macOS/Linux `.so`/`.dylib` files are development/checker artifacts, **not** game
   DLLs. Inspect the host load/refusal log before invoking commands.

## Commands and configuration

| Command | Argument | Capability |
| --- | --- | --- |
| `creative.god` | boolean on/off | `game.creative.god_mode` |
| `creative.noclip` | boolean on/off | `game.creative.fly` |
| `creative.noclip_speed` | finite float, 1–10000 m/s | `game.creative.fly` |
| `creative.ammo` | boolean on/off | `game.creative.ammo` |
| `creative.ship_ammo` | boolean on/off | `game.creative.ship_ammo` |
| `creative.status` | none | provider `state` availability |

The manifest declares `noclip_speed`: float, default **10**, minimum **1**,
maximum **10000**. `sco.settings` owns validation and persistence in the existing
`creative` namespace. This consumer reads only its own setting. It adds no new
storage format and does not write settings or auto-enable any toggle at load.

When `creative.noclip true` is explicitly invoked, it reopens settings, reads the
current configured speed and applies that speed **before** requesting fly mode.
A malformed/unreadable settings member or failed speed call refuses that request;
if fly mode later fails, the accepted speed remains owner-scoped until release.
Changing a setting does not itself mutate the game; it takes effect on the next
explicit noclip-on request. `creative.noclip_speed` changes speed immediately but
not the persisted setting; the next noclip-on request reapplies that setting.
With no settings service, noclip retains the existing game-speed behavior. A
failed settings lookup is not cached or treated as success.

## Creative tab

After its commands register, the plugin adds a `sco.widgets` 1.0 panel
`creative.panel` titled **Creative**: toggles for God mode, Noclip, Infinite ammo
and Infinite ship ammo, a Noclip speed slider (1 to 10000 m/s) and a status line.
Each control calls the same setter as its command (Noclip on applies the
`noclip_speed` setting first, as `creative.noclip true` does), then the panel is
redrawn from the provider's `state`, so a refused toggle flips back and the status
line shows the provider's reason (or "see mod.log"). Toggles start from `state`
(all off when it is unavailable); the slider starts from the `noclip_speed`
setting, or 10 when that is unreadable. Opening the panel switches nothing on.

When `sco.widgets` is absent or truncated, or refuses the panel, the plugin logs
one warning and loads anyway; the commands do not depend on the tab. The panel
is removed on unload. `sco_widgets.h` is one of the SDK headers CMake requires.

## Service and lifecycle boundary

Every service use is freshly queried at the declared version. Each invoked
method requires its **complete pointer extent** and a non-null callback; mutation
also checks its own capability even when reached outside host command gating.
Missing, truncated or withdrawn providers return unavailable; provider refusal,
wrong-thread and fault results remain failures, with a bounded provider reason
when `last_error` is usable. Invalid arguments never reach setters.

No borrowed service table is retained across commands or panel events. No worker,
tick, `sco.ui` tab, hotkey or settings subscription is installed; the Creative tab
is the `sco.widgets` panel above. SDK command handlers are withdrawn
on unload or failed load. The module makes **no extra setter calls on unload**:
`game.creative` owns release-hook cleanup, which switches off only toggles still
owned by Creative and restores its fly speed. Manually switching everything off
would steal another plugin's state. `creative.noclip false` switches fly mode off;
the provider restores owner-scoped speed at unload, not necessarily at fly-off.

For clean command-consumer unload/reload use the existing `sco.reload creative`
route. Rebuild only while unloaded or with the game closed; never overwrite a
loaded DLL. Folder disablement applies at the next start. Service-provider reload
and recovery after a native fault are **not** promised; restart may be required.
A terminated process is not a successful cleanup run.

## Evidence and limitations

The named fixture dynamically loads the independently compiled native module,
then tests complete and physically truncated host/service tails, null callbacks,
failed lookup/reopen, capability refusal, invalid values, propagated provider
errors, settings changes/refusals, unload twice, failed load, stale callback
suppression, clean reload and physical module close/reopen. A synthetic
`sco.widgets` table re-checks the panel against sco-core's node rules and drives
its toggle and slider events (including refusals, a refused panel and a
missing/truncated renderer); the published checker also renders the panel's
node list and delivers one toggle event. ASan/UBSan instrument
those truncated allocations on development platforms; leak detection is disabled
for the pinned SDK's intentionally process-lifetime callback registry.

Toggle/speed restoration and foreign-owner preservation use an explicitly
**synthetic provider release-hook stand-in**. This is consumer/module evidence,
not the actual Windows host guard or engine release hook. The published checker
with absent game services reports `creative.status -> unavailable` and zero
checker failures. A successful checker is not gameplay certification.

**Windows/MSVC execution, real-host fault containment, offline/EAC admission,
engine toggle/speed restoration, rendering and owner in-game acceptance: NOT
VERIFIED locally.** No network/transport policy exception is introduced. See the
[SDK workflow tutorials](../../docs/sdk-workflows.md) and
[product decisions](../../docs/decisions.md). Project code is GPL-3.0-only; retain
the SDK's upstream notices. Its four MIT wire headers do not relicense this plugin.
