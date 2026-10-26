# Quantum control and Travel — SDK-only G8/G3 consumer

The external `quantum` folder owns commands, input policy and the retained **Travel** panel. It uses only extracted public SDK headers. The host does not compile/start its old `src/quantum.cpp` hooks, and its global quantum cvar keeper no longer runs. Legacy source is retained inactive; there is no built-in or private-pointer fallback when plugins are off.

## Approved controls and current engine blocker

G8 control is approved for R1. Core's `game.quantum` / `sc_quantum_v1` activates its capability only with an explicitly configured complete game-pack backend and installed owner cleanup. **The default production engine adapter is still absent.** Signature matches and historical offsets do not certify local-drive ownership, offline admission or reversible charging/audio/effect teardown. Consequently default production controls still refuse. This module is an implemented consumer, **not completed boost/effects/NAV gameplay parity**.

| Command | Contract |
| --- | --- |
| `quantum.enable <on:bool>` | Explicit owner-scoped enable/disable; no load-time auto-enable |
| `quantum.hold <milliseconds:int>` | 1..1000 ms inclusive; fresh capability/service lookup; provider rechecks thread/owner/drive/offline/EAC |
| `quantum.release` | Owner-only hold cancellation, even after the capability closes |
| `quantum.boost` | Toggle hold for at most 1000 ms without refresh; optional `ctrl+alt+b` binding |
| `quantum.control_status` | Copied bounded phase/block/progress/owner state; unknown/unavailable is explicit |

After explicit enable, the Windows game-thread tick can refresh **150 ms** hold leases every **50 ms** while Caps Lock is physically held and this process owns the foreground window. `caps_input` defaults true. `mouse_input` defaults **false**; enabling it explicitly treats the primary mouse button as hold input while focused. It **does not infer NAV mode** or intercept the game's B/engage action: leave it off for ordinary weapon firing. Native NAV/action forwarding requires an admitted game-pack adapter and remains pending. Non-Windows builds do not poll input.

Charging, drive input forwarding, audio handles/effect state and restoration remain **G8 provider responsibilities**, not arbitrary plugin effect requests or raw writes. The public contract has no effect-tag, sound-ID, arbitrary action, cvar or game-pointer route. Do not copy legacy effects into the consumer to bypass the missing adapter. G7 cvar writes remain unapproved.

Every callback extent is checked before use, tables are reopened per operation, durations/arguments/copied observations are validated, and the plugin never steals another owner's control. Capability/admission loss stops input refresh and requests owner cancellation. Ordinary unload unsubscribes, removes its hotkey and requests cancellation/disable for its own lease; the provider's actual owner-release hook remains authoritative for quarantine/fault cleanup. Failed cleanup is not a reload-safety claim.

## Received GameRules in the control panel

The existing Travel panel copies the public `game.gamerules` 1.0 snapshot only
when `game.gamerules.read` is available. It shows the received generation,
quantum-travel and boost gates, and whether group QT is enforced. Group
membership remains unknown; enforcement is never treated as proof that the
player belongs to a group. A generation is an opaque update token, including
zero, and is not ordered or treated as a session identity.

Automatic presentation reads occur at most once per 250 ms. Explicit state
refresh and a UI request to start boost reopen and read the service immediately.
Every read checks the current capability, complete callback extent/non-nullness,
record size, boolean gates and finite thresholds. Thresholds are not used to
invent range or membership rules. Missing/unreadable, malformed or not-yet-valid
snapshots clear any previously displayed permission and show an explicit unknown
state. The last allowed snapshot is never used after a failed refresh.

The boost affordance requires a received, valid snapshot allowing both quantum
travel and boost, plus the existing capability and explicit owner lease. Unknown
or forbidden rules pause new boost through this panel. The UI rechecks before
starting a hold, so a stale or forged click cannot use an old allowed snapshot.
A held boost can still be released when rules or capabilities become unreadable;
the separate owned Release control and disable/cancel routes remain available.
Control callbacks carry an opaque per-load identity, so callbacks retained from a
previous load cannot start or release the new owner's controller.

