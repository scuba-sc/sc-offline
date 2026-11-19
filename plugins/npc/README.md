# NPCs: characters from the game

NPCs puts a character from the game next to you and removes the ones you added. You pick from a list on the **Characters** page of the in-game menu.

## Characters page

Open **Characters** in the **People** section of the menu (key **M**). The sections follow the order you use them:

- **Choose a character**: a search box, a paged list, **Reload character list** and a field for a typed character name.
- **Spawn them**: **How many** (1 to 10) and **Spawn characters**.
- **Characters you spawned**: a choice, **Remove character**, **Remove all my characters** and **Check on them**.
- **Last result**.

The plugin has no hotkey.

## Commands

| Command | What it does |
| --- | --- |
| `npc.spawn <archetype> <count>` | Adds 1 to 10 characters near you. The ids are logged at once as "pending", because the characters stream in later. |
| `npc.observe` | Logs how many are pending, visible, being removed and unknown, and the position of each visible one. Read-only. |
| `npc.remove <id>` | Asks for removal of one character this plugin added. |
| `npc.clear` | Asks for removal of all characters this plugin added. It never touches crew or characters from other plugins. |

Use an archetype name that exists in your game build. Spawning needs a valid player position in a streamed zone, and the characters appear at that position with no ground check. A batch that fails partway reports how many were added and keeps those.

Removal "accepted" means removal started, not that the character is gone. After the game accepts it, the plugin forgets the character and does not ask twice. A refused removal is retried later. The plugin keeps at most 1024 records per session, and records of characters being removed count until the plugin unloads, so after very many spawns you may have to restart the game. Unloading the plugin asks for removal of the rest once, and Core removes any that remain. Nothing is saved between sessions.

Characters come from `lists/npcs.txt` in the `picker_catalog` data pack (a copy of `data/npcs.txt`), up to 127 bytes per name. To use your own list, copy it over that pack's `lists/npcs.txt` with the game closed and keep a backup. A change needs a game restart. See [docs/pickers.md](../../docs/pickers.md).

## Settings

None.

## What it needs

- Services: `game.actors` 1.0, `game.entities` 1.0, `teleport.spatial` 1.0, `sco.widgets` 1.0, `sco.content` 1.0.
- Capabilities: `game.actors.spawn_npc`, `game.actors.despawn`, `game.entities.transform`.

Run in game (maintainer, build 4.10.200.58746): spawning a character worked. Not run in game. Removal, observe and the page. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sco_widgets.h` and `sco_content.h`. The plugin is a worked example in [docs/sdk-workflows.md](../../docs/sdk-workflows.md).

```sh
cmake -S plugins -B build-plugins -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-plugins --config Release --target sc-offline-npc
ctest --test-dir build-plugins -C Release -R npc --output-on-failure
```

The test is `npc-module-workflow`. `python tools/check_npc_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive and invokes `npc.observe` with no services. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). The picker fixture is described in [docs/pickers.md](../../docs/pickers.md).

Files: `npc.cpp` and `npc.h` hold the commands, `picker.inc` the page, `external.cpp` the three native exports, `plugin.ini` the manifest (`id = npc`, version 1.1.0, plugin ABI 1.1). Services are reopened on every command. Use `sco.reload npc` to reload in a running host, never overwrite a loaded DLL, and restart after a native fault.
