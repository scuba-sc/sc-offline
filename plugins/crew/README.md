# Crew: AI crew for your ship

Crew fills your ship's seats with AI characters and seats you. You manage them from the **Crew** page of the in-game menu.

## Crew page

Open **Crew** in the **People** section of the menu (key **M**). The sections follow the order you use them:

- **Your ship**: **Use my current ship**, **Get ship ready to fly**, **Update seat list**, a seat choice and a table of who sits where (32 seats a page).
- **Choose a character to add**: a search box, a paged list and a field for a typed character name.
- **Add crew**: **Fill all empty seats** and **Add to selected seat**.
- **You and your crew**: **Sit in selected seat**, **Stand up person in seat**, **Remove crew in seat**, **Stand everyone up** and **Remove all my crew**.
- **Last result**.

The seat buttons wait until you choose a seat. Removing one seat's crew refuses if someone else sits there: you, a character from another plugin or a character you did not add. When the plugin cannot add more characters this session, the add buttons are disabled and the page says a game restart is needed. The plugin has no hotkey.

## Commands

| Command | What it does |
| --- | --- |
| `crew.target` | Pins the ship you are in. Kept for this session only. |
| `crew.sit <seat>` | Seats you. The words you give must all appear in the seat name (up to 47 bytes, ignoring case). Refuses seats held by others and seats with a pending crew reservation. |
| `crew.stand_all` | Stops seating, then stands up you and the crew this plugin added. Others are counted, not moved. |
| `crew.fill <npc>` | Adds characters of the archetype you name (up to 127 bytes) and seats them in empty, usable seats. Reports skipped seats and partial failures. |
| `crew.clear` | Removes every character this plugin added. It needs no ship, so it works after you leave. A removal that was refused is retried. |
| `crew.power_on` | Asks for Flight Ready on the pinned ship. A dashboard that has not streamed in yet may refuse; try again later. |

Characters come from `lists/npcs.txt` in the `picker_catalog` data pack (a copy of `data/npcs.txt`). To use your own list, copy it over that pack's `lists/npcs.txt` with the game closed and keep a backup. A change needs a game restart. See [docs/pickers.md](../../docs/pickers.md).

Limits. The plugin reads at most 128 seats per ship and keeps 64 records per session, and removal records count too. After 64 you must restart the game before adding more crew. A new character waits 750 ms before its first seating attempt, retries every 1500 ms, and gives up after five accepted attempts or 30 s. On timeout only this plugin's characters are removed. Nobody else is ever moved out of a seat, and you are never removed. Unloading the plugin releases its characters through the game; Core removes any that remain.

## Settings

None.

## What it needs

- Services: `game.vehicles` 1.0, `game.actors` 1.0, `teleport.spatial` 1.0, `sco.widgets` 1.0, `sco.content` 1.0.
- Capabilities: `game.vehicles.seats`, `game.vehicles.seat`, `game.vehicles.flight_ready`, `game.actors.local_player`, `game.actors.spawn_npc`, `game.actors.despawn`.
- A missing service or capability makes the action refuse with a reason.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sco_widgets.h` and `sco_content.h`.

```sh
cmake -S plugins/crew -B build-crew -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-crew --config Release --target sc-offline-crew
ctest --test-dir build-crew -C Release --output-on-failure
cmake --install build-crew --config Release --prefix <stage> --component crew
```

The test is `crew-module-workflow`. `python tools/check_crew_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive. Add `--sanitizers` on POSIX, and `--candidate` with the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)).

Services are reopened on every command and tick, and complete table extents and null members are checked first. All mutations run on the game thread. A native fault means a restart, not a reload.
