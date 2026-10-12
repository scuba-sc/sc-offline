# Loadout and Squadron 42 — external development plugin

`loadout` 1.0.0 is a C++20 SDK consumer with three native exports, no product
headers/ImGui globals, private engine calls, XML loader or gameplay fallback.
It is **compile-only development work; not run in game**. The existing published
`sdk-v2.0.0` pin is unchanged and does **not** contain its G6/G7/content/widgets
requirements. Do not substitute this local build for a published-release proof.

## Obtain, build, install

1. Obtain an approved newer combined core+game SDK containing `sc_loadout.h`,
   `sc_cvars.h`, `sco_content.h` and `sco_widgets.h`, plus `sco_api.h` and the
   existing `sc_creative.h`. Use its extracted public `include/` directory and
   checksum inventory, never the host checkout's headers. The wave-2 local
   integration archive supplies these headers for development only.
2. Build separately from the Windows host:

   ```sh
   cmake -S plugins/loadout -B <plugin-build> \
     -DSCO_SDK_INCLUDE_DIR=<sdk>/include \
     -DSCO_LOADOUT_BASE_LIST_DIR=<product-data-directory>
   cmake --build <plugin-build> --config Release --target sc-offline-loadout
   cmake --install <plugin-build> --config Release --prefix <stage> --component loadout
   ```

   `SCO_LOADOUT_BASE_LIST_DIR` is optional; when supplied it stages existing
   `items.txt`, `outfits.txt` and `buildables.txt` unchanged into a separate
   `loadout_lists` data pack. It does not grant runtime filesystem access.
   A larger standalone `plugins/` build uses `SCO_LOADOUT_FEATURE=ON`; that switch
   defaults off until approved SDK/pin/release promotion. Runtime folder metadata
   itself is enabled, user-disableable, with no `disabled` marker.
3. On Windows x64/MSVC, install the staged `data/plugins/loadout/` module and
   manifest, and the optional `data/plugins/loadout_lists/` pack, while the host
   is stopped; enable `plugins = on`. macOS/Linux modules are development
   artifacts, not Windows-loadable game DLLs. No tag/release is authorized here.

The newer host must publish `game.loadout` 1.0, `game.cvars` 1.0 and `sco.content`
1.0. Missing/unready services leave commands available with explicit refusal.
Optional `sco.widgets` 1.0 adds renderer-neutral Player/SQ42 panels; without it
use commands. Actual product renderer integration remains a separate dependency.
Native code is trusted, not sandboxed. Commands/tick/widget callbacks use only the
public game-thread routes, reopen tables, and guard complete pointer extents/nulls.

## Commands and selections

| Command | Arguments / behavior |
|---|---|
| `loadout.equip` | Item-class string, separated by spaces/commas; resolve case-insensitively from copied `items.txt`, at most one item per logical slot |
| `loadout.items` | Filter string; list first 12 matches with original slot labels and unsupported markers |
| `loadout.reload_lists` | Re-read the public startup snapshot; no filesystem refresh; changed packs require host restart |
| `loadout.spoilers` | Bool; session-only SQ42 confirmation, reset on unload/reload |
| `loadout.wear` | Exact outfit label, case-insensitive; requires spoiler confirmation |
| `loadout.visor` | Explicit visor item-class string; typed logical VISOR request, no implicit face/lens/default replacement |
| `loadout.cvars` | Refresh and report four real public observations; failed/missing/invalid reads are `unknown/unavailable`, never silently off |
| `loadout.cvar_set` | Name string, bool; **always refused** |
| `loadout.console` | Command string; **always refused** |
| `loadout.spawn` | Class string, height float, seat string, power bool; requires spoilers; invokes external `spawn.ship_options` |
| `loadout.build` | Object string, ahead float; requires spoilers; invokes `build.place` |

Game-thread external shortcuts preserve the provider result/reply. This is not
proof of eventual gameplay/streaming completion. Missing,
disabled or refusing spawn/build/creative plugins have no private fallback.
The Player panel invokes optional creative commands and displays the public
provider's requested-state flags, not an invented engine-state observation.
SQ42 ship labels/reference classes are retained; enemy-wing orchestration remains
deferred rather than silently spawning an ordinary ship as a substitute.

