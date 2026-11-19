# Discord: presence is off

Discord would show your game status in Discord. In 0.8.0 it does nothing of the kind: the plugin ships turned off, and even when you turn it on it sends nothing to Discord.

## What it does

The folder `data/plugins/discord/` contains an empty `disabled` file. While that file is there, the plugin does not load. If you remove it (or run `sc-offline.exe plugins enable discord`), the plugin loads and adds one command:

- `discord.status` always answers "unavailable" and says why: Discord presence needs an approved way to reach Discord and an approved list of what is sent, and neither exists yet.

The plugin opens no pipe, socket or shared memory, reads no game service, and sends nothing. It has no menu page and no key. Enabling it cannot turn presence on.

## Why it is off

The old version opened Discord's local pipe directly, outside the SDK's rules for host-mediated transport. It sent your process id, session start time, game version, and, depending on settings, your location and ship class. Discord can publish that to other users, so "stays on this PC" would have been false. The SDK's `sco.ipc` is owner-scoped shared memory, and `sco.net` is private co-presence. Neither reaches Discord. Presence can return only with an approved transport, an approved list of sent fields and a consent step. See [docs/decisions.md](../../docs/decisions.md).

## Settings

Three settings keep their old names and are inactive. They default to off, and turning one on changes nothing but the text `discord.status` prints.

| Setting | Default |
| --- | --- |
| `show_location` | off |
| `show_ship` | off |
| `show_buttons` | off |

Change them with `sc-offline.exe plugins settings discord show_ship=true` or on the launcher's Plugins page.

## What it needs

Only `sco.settings`, read each time `discord.status` runs. If it is missing, the three settings read as off.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory.

```sh
cmake -S plugins/discord -B build-discord -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-discord --config Release --target sc-offline-discord
ctest --test-dir build-discord -C Release --output-on-failure
cmake --install build-discord --config Release --prefix <stage> --component discord
```

The install copies `plugin.ini`, the module and the `disabled` file. The test is `discord-module-workflow`. `python tools/check_discord_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs it from a verified SDK archive and runs the SDK's `sco-plugin-check --invoke discord.status` with no services. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). The checker loads the folder even though it is disabled, so it does not show that the host honours the marker.