This is control-panel guidance. Existing commands, hotkeys and focused Windows
input retain their G8 provider checks and authority; the snapshot does not replace
native admission or enable a backend. These quantum-travel rules do not gate the
separate `game.relocation` destination/bookmark operations. The default production
GameRules reader and G8 adapter remain unavailable; no default, SDK pin, staging
or native activation changes here.

## Existing travel and storage compatibility

Retains `quantum.travel(place, altitude)`, `quantum.bookmark(name)`, `quantum.save_bookmark(name)` and `quantum.scan`. `quantum.travel` and `quantum.bookmark` move you through sco-core's public **`game.relocation` 1.0**: the plugin's `relocation` setting is checked, then the capability `game.relocation` fresh, then the arguments; one request goes out (zone id, pose local to that zone, 30 s timeout), and the plugin follows the ticket each game tick and reports arrival or the failure/cancellation reason once. One move at a time; tickets are never persisted. `quantum.travel` finds the destination zone by its entity name and asks for a point at radius + altitude (100..20000 m) along that zone's own +Z axis, identity rotation; `quantum.bookmark` resolves a saved bookmark's exact/partial pose. `game.relocation` moves only within your current star system and only with sco-core's offline admission, a verified engine backend, a live player and a known system, which needs this release's host, **sco-core main e616ed6 or later**; without those conditions moves answer unavailable. The `relocation` setting (bool, default true, "Allow moving the player (offline only)") turns moves off for this plugin; `relocation = off` in `sc-offline.ini` turns them off for every plugin once the host pin has `SetRelocationEnabled`. `quantum.status`, `quantum.delete_bookmark(name)` and `quantum.inspect_bookmark(name)` remain unchanged. `quantum.scan` copies at most 256 observed zones, 64 per page, with epoch checks; it is not exhaustive system discovery. Unknown system/root/name never authorizes movement; the provider decides the star system.

Curated destinations use the public content route described below. Scan-generated `locations_found.txt` retains its existing `system | display name | entity name | radius` format and filesystem policy. Bookmarks remain `bookmark system|name` followed by `spot x y z zone` lines. Optional `pose x y z w` records preserve orientation; old readers ignore them and old files default to identity. `data_root` selects this persistence root (default `data`), not a curated-list source. Owner-scoped `quantum.db` keeps the exact `bookmarks(pos,system,name,spots)` schema and `bookmarks.txt.time` marker. Windows markers remain decimal FILETIME. Import/update code is unchanged; absent storage retains text fallback and failed DB transactions do not overwrite it.

### Curated destinations from an ordinary data pack

While the host is stopped, explicitly install an ordinary enabled `kind = data`
pack with a `lists/locations.txt` file. The existing published RC1 `sco.content`
1.0 service provides its copied startup bytes. No default pack or production
staging is added here. The original `data/locations.txt` is untouched and is no
longer read by this external module; it is not automatically imported or used
as a fallback. Users who want those curated rows must copy their own list into
the pack explicitly. Restart the host after pack changes.

Exactly one public List record must have the case-sensitive relative name
`lists/locations.txt`. No match leaves curated destinations unavailable; two
matches, including the same name in different packs, refuse as ambiguous. The
module does not guess which pack should win. Commands remain available and
`quantum.status` explains refusal. Generated locations and bookmarks remain
available under their separate existing persistence policy.

Rows are `system | display name | entity name | radius`, in file order. Blank
lines and lines beginning with `#` after trimming are ignored. Limits are
1 MiB/file, 512 bytes/row, 3000 unique destinations, 31/63/95 bytes for the
system/display/entity fields; entity must be nonempty. An omitted radius means
zero; otherwise the complete value must parse as finite and nonnegative.
Embedded NUL, malformed rows or exceeded bounds refuse the entire curated
list, with no partial selection. Repeated entity names are case-insensitive:
the first row wins. Existing first display/entity lookup order is preserved.
Generated locations load afterward and do not replace a curated entity.