## Copied list formats and refusal

Read only `SCO_CONTENT_LIST` entries named `lists/items.txt`, `lists/outfits.txt`
and optional `lists/buildables.txt`, in public discovery order. Legacy top-level
base files are not read privately; stage them into `loadout_lists` or supply an
enabled data pack. Duplicate packs remain ordered, but ambiguous item-slot or
outfit-name selections refuse. No item-existence guess is made while parsing;
the typed provider reports missing items at apply time.

- Items retain `[slot]` sections, comments introduced by `#`, and one ASCII
  `[A-Za-z0-9_]` class per line, 1–95 bytes. Bounds: 3000 items.
- Outfits retain `[name]` sections and `port class` lines, original labels and
  all pieces (including unsupported ports). Bounds: 256 outfits, 32 pieces each.
- Build shortcuts retain the `[sq42]` group from the copied buildables list.
- Reject malformed sections/classes, embedded NULs, oversized lines/records and
  snapshots beyond 1 MiB combined. Parsing is atomic; failed explicit reload
  invalidates old picks rather than displaying them as a current read.

Consumer mappings select logical UNDERSUIT, HEAD (helmet), TORSO, ARMS, LEGS,
BACKPACK and VISOR. These are **not certified engine port mappings**. Unknown
legacy sections/ports remain visible; any selected unknown port refuses the
whole request before mutation. Duplicate logical slots or more than 16 pieces
also refuse before mutation; batches are never silently split. Weapons, ammo,
grenades, clothing, body/head/eyes/hair/accessories/material variants, implicit
magazine/default face/undersuit/medpen synthesis and old empty-slot clearing need
approved G6 extensions/verified mappings. Most legacy outfits therefore refuse;
this is not full feature-parity acceptance. The inactive original sources remain
under `legacy/loadout` for reference, not as a runtime escape hatch.

A supported typed batch reports input-ordered applied/unsupported/item-missing/
failed/unavailable results. Partial success is not rollback or whole-outfit
success. Never retry applied slots automatically. Unspecified slots stay
unchanged; successful equip remains player state on ordinary unload, not a
plugin-owned spawned entity. Invalid/inconsistent reports leave state unknown
and refuse further automatic work. Unload removes panels/subscription and
clears loaded-session picker/spoiler state; host-owned command lifetime applies.

## SQ42 policy and quantum boundary

The exact four bool observations are:

- `i_target_selector.targeting2_enabled`
- `pl_lensdisplay.minimap_enabled`
- `pl_lensdisplay.greebles_enabled`
- `g_squadron_frontend`

Controls are disabled/read-only even with valid values. The module never opens
`game.cvars_write`, invokes console, writes `USER.cfg`, or restores/mutates a
cvar on unload. Forged write widget events also refuse. Failed reads clear prior
values to unknown. Tick-driven observations are capped at 5 Hz; explicit refresh
reads on request. The game's own values may change independently.

**Step 4C / G8 quantum control is NOT approved.** This lane does not change the
existing private boost/keeper sources, their startup/ticks or hook ownership.
Migration to public quantum control remains pending separate approval.

## Evidence and pending acceptance

The touched native plugin configures, compiles and links against the local
integration SDK. **Canonical suites, checker/archive/dependency proofs, reviews,
Windows host/MSVC/DLL calling conventions, renderer, runtime reload/cleanup and
actual gameplay NOT RUN** by owner speed order. A follow-up focused ad-hoc
probe loaded the actual native module against synthetic public cvar callbacks:
four exact-name 0/1 reads, absent/failed reads invalidating prior values,
write/console refusal without querying a write service, synchronous external
provider result/reply preservation, and unload twice passed. This is not host
loader/gameplay evidence; partial batches, parsing and unsupported-slot behavior
were not dynamically exercised. Production G6/G7 adapters are unverified/fail-closed; four actual Windows game
values, outfits/head/visor mapping and no duplication/corruption remain owner
acceptance work. SDK publication/pin/release promotion is also pending.
