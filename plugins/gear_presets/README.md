# Gear presets

Capture a complete logical layout, give it a name, save it in the host's owner-scoped storage, and compare it with a fresh observation. The picker and commands use the public SDK; no game memory, item equip, move, input or mission mutation is used. Comparison is category-level advice, never exact class or item-instance identity. Storage ports are retained as layout context and never generate gear actions. Incomplete or unavailable observations cannot be saved or treated as empty.

The layout source must supply stable keys and roles under `game.inventory` 1.2 and advertise `game.inventory.layout`. The current native product has no such producer, so capture is unavailable there. The synthetic host fixture is development evidence, not a claim about the game or an installed launcher. No plugin staging or embedded SDK pin change is made by this directory.

Build the standalone module using a checksum-verified extracted SDK containing the 1.2 layout contract:

```sh
cmake -S plugins/gear_presets -B build/gear-presets \
  -DSCO_SDK_INCLUDE_DIR=/path/to/extracted-sdk/include -DSCO_GEAR_PRESETS_TESTS=ON
cmake --build build/gear-presets
ctest --test-dir build/gear-presets --output-on-failure
```

For the host-only SQLite and widget fixtures, additionally provide `SCO_CORE_HOST_TEST_ROOT` and `SCO_CORE_HOST_BUILD_DIR` from the same source as the SDK. These fixtures link a synthetic host; they do not ship a backend. Windows/MSVC and actual target-host/game acceptance remain separate.
