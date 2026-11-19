# Loadout: the Gear and Squadron 42 pages

Loadout puts armour, weapons and tools on your character from the **Gear** page of the in-game menu. The **Squadron 42** page, behind a spoiler warning, wears Squadron 42 outfits and shows four game settings.

The 0.8.0 release package includes this plugin, enabled, with the `loadout_lists` pack it reads. `tools/release-plugin-profiles.json` lists it in the `player-offline` profile.

## What the Gear page can equip

Equipping goes through Core's background route (`game.loadout` 1.2, capability `game.loadout.async`). The launcher switch `exp_loadout_async` turns that capability on, and it is on in the package.

| Part | Available in 0.8.0 |
| --- | --- |
| Undersuit, helmet, torso, arms, legs, backpack | Yes, from the gear list (`items.txt`). |
| Primary weapon and sidearm | Yes. |
| Optic, barrel and underbarrel for each weapon; tool attachment, canister and battery for each multi-tool | Yes. The page picks the part. Core decides whether it fits the weapon you hold. |
| Visor | Only through the command `loadout.visor`, which needs the synchronous route. That route is off, so it refuses. |
| Clothing (torso layers, legs, feet, hands), hat, head accessory, head horn, jewellery | No. |
| Hair, hair colour, eyebrow, teeth, eyes, head, body | No. Core has no slot for them. |
| Ammo and grenades | No. The page does not offer items it cannot map to a slot. |

The clothing row needs a second Core capability. Core 2.4.1 defines outfit slots 22 to 31 in `sc_loadout.h`: clothing torso 0, 1 and 2, clothing legs, feet and hands, hat, head accessory, head horn and jewellery. They exist only on the background route, and the capability `game.loadout.outfit` is on only when `SCO_LOADOUT_ASYNC=1` and `SCO_LOADOUT_OUTFIT=1` are both set. The launcher does not set `SCO_LOADOUT_OUTFIT` and has no switch for it, so the capability stays off. The plugin checks it each time the page is drawn and each time a request is sent. With it off, no clothing slot is offered and Core would refuse the whole request anyway.

The Squadron 42 page lists only outfits whose pieces all go into slots the page can set. With the capability off, those are outfits made of armour pieces only. Of the 35 outfits in `data/outfits.txt`, 8 qualify (four Navy pilot flight suits, two Navy pilot light armours, Navy pilot Fleet Week and Navy gunner). The page says how many outfits it leaves out. An outfit never goes on partly: one piece with no slot, two pieces in one slot, or more than 16 pieces keeps the whole outfit off the list.

## Gear page

Open **Gear** in the **Character** section of the menu (key **M**). The sections follow the order you use them:

- **Choose gear**: **Search gear**, the list of gear, page buttons, **Refresh gear list**, your current selection, **Equip selected gear** and **Clear selection**. Search ignores case and takes up to 63 printable ASCII characters.
- **Weapons and tools**: one column for the primary weapon or tool and one for the sidearm or secondary tool. Each column has **Type**, **Find weapon** and **Find attachment** filters, then weapon, optic, barrel, underbarrel, tool attachment, canister and battery. Every choice starts at **Keep current**. Then **Equip chosen weapons**, **Clear choices** and **Refresh weapon list**. A selected item stays visible when the search does not match it. Changing the type so that it excludes the selected weapon clears that column. Choosing a different weapon clears that column's attachments. Replacing a weapon does not keep its attachments, and spawned defaults are up to the game.
- **Progress**: the status of the equip request, **Check progress** and **Cancel equipping**. A finished request stays in Core until released, so the page releases it for you: after the last report says finished, the button reads **Clear result**, and pressing an equip button first releases the finished request and then starts the new one. A request still in progress is never released that way.
- When the background route is off, the page says "Changing gear isn't turned on." and shows no equip button.

Equipping is not an undo. Cancelling stops pending parts, and parts already attached stay on. A batch can end partly applied. The page reports each part and never retries applied parts.

Names on the page are readable forms of the game's class names. The request carries the exact class. Search still matches the class name.

## Squadron 42 page

Press **Show spoilers** first. The page then offers:

