# Natural mining — external SDK consumer

`mining` 1.0.0 owns `mining.status` and two declared bool settings: `enabled`
(requested natural mining) and `debug` (copied trace counters), both **false**.
The module is discoverable by default so off/unavailable diagnostics remain usable;
`plugins = off` prevents loading. No built-in, raw hook/signature read, environment
switch, private admission check or fallback remains in the product.

The public `game.mining` 1.0 table belongs to the game pack. It accepts owner-scoped
intent, copies counters/refusal state and withdraws each owner's request on unload.
A successful request is **not** proof the hook installed or mining is active. The
provider alone owns `mining.cell`, eligibility and fresh offline/EAC/network/target
admission. Faults latch restart-required; process-retained hooks pass through after
stop/unload. This conversion's native adapter is **unavailable** until Windows
engine behavior and admission collectors are verified. Earlier third-party gameplay
does not verify this implementation. No engine mutation is enabled by import.

## Deliberate legacy settings import

Host ini `mining`/`mining_debug` and `SC_OFFLINE_MINING`/`SC_OFFLINE_MINING_DEBUG` no
longer control mining. An old ini produces a launcher warning, not implicit import.
Use the launcher Plugins page to set values, or deliberately import old preferences
with Python 3 while **both launcher and game are closed**:

```sh
python data/plugins/mining/import_settings.py --ini sc-offline.ini --data-root data
python data/plugins/mining/import_settings.py --ini sc-offline.ini --data-root data --apply
```

The first command is a no-write preview. `--environment` instead of `--ini` explicitly
chooses the current process's two legacy variables. Invalid or duplicate keys refuse
before opening the DB. `--apply` stores typed `bool:true`/`bool:false` values in
`data/storage/mining.db`, matching the host's `sco.settings.enabled` and
`sco.settings.debug` format. Missing source keys are not imported. Existing settings
are **preserved** on repeated imports; only explicit `--replace` overwrites those two
keys. Unrelated keys and legacy source files are untouched. Symlinked storage/DBs
refuse. The importer never runs at startup and is not a plugin-side write service.

## Build and distribution

Configure this folder alone against an extracted core + game SDK that publishes
`sc_mining.h` (published sdk-v2.0.0 is too old):

```sh
cmake -S plugins/mining -B mining-build -DSCO_SDK_INCLUDE_DIR=/path/to/sdk/include -DSCO_WORKFLOW_TESTS=ON
cmake --build mining-build --target sc-offline-mining mining-module-test
ctest --test-dir mining-build -R '^mining-workflow$' --output-on-failure
cmake --install mining-build --prefix stage --component mining
```

The aggregate plugins build opts in with `SCO_MINING_FEATURE=ON`; the module, manifest
and standalone importer install under `data/plugins/mining`. CI/release staging names
mining explicitly, but must refuse the currently pinned old SDK until the contract
is published and a separate authorized pin promotion occurs. This change does not
change the published pin or download an unpublished archive in CI.

`tools/check_mining_sdk.py` builds a separate copied consumer against a checksum-
verified archive and runs one synthetic workflow. The fixture loads the actual module
and tests settings, absent/short/null/refused services, lifecycle, off/unavailable
status, throttled trace and deliberate import. Portable fixture success is not
Windows/MSVC/SEH, native hook, launcher UI or in-game acceptance.
