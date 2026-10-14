# Discord: blocked presence, local diagnostics only

`discord` remains one of the 13 baseline feature units, not a retired feature.
Presence is **unavailable pending owner privacy/security approval**, even if the
folder is manually enabled. The source and release folder metadata contain an
empty `disabled` marker. This is the specific Discord exception to enabled
first-party folders in [owner decisions](../../docs/decisions.md).

## Transport and privacy review

The previous implementation opened `\\.\pipe\discord-ipc-N` directly with Windows
file APIs, outside the SDK's host-mediated transport policy. Its activity payload
contained the process ID, session start timestamp, host version, optional location
and ship class, image labels, and optional invite/download URLs. Location and ship
were still read from the game when their display preferences were off.

A local write is **not** a promise that information stays local: Discord may
publish presence through its own service to other users. The old documentation's
"nothing goes over the network" claim did not establish privacy compliance.
There was no account/token read in the plugin; that does not make the activity
fields approved or prevent correlation with a Discord profile.

Published SDK 2.2.0's `sco.ipc` is owner-scoped shared memory with SCO framing,
not an arbitrary Discord named-pipe client. `sco.net` is private authenticated
co-presence, not a Discord transport. Neither contract supplies an approved route
for this feature. No custom pipe/network exception, invented service name, peer
helper or new API is introduced. See the published SDK's `docs/plugin-rules.md`
and `include/sco_ipc.h`.

## Current behavior

- `discord.status` returns `SCO_UNAVAILABLE` and explains the transport/field
  approval blocker. Loading the diagnostic module succeeds so the rest of the
  host stays usable; load success is **not** active Rich Presence.
- No game service, host version, process ID, timestamp, location, ship, account,
  token, socket, pipe, shared-memory channel or worker is used. Nothing is sent.
- The three existing settings (`show_location`, `show_ship`, `show_buttons`) keep
  their names and bool types, with defaults now **false**. Kept true preferences
  are not erased or treated as consent. They are read only as inactive local
  diagnostics through a fresh, extent-checked `sco.settings` table on each status
  call. Missing/short/null/failed providers fall back to false.
- No ticks or settings-change subscriptions are registered; no retry worker or
  cleanup transport remains. Unload only closes diagnostic admission. The host
  removes its owner-scoped command. Unload twice and reload are exercised by a
  synthetic module loader; native faults still require restart, not recovery.
- The launcher switch still changes the folder marker. Removing it cannot enable
  transmission in this version. Existing settings databases and launcher code
  are unchanged. No host fallback restores the removed custom transport.

## Build, check, install (three stages)

1. Obtain the published `sdk-v2.2.0` combined `sco-sdk-2.2.0.zip` and its release
   `SHA256SUMS`. SDK 2.2.0 keeps plugin ABI **1.1**; this plugin is **1.1.0**.
2. From this repository, run the focused standalone proof with a new work
   directory **outside** the checkout:

   ```sh
   python3 tools/check_discord_sdk.py --archive /path/to/sco-sdk-2.2.0.zip \
     --sums /path/to/SHA256SUMS --work /path/to/new-discord-proof
   ```

   It verifies release and extracted hashes; builds just this feature with
   published headers; dynamically loads its actual module; exercises settings,
   refused status, failed load, unload twice and binary reopen; traces exact
   dependencies; and stages `stage/data/plugins/discord/{plugin.ini,disabled,module}`.
   It builds/runs the archive's actual `sco-plugin-check --invoke discord.status`
   with services absent. **The checker deliberately loads disabled folders; it
   does not prove real host discovery honors the marker.** POSIX additionally
   rejects private host headers, product headers/symbols and system-marked
   transitive foreign includes. MSVC dependency traces are configured but Windows
   execution remains owed. Development `.so` modules are not game DLLs.
3. Windows only: copy the staged `discord` folder to `data/plugins/discord/`.
   Leave `disabled` in place. A deliberate diagnostic-only enable with
   `plugins = on` exposes `discord.status`, not presence. Neither the proof nor
   module changes firewall, EAC or launcher/network policy.

The independent CMake entry is `plugins/discord/CMakeLists.txt`; it also supports
`add_subdirectory(discord)` in the shared standalone plugin build. Release/CI
wiring is a separate integration step, not implied by a local proof.

## Decision pending owner

Approve a supported host-mediated transport and a bounded explicit transmitted
field/consent policy before implementing active presence. SDK support must land
upstream if the approved route needs a new contract. There is no approved transport
or field set today. Re-enabling presence requires a reviewed consumer change, not
a preference or a build flag; this refusal is narrow and reversible.

**Evidence:** published-archive compile, synthetic actual-module/settings/lifecycle
and checker proof. **Not verified:** Windows/MSVC DLL load, real host disabled
folder discovery/reload, cross-process transport, Discord publication/privacy,
Wine, or in-game behavior. This is neither gameplay nor privacy certification.

Project code is GPL-3.0-only; native plugins are trusted in-process code, not
sandboxed. No SDK license exception is added.