- **Outfit**: search, list, page buttons, **Wear selected outfit** and progress, as above.
- **Game settings**: four settings shown as on or off lines when the game lets them be read (`i_target_selector.targeting2_enabled`, `pl_lensdisplay.minimap_enabled`, `pl_lensdisplay.greebles_enabled`, `g_squadron_frontend`). Otherwise one line says they cannot be read yet. They cannot be changed from the menu, and the plugin never writes a console variable, runs a console command or edits `USER.cfg`.
- **Ships** and **Objects**: **Spawn ship** and a place button that call the Ships & vehicles and Build plugins (`spawn.ship_options`, `build.place`). If that plugin is missing, the page shows one line instead of a button. SQ42 enemy wings are not spawned.

God mode, noclip and infinite ammo belong to the Creative page, not here.

## Commands

| Command | What it does |
| --- | --- |
| `loadout.submit_gear <items>` | Equips gear from `items.txt`, separated by spaces or commas, at most one item per slot. The Gear page button uses this. |
| `loadout.submit_outfit <outfit>` | Wears an outfit by its exact name, ignoring case. Needs the spoiler confirmation. |
| `loadout.submit <slot=Class,...>` | Equips named weapon or tool slots, for example `primary=SomeWeaponClass,primary_optic=SomeOpticClass`. Up to 2047 bytes. The classes are yours to supply. |
| `loadout.progress`, `loadout.cancel` | Show or cancel the one retained request. |
| `loadout.spoilers <on:bool>` | Confirms Squadron 42 spoilers for this session. |
| `loadout.items <filter>` | Lists the first 12 matching gear items. |
| `loadout.reload_lists`, `loadout.reload_slots` | Re-read the startup copy of the lists. Changed files need a game restart. |
| `loadout.cvars` | Reads the four settings above. |
| `loadout.spawn <class> <height> <seat> <power>`, `loadout.build <object> <ahead>` | The Squadron 42 shortcuts. Need the spoiler confirmation. |
| `loadout.equip`, `loadout.wear`, `loadout.slots`, `loadout.visor` | Synchronous equivalents. They need `game.loadout.apply` and `game.loadout.weapons`, which are off, so they refuse. |
| `loadout.cvar_set`, `loadout.console` | Always refuse. |

Slot names for `loadout.submit`, case-sensitive: `primary`, `primary_optic`, `primary_barrel`, `primary_underbarrel`, `primary_tool_module`, `primary_tool_canister`, `primary_tool_battery`, and the same seven with `sidearm_`. A class is 1 to 95 characters of `A-Z a-z 0-9 _`. An unknown slot, a duplicate slot, an empty class or `None` refuses the whole batch before anything changes. Parents are applied before attachments. Omitting a parent targets the weapon you already hold. There is no remove operation.

## Lists and settings

The plugin has no settings of its own. It reads these lists through `sco.content`, from enabled data packs, and never opens files itself. Each file is at most 1 MiB, and a change needs a game restart.

- `lists/items.txt`: `[slot]` sections (`undersuit`, `helmet`, `torso`, `arms`, `legs`, `backpack`, `primary`, `sidearm`) with one class per line. Up to 3000 items.
- `lists/outfits.txt`: `[name]` sections with `port class` lines. Up to 256 outfits and 32 pieces per outfit.
- `lists/buildables.txt`: only for the Squadron 42 Objects shortcut. The `build_catalog` pack supplies it.

Each list path may come from one enabled pack only. Two packs supplying the same path refuse the whole snapshot. A missing list leaves its feature empty with a plain message. A malformed list refuses entirely.

### Curated weapon and tool selectors

The weapon and tool choices come from an optional `lists/loadout_slots.txt`. Without it the selectors stay empty and show only **Keep current**.

The source copy is `data/loadout_slots.txt`. It keeps its attribution for the attachment names and the primary and sidearm split, taken from a public GPL-3.0-only community change. It holds 344 choices: 101 primary weapons, 31 sidearms, and for each weapon 53 optics, 35 barrels, 10 underbarrel parts, 5 tool attachments, 2 canisters and 1 battery. They are requests only: the game decides whether a class exists and fits the weapon you hold. The plugin never guesses a slot from a class name and adds no default tool parts.

