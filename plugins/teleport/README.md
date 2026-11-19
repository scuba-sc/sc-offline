# Teleport: save a spot and go back to it

Teleport saves where you stand and takes you back to it. Press **F7** to save, **F8** to go back.

## Saved spots page

Open **Saved spots** in the **Travel** section of the menu (key **M**). It has three sections, in the order you use them:

- **Save where you stand**: **Save this spot** (**F7**).
- **Go back to it**: **Go to saved spot** (**F8**). It is disabled until a spot is saved.
- **Status**: what happened, in plain words.

The key hints show only when the plugin could bind F7 and F8. The page keeps one quick-save slot, with no list and no delete button. Named spots are on the Travel page of the [quantum plugin](../quantum/README.md).

## Commands

| Command | Key | What it does |
| --- | --- | --- |
| `teleport.save` | F7 | Saves your position, rotation and zone ancestry. It only reads. |
| `teleport.go` | F8 | Moves you to the saved spot. |
| `teleport.inspect` | | Checks whether the saved spot can be reached, without moving. A spot that is stale or matches only the root zone is refused. |
| `teleport.status` | | Says whether a spot is saved, whether moving is allowed and whether a move is running. |

## How a move works

1. The plugin setting `relocation` must be on. The capability `game.relocation` is checked fresh.
2. The saved spot is matched to a live zone and a position inside that zone. An unloaded, expired or root-only match refuses.
3. One request (zone id, local position, rotation, 30 s timeout) goes to `game.relocation`. One move runs at a time.
4. Every tick the plugin reads your position. When you are within 2 m of the target it says "You are at your saved spot.", counts the move as finished and cancels the Core request. Cancelling cannot undo a move that already started. A later timeout of that request is logged and does not change the message. If you never come within 2 m, Core's own result is reported: arrived, or failed or cancelled with a reason (timeout, session or player changed, offline admission withdrawn, different star system, the game did not start the move, provider fault).
5. A new F8 while you are already within 2 m of the spot answers that you are there.

Moving works only inside your current star system, offline, with a live player and a known star system. Moving while seated in a ship is not blocked and has not been tried.

F8 can go only to a zone the game still streams. The game resolves your own zone and the larger places around it, and has no lookup by name for other zones. A saved zone is stored with its id and its name; going back tries the id first (accepted only if the game still streams that zone under the same name), then the name. Otherwise F8 refuses with a plain sentence.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `relocation` | on | Allow moving you (offline only). Off makes `teleport.go` refuse. Saving and checking still work. |
| `data_root` | empty | Folder for the saved spot. Empty means the folder of the launcher's `SC_OFFLINE_SPAWN_FILE`, else `data`. It is created when needed. |
| `spawn_file` | empty | Where the spot is kept when there is no database. Empty means `SC_OFFLINE_SPAWN_FILE`, else `<data_root>/spawn.txt`. |

The host setting `relocation = on|off` in `sc-offline.ini` applies to all plugins and cannot bypass offline admission.

The spot is saved in `data/storage/teleport.db`. An existing `spawn.txt` (lines `spot x y z zone`, plus `pose x y z w`) is imported once each time its file time changes. Without `sco.storage`, F7 writes the spawn file instead.

## What it needs

- Services: `game.relocation` 1.0, `game.spatial` 1.0, `teleport.spatial` 1.0, `sco.storage`, `sco.settings`, `sco.ui` (the keys), `sco.widgets` 1.0 (the page; without it the commands still work).
- Capability: `game.relocation` (on in Core).

Seen in game (owner, build 4.10.200.58746): F7 then F8 really moved the player to the spot, but Core did not confirm the arrival. The 2 m arrival message is new. Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_relocation.h` and `sc_game_spatial.h`.

```sh
cmake -S plugins/teleport -B build-teleport -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_TRAVEL_TEST=ON
cmake --build build-teleport --config Release
ctest --test-dir build-teleport -C Release --output-on-failure
cmake --install build-teleport --config Release --prefix <stage> --component teleport
```

The test is `travel-model`. `python tools/check_teleport_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive, traces compiler dependencies and runs the SDK's `sco-plugin-check` with no game services. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)).

The module shares its travel code with the quantum plugin (`plugins/spatial/`). Calls run on the game thread, and metadata copied from the game is never reused across commands or ticks. Unload removes the page, the keys and the tick subscription, and `game.relocation` cancels and forgets this plugin's requests.
