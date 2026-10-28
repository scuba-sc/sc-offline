# Crew — public SDK Crew and archetype pickers

Current picker/copy-list installation and the one targeted module fixture:
[Public pickers](../../docs/pickers.md). **These sources use `sco_widgets.h` and
`sco_content.h`, which the pinned published `sdk-v2.4.0-rc.2` ships** (`sdk-v2.0.0`
did not). The obtain/proof recipes below use that archive.

External native consumer of **sdk-v2.4.0-rc.2** (SDK 2.4.0, plugin ABI 1.1,
Star Citizen game pack 0.8.0). GPL-3.0-only project code. Native plugins are
trusted process code, not a security sandbox.

## Public widget boundary

The external Crew panel uses public `sco.widgets`: target/refresh the current
ship, copied seat/occupant/usability rows, 32-seat pages, selected-seat Sit,
Stand or Remove, add one owned NPC to a selected seat, fill/stand/clear owned
crew and Flight Ready. Each action rereads public providers; per-seat removal
refuses foreign/player/NPC-plugin occupants. Manual seat words keep `crew.sit`
semantics; selected-seat actions use the fresh copied index, not ambiguous names.
Crew and NPC share SDK-only catalog parsing/filtering/paging, not session IDs.
Widgets/content ship in the pinned published SDK 2.4.0; SDK 2.0.0 predated
them. No Windows rendering/gameplay result is claimed.
There is no private ImGui/menu, spawner engine, built-in registration or fallback
in this module. The command IDs remain available through the host's typed
`sco_api.invoke` route; this is not a new console or command transport.

## Obtain, build/check, install

1. Obtain the published combined `sco-sdk-2.4.0.zip` and its adjacent release
`SHA256SUMS` from [sdk-v2.4.0-rc.2](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2) (SDK 2.4.0 release candidate 2).
   The proof verifies both the archive checksum and every extracted inventory
   entry. It never initializes the host submodule or includes host sources.
2. From the product source, run the focused archive proof (new directory outside
   the checkout):

   ```sh
   python3 tools/check_crew_sdk.py --archive <sco-sdk-2.4.0.zip> \
     --sums <release-SHA256SUMS> --work <new-outside-checkout-directory>
   ```

   POSIX developers can add `--sanitizers` for a separate ASan/UBSan loaded-module
   build and the partial-function-pointer guard negative control. Release
   dependency traces remain uninstrumented and strict; no sanitizer metadata
   exemptions are added. Windows uses Visual Studio 2026 x64; `/W4 /WX`, full
   dependency traces and strict linking are configured but not locally verified.

   To build independently after verifying/extracting the archive:

   ```sh
   cmake -S plugins/crew -B <build> -DSCO_SDK_INCLUDE_DIR=<sdk>/include
   cmake --build <build> --config Release --target sc-offline-crew
   cmake --install <build> --config Release --prefix <stage> --component crew
   ```

3. On Windows, install the staged **`data/plugins/crew/{plugin.ini,crew.dll}`**
   into the host's data root. Requires `plugins = on`; the folder is enabled by
   default, user-disableable with a `disabled` marker. Remove the old host Crew
   built-in registration/link inputs before installing: duplicate `crew` IDs
   are refused. An absent/disabled external folder must not restore a legacy
   feature tick. POSIX `.so` outputs and rewritten checker manifests are
   development evidence, not Windows game-loadable artifacts.

## Preserved commands

| Command | Behavior |
| --- | --- |
| `crew.target` | Pin the ship currently aboard; session-only, never persisted. |
| `crew.sit <seat>` | Case-insensitive words from seat name, at most 47 bytes. Seat the local player only; foreign/unknown occupants and pending crew reservations are refused. Selecting the player's current seat cancels an older queued player move. A seated NPC spawned by this plugin may be removed before the player seat job starts. |
| `crew.stand_all` | Cancel target-ship seating jobs, then eject the local player and this plugin's owned crew only. Foreign/unknown occupants are counted, not moved. |
| `crew.fill <npc>` | Caller-supplied archetype, at most 127 bytes. Spawn owned NPCs beside the current player pose and queue empty, known-usable seats. Report copied-batch truncation, skipped seats and partial spawn failure. |
| `crew.clear` | Start actors-specific removal of target-ship NPCs spawned by this plugin. Refused removals retain retry authority; accepted removal is not confirmed world disappearance. Never delete player, foreign occupants or another plugin's NPCs. |
| `crew.power_on` | Request Flight Ready for the target ship. The provider validates ship authority; a dashboard not yet streamed may refuse. Retry the command later; no private power-on fallback. |

