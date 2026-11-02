# Gear presets

Capture a complete logical layout, give it a name, save it in the host's owner-scoped storage, and compare it with a fresh observation. Delete one saved preset with the picker button or `gear_presets.delete <id>`; `gear_presets.list` shows IDs. Deletion frees a slot without resetting the next ID. If storage reports a deletion error, refresh before retrying because the outcome may be unknown. The picker and commands use the public SDK; no game memory, item equip, move, input or mission mutation is used. Comparison is category-level advice, never exact class or item-instance identity. Storage ports are retained as layout context and never generate gear actions. Incomplete or unavailable observations cannot be saved or treated as empty.

The published stable SDK 2.4.0 defines `game.inventory.layout` over `game.inventory` 1.2, including the required stable keys and roles. The current product refuses native startup, so capture is unavailable there. The synthetic host fixture is development evidence, not a claim about the game or an installed launcher. This module is not staged for players.

Build the standalone module using a checksum-verified extracted SDK containing the 1.2 layout contract:

```sh
cmake -S plugins/gear_presets -B build/gear-presets \
  -DSCO_SDK_INCLUDE_DIR=/path/to/extracted-sdk/include -DSCO_GEAR_PRESETS_TESTS=ON
cmake --build build/gear-presets
ctest --test-dir build/gear-presets --output-on-failure
```

For the host-only SQLite and widget fixtures, additionally provide `SCO_CORE_HOST_TEST_ROOT` and `SCO_CORE_HOST_BUILD_DIR` from the same source as the SDK. These fixtures link a synthetic host; they do not ship a backend. Windows/MSVC and actual target-host/game acceptance remain separate.

The earlier `44ffd129` candidate host omitted the layout header. Published stable
source `6106fa3639cbcc28f78a1ded8de2707de019f4d3` includes it, so the syntax
check can now see the development-only adapter. The standalone configure still
requires the matching SDK archive and refuses an older SDK. CI runs the model,
storage and actual-module fixtures against a checksum-verified SDK and matching
Core source. This verifies contract compatibility only; the current product
startup refusal means there is no active native producer or runtime capture.
