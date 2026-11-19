# TitanLink: link to a local Titanfall 2 session (optional)

TitanLink connects the game to a Titanfall 2 session running under Northstar on the same PC. The other game receives your position and view. You can call a Titan and climb in, and nothing in it moves your Star Citizen character.

This plugin is off by default and is not in the 0.8.0 release package. The folder `data/plugins/titanlink/` holds an empty `disabled` file, so it does not load until you remove that file. Native plugins run with the game's rights and are not sandboxed.

## Titanfall link page

When the plugin is enabled, the page **Titanfall link** appears under **More plugins** in the menu (key **M**). It holds **Start pilot mode (F9)**, **Call Titan (V)**, **Embark (E)** (**Disembark (E)** in a Titan) and **Close link**, and shows the match, link, frame, weapon, clip, health, range, configuration and peer log, with plain refusal text. **F9** runs `titanlink.pilot`.

## Commands

| Command | What it does |
| --- | --- |
| `titanlink.pilot` (**F9**) | Opens the link channel and, once linked, sends one request for the configured map and mode. |
| `titanlink.titan` | Calls the Titan 8 m ahead of you, at most once every 8 s. |
| `titanlink.embark` | Climbs into a parked Titan within 9 m. Disembarking is a message to the other game. |
| `titanlink.unlink` | Closes the link. |
| `titanlink.status` | Reports the state. It works with no providers. |

Your feet position and body-facing rotation, an eye height of 1.62 m, velocity, field of view, view size and held buttons go to the other game about 10 times a second. Positions and rotations in another zone are converted with the game's full-pose conversion. The link counts the other side as alive up to 3999 ms after its last heartbeat and stale from 4000 ms. A malformed frame is ignored, a damaged message ring switches the link off until it is reopened, and a malformed state stops pilot mode. A busy link, being in a Titan or an unknown position makes the command refuse.

Not available: moving the Star Citizen player, changing console variables, reading `cl_fov`, input capture, hiding your body, Titanfall-style movement in Star Citizen, NPC hits and light matching.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `game` | the usual Steam path | Titanfall 2 folder. |
| `map` | `mp_coliseum` | Local match map. Only `a-z 0-9 _`. |
| `mode` | `tdm` | Local match mode. Only `a-z 0-9 _`. |
| `extra` | empty | Northstar launch arguments. |
| `fov` | 0 | Vertical field of view, 0 to 169. 0 uses 60. |
| `launch` | true | Launch the EA app and Northstar, only on an explicit `titanlink.pilot` and only when no peer is linked. |

The EA app starts on that request, and Northstar 25 s later. Cancelling, unlinking or unloading cancels a pending launch. Nothing launches at load, and the plugin never touches account credentials, official servers or Easy Anti-Cheat. The old `data/titanlink.txt` is not imported: copy its five keys into the settings above (`sc-offline.exe plugins settings titanlink ...`) and keep the old file as a backup.

## What it needs

- Services: `sco.ipc` 1.0, `game.spatial` 1.0 (full-pose conversion), `teleport.spatial` 1.0, `sco.settings`, `sco.widgets` 1.0, `sco.ui` (F9).
- Launch, key forwarding, window placement and the overlay (a click-through DirectComposition window with a crosshair) work on Windows only. Elsewhere they report "unavailable (requires Windows)".

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build in its own tree, against an extracted SDK 2.4.1 `include/` directory that holds `sco_ipc.h` and `sc_game_spatial.h`. The wire header `sc_titanlink.h` (layout `0x314B4C54`, 64 KiB) is MIT-licensed SDK code; this plugin is GPL-3.0-only. See [docs/bridges.md](../../docs/bridges.md) for the wire and the peer fixture.

```sh
cmake -S plugins/titanlink -B build-titanlink -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-titanlink --target sc-offline-titanlink titanlink-module-test
ctest --test-dir build-titanlink -R "^titanlink-module-workflow$" --output-on-failure
cmake --install build-titanlink --component titanlink --prefix <stage>
```

The install holds the module, `plugin.ini` and `disabled`. `python tools/check_titanlink_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` checks the module from a verified SDK archive. It traces all four translation units and rejects private headers and symbols.

Files: `titanlink.cpp` (commands and link), `platform.cpp` (launch, key forwarding, window placement), `overlay.cpp` (the overlay window), `frame_valid.h` and `math.h` (frame checks and pose math). Unload joins the overlay worker before it closes the channel. A native fault needs a restart.