Only `game.vehicles` 1.0 (`player_ship`, `seats`, `seat_occupant`, `seat`,
`eject`, `power_on`), `game.actors` 1.0 (`local_player`, `spawn_npc`,
`despawn`) and `teleport.spatial` 1.0 (`player_pose`) are used. Public vehicle
1.0 explicitly permits local-player seating/ejection and owned actor actions.
Services are reopened on each command/tick; complete member extents and null
callbacks are checked before use, alongside capabilities and provider results.
Missing service/capability means unavailable. No tables are cached across calls.
Provider wrong-thread results are propagated by commands; all mutations belong
on the public command/tick game-thread path. No workers, raw pointers, entity
removal service, network transport, new SDK contract or persistent storage.

## Bounded jobs, removal and reload

- At most 128 copied seats and 64 session tickets shared with local-player jobs.
  `out_more` is reported; vehicle 1.0 has no pagination cursor, so seats beyond
  the copied batch are not silently claimed as processed.
- NPC first attempt is delayed 750 ms; retry interval 1500 ms; at most five
  accepted link attempts or a 30 s seating deadline. Confirmation is checked
  before abandoning the fifth link. Jobs queued before the first tick anchor
  to that tick, not to an assumed process uptime of zero.
- Unknown/foreign occupants are never evicted. Timeout cleans only this
  plugin's NPCs; the player is never despawned. Refused removal retries remain
  bounded per tick. Accepted/not-owned removal retains a ticket until unload;
  streaming absence cannot prove disappearance, and no `alive=0` recycling is
  used. The 64-ticket lifetime budget can therefore require normal unload/reload
  before more crew are spawned; repeated `clear` never repeats accepted removal.
- `Unload` withdraws the tick subscription and resets local state, idempotently.
  It performs no game mutation during shutdown. The host withdraws commands and
  the actors provider releases remaining owned NPCs, as required by the published
  actors contract. Synthetic tests label that owner-release step a **stand-in**;
  it is not actual host/game cleanup evidence.
- Normal load/invoke/unload/load is exercised. Native faults are not certified
  safe reload/recovery; real fault cleanup and restart-required behavior remain
  owner acceptance. No unconditional reload or process-termination cleanup claim.

## Evidence and owed acceptance

The one named `crew-module-workflow` dynamically loads the actual independently
compiled module. Eight focused groups cover missing/null/physically truncated
methods, failed service reopen, typed invalid input/capability refusal, target,
Flight Ready, player sit/eject, owned fill/clear, partial seat/spawn batches,
unusable/unknown seats, no player, foreign NPCs, pending reservations, cancellation
of an older player move when already seated, fifth-link
confirmation, timeout/refused removal retry, retained ticket quota, reload and
unload twice; the same fixture also checks pre-first-tick absolute clock handling.
The archive proof checks exact enabled install inventory, two full compiler
dependency traces, published `sco-plugin-check` with absent services, and four
POSIX private-header/symbol/transitive dependency controls. Optional sanitizers
prove the old offset-only function-pointer guard fails and the restored module
passes. These are synthetic public providers, not private engine simulations.

**NOT RUN IN GAME.** Owed: actual Windows x64/MSVC exports/module loading,
small/large real seat lists, NPC streaming/removal, local-player boarding/eject,
ship-authority and Flight Ready, real host owner-release/command withdrawal,
normal disable/rebuild/reload, fault handling, offline/EAC refusal. Rendering/DPI
acceptance is still NOT RUN despite the restored public widget UI. CI, release staging in the
product, game execution and runtime networking are not performed by this change.