To use the file, copy it with the game closed to `data/plugins/my_loadout/lists/loadout_slots.txt` and add `data/plugins/my_loadout/plugin.ini` holding `id = my_loadout`, `name = My loadout slots`, `version = 1.0.0`, `api = 1.1` and `kind = data`. Two packs supplying the file refuse as ambiguous. The release staging does not copy it.

The file has one section per slot, named exactly like the 14 slot names above (case-sensitive), with one class per line (1 to 95 characters of `A-Z a-z 0-9 _`). `#` starts a comment. Limits: 1 MiB, 255 bytes per line, 255 classes per section, 3000 in all. A duplicate section, an unknown section, a malformed line or `None` refuses the whole file. An empty file is valid.

## What it needs

- Services: `game.loadout` 1.2, `sco.content` 1.0, `sco.widgets` 1.0, and `game.cvars` 1.0 for the settings readout.
- Capabilities: `game.loadout.async` (needed for every equip button), `game.loadout.outfit` (optional, clothing slots) and `game.cvars.read` (optional, settings readout; the `exp_cvars_native` switch turns it on).
- Missing services leave the commands in place and they refuse with a plain reason. There is no private fallback.

Seen in game (owner, build 4.10.200.58746): equipping gear on the Gear page attached the items. Not run in game. Everything else on this page, including weapon attachments, outfits and the Squadron 42 page. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK `include/` directory, which must hold `sc_loadout.h`, `sc_cvars.h`, `sco_content.h` and `sco_widgets.h`. The 0.8.0 line uses SDK 2.4.1.

```sh
cmake -S plugins/loadout -B build-loadout -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-loadout --config Release --target sc-offline-loadout
ctest --test-dir build-loadout -C Release --output-on-failure
cmake --install build-loadout --config Release --prefix <stage> --component loadout
```

`-DSCO_LOADOUT_BASE_LIST_DIR=<data folder>` also stages `items.txt` and `outfits.txt` unchanged as a `loadout_lists` data pack (Python 3 required). It refuses to overwrite a different existing file. `-DSCO_LOADOUT_STAGE_BUILDABLES=ON` adds `buildables.txt` to that pack; use it only when no other enabled pack supplies that list. The aggregate `plugins/` build compiles this plugin only with `SCO_LOADOUT_FEATURE=ON`.

The tests are `loadout-widget-workflow`, `loadout-public-route`, `loadout-curated-catalogue`, `loadout-weapon-picker`, `loadout-outfit-recipe` and `loadout-outfit-import`. They run the real module against stand-in services. `python tools/check_loadout_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs them from a verified SDK archive; add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). Its raw result includes two `NOT_FOUND` observations from the SDK's generic checker for `loadout.progress` and `loadout.cancel` with no request. The helper accepts exactly those two.

To stage a release-style folder with this plugin, run `python -B tools/stage_loadout_candidate.py --artifact-root <absolute artifact root> --staging-parent <absolute existing folder outside any game install>`. It creates a new `sco-loadout-candidate-*` folder, refuses to replace files and refuses a dirty checkout. Add `--lists-source <folder>` to include caller-supplied `items.txt` and `outfits.txt`.

Known gap to close in `loadout.cpp`: it maps outfit ports to slots 22 to 34 and names hair, hair colour and eyebrow slots plus the capability `game.loadout.appearance`. Core 2.4.1 has slots 22 to 31 only, with head accessory 29, head horn 30 and jewellery 31, and no hair slots or `game.loadout.appearance`. Against that header the source does not compile (a `static_assert` names `SC_LOADOUT_SLOT_HAIR`). Even after it compiles, head accessory, head horn and jewellery would be sent as slots 32 to 34, which Core reports as unsupported. The product is not affected today because `game.loadout.outfit` stays off.

`outfit_import.cpp` and `outfit_recipe.cpp` keep the 0.7.7 outfit file parser and recipe model with a source-audit trail. Nothing in the module calls them, and their two standalone tests do not show any game behavior.
