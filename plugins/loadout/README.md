# Loadout and Squadron 42 — external development plugin

`loadout` 1.0.0 is a C++20 SDK consumer with three native exports, no product
headers/ImGui globals, private engine calls, XML loader or gameplay fallback.
It is **development work; not run in game**. Its G6/G7/content/widgets
headers ship in the pinned published `sdk-v2.4.0-rc.1` (`sdk-v2.0.0` lacked them);
the release workflow does not yet stage loadout. Do not substitute this local build for a published-release proof.

## Obtain, build, install

1. Obtain the pinned published combined core+game SDK containing `sc_loadout.h`,
   `sc_cvars.h`, `sco_content.h` and `sco_widgets.h`, plus `sco_api.h` and the
   existing `sc_creative.h`. Use its extracted public `include/` directory and
   checksum inventory, never the host checkout's headers.
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
   defaults off until release staging is approved (the pinned sdk-v2.4.0-rc.1 ships
   the headers). Runtime folder metadata
   itself is enabled, user-disableable, with no `disabled` marker.
3. On Windows x64/MSVC, install the staged `data/plugins/loadout/` module and
   manifest, and the optional `data/plugins/loadout_lists/` pack, while the host
   is stopped; enable `plugins = on`. macOS/Linux modules are development
   artifacts, not Windows-loadable game DLLs. No tag/release is authorized here.

The copied-list routes use `game.loadout` 1.0, `game.cvars` 1.0 and `sco.content`
1.0. Explicit weapon/tool slots require loadout 1.1 for synchronous apply or 1.2
for jobs. Missing/unready services leave commands available with explicit refusal.
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
| `loadout.slots` | Explicit comma-separated `slot=Class` assignments, at most one per named public weapon/tool slot; synchronous 1.1 apply |
| `loadout.submit` | The same explicit assignments; submit one retained 1.2 job, without synchronous fallback |
| `loadout.progress` | Inspect the retained job's completed prefix and per-slot results; does not advance it |
| `loadout.cancel` | Cancel/release the retained job; confirmed equipment remains equipped |
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

### Manual weapon and tool slots

The development Player panel has a manual assignment field (255 bytes) and apply,
submit, progress and cancel buttons. Commands accept up to 2047 bytes. For example,
`primary=KnownWeaponClass,primary_optic=KnownOpticClass` selects a parent and optic;
the names here are syntax examples, not an item catalogue. The caller must supply
known classes; only the provider can report whether they exist or can be equipped.
This route does not require copied lists or infer a class's slot from those lists.

The case-sensitive slot names are the published logical vocabulary:

| Parent | Weapon | Attachments | Multitool |
|---|---|---|---|
| Primary | `primary` | `primary_optic`, `primary_barrel`, `primary_underbarrel` | `primary_tool_module`, `primary_tool_canister`, `primary_tool_battery` |
| Sidearm | `sidearm` | `sidearm_optic`, `sidearm_barrel`, `sidearm_underbarrel` | `sidearm_tool_module`, `sidearm_tool_canister`, `sidearm_tool_battery` |

Classes require 1–95 ASCII `[A-Za-z0-9_]` bytes. Unknown slots, duplicate slots,
empty classes, `None` (case-insensitive) and malformed assignments refuse the
whole batch before mutation. Whitespace around names/classes is ignored. The
consumer orders assignments by public slot number so both parent weapons precede
attachments. Omitting a parent targets its currently equipped item. Replacing a
parent does not preserve attachments; spawned defaults are engine-defined. There
is no remove/clear operation and no promise to preserve an SRT tool's built-in
configuration. Existing ambiguous legacy catalogue/outfit ports remain unsupported.

Synchronous apply queries 1.1 and requires `game.loadout.apply` plus
`game.loadout.weapons`. Job submission queries 1.2, checks complete non-null
submit/status/cancel members and requires **`game.loadout.async`**, independently
of those synchronous capabilities. No route falls back to a lower version or
another mutation method. Table publication alone does not enable a backend.

One job is retained until explicit cancellation/release, including after terminal
status. While retained, another submit or synchronous equip refuses. Progress
reports only completed slots: pending is not equip confirmation, and complete
does not mean every slot applied. Failed/partial results are displayed without
retry or rollback. Status/cancel remain usable after mutation-capability revocation.
Unload attempts cancellation while the owner is live, then clears local state;
if the service has withdrawn or cancellation refuses, host owner-unload/stop is
the remaining cleanup boundary. No confirmed equipment is removed on unload.

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
also refuse before mutation; batches are never silently split. Legacy weapon, ammo,
grenades, clothing, body/head/eyes/hair/accessories/material variants, implicit
magazine/default face/undersuit/medpen synthesis and old empty-slot clearing need
separate consumer mappings, additional public contract coverage where absent,
and validation. Copied-list/outfit selections still query 1.0 and use the
synchronous armor/visor subset. The separate manual route uses the published
1.1/1.2 contracts without assigning meanings to old catalogue ports. Public
contract availability does not verify native equip or attachment behavior.
Most legacy outfits therefore refuse; this is not full feature-parity acceptance.
The inactive original sources remain
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

The standalone plugin and focused widget test configure, compile and link with
LLVM-MinGW against the checksum-verified published `sdk-v2.4.0-rc.1` combined
archive (`sco-sdk-2.4.0.zip`, SHA-256
`124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b`).
The synthetic public-service workflow covers an items-only snapshot with no
outfits/buildables, populated pickers, an empty gear filter, and unload cleanup.
The items-only case failed before the empty-array correction and passes after
it. Enable this development-only test with `-DSCO_WORKFLOW_TESTS=ON`, then run:

```sh
cmake --build <plugin-build> --config Release --target loadout-widget-test
ctest --test-dir <plugin-build> -C Release -R '^loadout-widget-workflow$' --output-on-failure
```

These tests link the feature source
against public headers and synthetic callbacks; they do not load the native DLL
or exercise the real widgets service/renderer.

The manual route's `loadout-public-route` test also uses the exact published RC1
headers. It covers all 14 public weapon/tool slots, parent-first ordering, input
refusals, old/missing providers, physically truncated/null members, independent
async capability, copied submit buffers, partial/malformed reports, retained jobs,
provider withdrawal, cancellation after capability revocation and unload/reload.
It failed on the unchanged consumer because the new command was absent. Run both
focused workflows with `ctest --test-dir <plugin-build> -C Release --output-on-failure`.
This is consumer behavior against synthetic providers, not native slot mapping,
Windows/MSVC ABI, actual async resource cleanup, renderer or game acceptance.

**Canonical suites, loadout checker/dependency proofs, Windows host/MSVC/DLL
calling conventions, renderer, runtime reload/cleanup and actual gameplay
NOT RUN.** A prior focused ad-hoc
probe loaded the actual native module against synthetic public cvar callbacks:
four exact-name 0/1 reads, absent/failed reads invalidating prior values,
write/console refusal without querying a write service, synchronous external
provider result/reply preservation, and unload twice passed. This is not host
loader/gameplay evidence; that earlier probe did not exercise partial batches or
list parsing. Production G6/G7 adapters are unverified/fail-closed; four actual Windows game
values, outfits/head/visor mapping and no duplication/corruption remain owner
acceptance work. Release staging is also pending (SDK 2.4.0 release candidate 1 is published and pinned).