Content lookup checks the root/member extents, non-null methods, returned
metadata and exact copied byte count. Loading retains only destination values,
not content IDs or service pointers. Consumer reload reads the same host startup
snapshot; changed disk bytes need a host restart. No query or list discovery
authorizes relocation, quantum control or native backend activation.

The optional `sco.widgets` panel retains ID `quantum.travel` and now has title **Travel**, combining paged destination/bookmark lists, altitude, bookmark save/delete/read-only inspection and observed scan with bounded G8 controls. No host/ImGui include or renderer callback is added. The panel's Travel and Go-to-bookmark buttons use the same commands. Missing widgets leaves command-only operation.

## Build, install and focused evidence

Use the published `sdk-v2.4.0-rc.1` archive (it contains `sc_quantum.h`, `sc_game_spatial.h`, `sc_relocation.h`, `sc_gamerules.h`, `sco_widgets.h` and `sco_content.h`). The release workflow stages quantum enabled through `tools/check_quantum_sdk.py`, which builds against that archive outside the checkout, runs the module workflow below, traces compiler dependencies, runs boundary negatives and a relocation red arm (POSIX), and runs the SDK's `sco-plugin-check` with no game services. That is not a game proof.

```sh
cmake -S plugins/quantum -B build-quantum -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/include -DSCO_QUANTUM_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-quantum
ctest --test-dir build-quantum -R '^quantum-(workflow|content|gamerules)$' --output-on-failure
cmake --install build-quantum --prefix /path/to/product --component quantum
```

Installed `data/plugins/quantum/` contains the native module and enabled manifest; ordinary folder disable and `plugins=off` semantics apply. Native modules are trusted code, GPL-3.0-only.

The GameRules workflow drives the actual module/published panel with synthetic
public providers, covering cache timing and clock wrap, opaque generations,
malformed/short/null/refused reads, capability loss/recovery, current-owner
cancellation, stale callbacks across unload/reload and unchanged relocation.
The isolated helper runs all three workflows normally and with ASan/UBSan on
POSIX, retains the relocation red arm and private-dependency negatives, and
requires the GameRules workflow to fail if the unknown-policy guard is removed.
These fixtures do not exercise the game or a live GameRules reader.

The focused content workflow drives the actual module with synthetic public
snapshots: canonical/ambiguous source selection, first-entity ordering, bounds,
atomic malformed refusal, short/null service and root members, copied lifetime,
generated-file precedence and unload/reload. It does not replace the separate
real-provider integration proof or certify Windows, rendering or gameplay.

**Historical content-migration evidence (PR #165):** macOS AppleClang compiled/linked the actual module with `-Wall -Wextra -Werror`; both dynamically loaded synthetic public-provider workflows passed, including 6,015 content checks, and passed again with ASan/UBSan. The existing control/movement workflow retains legacy IDs/Travel composition, no startup writes, bounded holds, capability-loss cancellation, relocation request/tick tracking, widget routes and unload/reload checks. A separate real content-provider fixture feeds ordinary synthetic packs into the actual Quantum module; it covers copied startup bytes, parsing, ambiguity/refusal, restart and persistence compatibility without game providers.

For that earlier content slice, independent actual MSVC 19.44 compilation of the production DLL and both test executables passed with strict warnings. Both executables dynamically loaded that DLL and passed, including the 6,015 content checks, against the checksum-verified published RC1 headers. This is Windows compilation and synthetic module behavior, not game or renderer acceptance. The fixtures do not execute the real game-pack adapter, its release hook, Windows input, database transactions or engine effects. **Real renderer, storage import twice, native detours, NAV/action forwarding, owned drive/charge/audio/effect restoration, every in-game move and where `quantum.travel` actually puts you, and actual gameplay remain NOT VERIFIED; not run in game.**
