# Mining: natural mining switch and status

Mining asks the game pack to turn on natural mining and reports whether it is on. In 0.8.0 it cannot mine: the game pack's mining adapter is not available, so asking changes nothing.

## What it does

The plugin adds one command, `mining.status`, which reports whether mining is requested, whether it is active, why not if it is blocked, and the game pack's counters (ecosystem cells seen, cells promoted and read faults). It has no menu page and no key.

A successful request is not proof that mining runs. The game pack decides whether mining is allowed, and it re-checks offline, anti-cheat, network and target admission itself. A fault latches until the game restarts. The plugin withdraws its request when it unloads.

## Settings

Both default to off. Change them on the launcher's Plugins page or with `sc-offline.exe plugins settings mining enabled=true`.

| Setting | Meaning |
| --- | --- |
| `enabled` | Request natural mining. |
| `debug` | Write a trace line with the counters to `mod.log` when the cell count changes, at most once per second. |

The old `mining` and `mining_debug` keys in `sc-offline.ini` and the `SC_OFFLINE_MINING` and `SC_OFFLINE_MINING_DEBUG` variables no longer do anything. An old ini produces a launcher warning and is not imported. To carry old values over, close the launcher and the game, then run:

```sh
python data/plugins/mining/import_settings.py --ini sc-offline.ini --data-root data
python data/plugins/mining/import_settings.py --ini sc-offline.ini --data-root data --apply
```

The first command is a preview and writes nothing. `--environment` instead of `--ini` reads the two variables from your current shell. `--apply` stores the values in `data/storage/mining.db`. Existing settings are kept unless you add `--replace`. The importer refuses an invalid or duplicate key and a symlinked database, and it never runs at startup.

## What it needs

- Services: `game.mining` 1.0 and `sco.settings`.
- The game pack's mining adapter is not available in 0.8.0, so `mining.status` reports "unavailable: native engine bridge not verified" when mining is requested. With `enabled` off it reports "off". Without `game.mining` it reports the service as unavailable.

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_mining.h`.

```sh
cmake -S plugins/mining -B build-mining -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-mining --config Release --target sc-offline-mining mining-module-test
ctest --test-dir build-mining -C Release -R "^mining-workflow$" --output-on-failure
cmake --install build-mining --config Release --prefix <stage> --component mining
```

The install puts the module, `plugin.ini`, `import_settings.py` and this file in `data/plugins/mining/`. The aggregate `plugins/` build compiles the plugin only with `SCO_MINING_FEATURE=ON`. `python tools/check_mining_sdk.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder outside the checkout>` runs the workflow from a verified SDK archive. Add `--candidate` and the digest options for an unpublished SDK (see [docs/sdk-candidate.md](../../docs/sdk-candidate.md)). The workflow loads the real module and covers the settings, missing or refusing services, the lifecycle, the status text, the throttled trace and the import.
