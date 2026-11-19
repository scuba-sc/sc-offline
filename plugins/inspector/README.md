# Inspector: read-only view of what the game reports

Inspector shows your character's health, position, stats, inventory and the game's rules, as the game reports them. It is for finding bugs and writing plugins, and it never changes anything.

## Inspector page

The page is in the **Developer** section and appears only when **Menu settings > Show developer pages** is on. Its tabs are **Diagnostics**, **Inventory**, **Actions** and **Parameters**. **Refresh** captures diagnostics and inventory once. **Lookup**, **Browse parameters** and **Refresh action count** read action metadata when you press them. Drawing the page, switching tabs and paging do not poll the game. A captured snapshot is history, not a live view, and a failed refresh replaces the old values with "unavailable".

## Commands

Pages are numbered from zero. A reply that does not fit is refused, never cut short.

| Command | What it does |
| --- | --- |
| `inspector.read` | Logs your actor and entity ids, health, and zone-local position and rotation. |
| `inspector.refresh` | Captures diagnostics and inventory once and starts a new snapshot. |
| `inspector.status` | Shows the state of each snapshot section. |
| `inspector.stats <page>` | One captured actor stat. |
| `inspector.gamerules <page>` | One captured game rule. |
| `inspector.inventory <page>` | One captured inventory port, with totals and truncation flags. |
| `inspector.action_count` | Counts the game's actions. |
| `inspector.action <name>` | Looks up one action by exact, case-sensitive name. |
| `inspector.action_id <id>` | Looks up one action by id. |
| `inspector.params <page>` | One action definition. It shows no live value. |

Missing providers, no player, a zero zone, an invalid rotation or non-finite data show as "unknown" or "unavailable". A health maximum of zero means unknown. The build identity is what the game reports and is not checked.

## Settings

| Setting | Values | Default |
| --- | --- | --- |
| `language` | `en`, `ru` | `en` |

`ru` translates the fixed panel labels and the Lua example's command title and help. Command names, replies and diagnostics stay English. A missing or unsupported value uses English. The setting belongs to each Inspector plugin, not to the launcher. The native panel updates when the setting changes, or at the next click on a host without setting-change events. The Lua plugin needs a reload after a change.

## What it needs

- Services: `game.actors`, `game.entities`, `game.inventory`, `game.gamerules`, `game.actor_stats`, `game.build`, `game.input`, `sco.widgets` 1.0, `sco.settings`.
- Capabilities: `game.actors.local_player`, `game.actors.health`, `game.entities.transform`, `game.actor_stats.read`, `game.inventory.read` (on with `exp_inventory_read`) and `game.gamerules.read` (off in 0.8.0). Each one is optional: a missing one makes that part report "unavailable" and does not stop the plugin loading.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

The plugin is the worked example in [docs/sdk-workflows.md](../../docs/sdk-workflows.md): obtain and verify the SDK, build and check in isolation, then install. Files: `inspector.cpp` and `inspector.h` hold the reads and commands, `external.cpp` the three native exports, `plugin.ini` the manifest (`id = inspector`, version 1.0.0, plugin ABI 1.1).

```sh
cmake -S plugins -B build-plugins -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-plugins --config Release --target sc-offline-inspector
ctest --test-dir build-plugins -C Release -R inspector --output-on-failure
```

The test is `inspector-module-workflow`. `python tools/check_inspector_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive and invokes `inspector.read` with no services. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). Setting `-DSCO_SCUBA_HOSTD=<scuba-hostd>` adds `inspector_hostd_e2e`, which needs Node.js 22 or newer. `python tools/check_inspector_locale.py` checks the Russian labels.

Use `sco.reload inspector` to reload the command consumer in a running host. Do not overwrite a loaded DLL: close the game or unload the module first. A native fault needs a restart.

The optional Lua version, `inspector.lua`, reads only `game.actors` and `game.entities`. With the game closed, copy `inspector.lua` to `data/plugins/inspector_lua/` and `lua-plugin.ini` there as `plugin.ini`. It keeps the id `inspector_lua` and the command `inspector_lua.read`. `inspector_test.lua` is its stand-in test.
