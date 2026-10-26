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
| `loadout.reload_slots` | Re-read exactly one public `lists/loadout_slots.txt` startup snapshot; reset curated picks on success or refusal; no disk refresh |
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

### Curated weapon and tool selectors

The Player panel offers fourteen independent selectors using the same public
logical slot names as the manual route below. Every selector starts at
**Unchanged (omit slot)**. Selecting a class adds only that explicit assignment.
Apply uses `loadout.slots`' synchronous 1.1 path; Submit uses `loadout.submit`'s
1.2 job path. Both keep the same parent-first ordering, version/capability gates,
per-slot partial results and retained-job/cancel policy. Clear curated selection
changes only local picks; it does not clear equipment or cancel a retained job.
The manual assignment field remains a separate selection route.

Install one ordinary `kind = data` pack containing `lists/loadout_slots.txt`.
For example, while the host is stopped, explicitly copy the optional source
`data/loadout_slots.txt` to `data/plugins/my_loadout/lists/loadout_slots.txt` and
create `data/plugins/my_loadout/plugin.ini` with:

```ini
id = my_loadout
name = My curated logical slots
version = 1.0.0
api = 1.1
kind = data
```

Enable ordinary plugin loading and restart the host. This file is not staged by
the Loadout install component, including when `SCO_LOADOUT_BASE_LIST_DIR` is set.
There is no automatic top-level-file import. Content is copied at host startup;
`loadout.reload_slots` re-reads that snapshot and cannot see disk edits until
restart. More than one exact canonical List is ambiguous and refuses rather
than choosing a pack. Missing, disabled or invalid packs expose no choices and
an explicit unavailable message; the manual route remains independent.

The format has exact case-sensitive section names from the fourteen-slot table
below, followed by ASCII `[A-Za-z0-9_]` classes, 1–95 bytes each. `#` introduces
comments; blank lines and surrounding whitespace are ignored. Parsing is atomic:
at most 1 MiB, 255 bytes per input line, fourteen distinct known sections,
255 classes per section and 3000 classes total. Duplicate sections, duplicate
case-insensitive classes in one section, unknown sections, malformed records,
embedded NULs and `None` refuse the whole catalogue. Explicit choices of the
same class in different target sections remain independent. An empty catalogue
is valid. Each combo's first unchanged option plus its classes fits the public
256-option limit. Reload success or failure clears previous curated selections;
failed panel replacement pauses old Player actions, and publication cookies
reject callbacks from older publications or loaded sessions.

The optional source catalogue contains **344 target choices**: 101 primary,
31 sidearm, and, for each parent, 53 optics, 35 barrels, 10 underbarrel choices,
5 tool modules, 2 tool canisters and 1 tool battery. Primary/sidearm identifiers come unchanged from
[the existing project list at `9b2b71f`](https://github.com/scuba-sc/sc-offline/blob/9b2b71f6916bd40129ed89965d1a2585c799e520/data/items.txt).
The attachment/tool identifiers and their independent primary/sidearm selector
classification come from Alpha-Rome0's public GPL-3.0-only
[PR #127 at `7ea9731`](https://github.com/scuba-sc/sc-offline/pull/127/commits/7ea9731b693bb9042ccbb721099ca88acb80d6f4).
The file retains source attribution. These are logical target requests, not a
verified compatibility matrix: the native provider must decide whether a class
exists and fits the currently equipped parent. No class spelling is used to
infer a native port, no SRT defaults or battery are synthesized, and no installed
game files or community-site data were copied for this catalogue.

For manual research, searchable references include
[Cornerstone's Universal Item Finder](https://finder.cstone.space/) and
[Star Citizen Wiki's Pyro RYT Multi-Tool page](https://starcitizen.tools/Pyro_RYT_Multi-Tool).
They are navigation references, not runtime inputs or proof for an installed
build's compatibility. Historical game-acceptance statements on PR127 do not
validate this SDK consumer.

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
synchronous armor/visor subset. The separate manual and curated-selector routes use the published
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

Loadout does not own quantum control. This lane changes no Quantum commands,
providers, startup/ticks or hook ownership; that consumer's source and native
acceptance remain separate from the gear selectors.

## Evidence and pending acceptance

The consumer proof uses the checksum-verified published `sdk-v2.4.0-rc.1` combined
archive (`sco-sdk-2.4.0.zip`, SHA-256
`124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b`).
Enable the three development workflows with `-DSCO_WORKFLOW_TESTS=ON`, then run:

```sh
cmake --build <plugin-build> --config Release
ctest --test-dir <plugin-build> -C Release --output-on-failure
```

`loadout-widget-workflow` and `loadout-public-route` link the feature source to
synthetic public services. They cover copied legacy lists, empty panels, all
fourteen manual slots, ordering, version/capability/extent guards, partial and
invalid reports, retained jobs and cleanup after capability revocation.
`loadout-curated-catalogue` loads the actual native module and exercises the
catalogue, copied selectors, existing apply/job paths, exact limits, ambiguity,
failed publication, stale callbacks, unload/reload and the attributed seed file.

`tools/check_loadout_sdk.py` copies only public consumer/test inputs into an
isolated directory, verifies the archive, builds/runs all three workflows,
checks the installation inventory and full compiler dependencies, and on POSIX
runs ASan/UBSan plus missing-selector and four private-dependency negatives.
The published RC1 generic checker has a separate known limitation: it treats
the existing argument-free `loadout.progress` and `loadout.cancel` no-job
`NOT_FOUND` observations as failures. Its raw **FAILED: 2 failure(s)** result is
retained. The helper narrowly recognizes those exact two messages and rejects
other failures; it does not call that a clean checker pass or change the
published checker or product return values.

The build workflow defines separate `loadout-sdk-posix` and `loadout-sdk`
validation jobs for this helper. They upload no artifacts and are outside the
production release-folder matrix; validation does not add Loadout to releases.

Separate source-review evidence records actual MSVC 19.44 x64 strict compilation
of five translation units, linked workflow runs and actual-DLL catalogue tests.
The same new fixture fails on the unchanged old DLL because selectors are
absent, then passes on the new DLL. A separate verified RC1 host-content and
headless widgets-renderer harness covers real discovery, copied panels and
event admission with an authored synthetic typed equip backend. These are
source, ABI and host integration evidence, **not run in game**.

Production G6/G7 adapters remain unverified/fail-closed. Native class/parent/port
compatibility, actual async resource cleanup, game rendering, SRT parent-default
behavior, outfits/head/visor mapping and duplication/corruption checks remain
game acceptance work. Release staging, tagging/publication, backend defaults and
SDK pin adoption are separate decisions.
