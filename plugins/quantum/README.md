# Travel (`quantum`) — SDK-only G3 consumer

Retains `quantum.travel(place, altitude)`, `quantum.bookmark(name)`, `quantum.save_bookmark(name)` and `quantum.scan`. Movement commands always return `SCO_UNAVAILABLE`: G2 mutation is not approved, even if a relocation table advertises it. Added `quantum.status`, `quantum.delete_bookmark(name)` and `quantum.inspect_bookmark(name)` (read-only exact/partial candidate; never movement).

`quantum.scan` copies at most 256 observed zones, 64 per page in one callback with epoch checking. It is **not exhaustive discovery**: only provider-observed ancestry is available. Unknown system/root/name never authorize cross-system movement. Full player quaternion+position is converted into every complete copied ancestor; failures retain prior state.

Legacy destinations remain `system | display name | entity name | radius` in `locations.txt` / `locations_found.txt`. Bookmarks remain `bookmark system|name` followed by `spot x y z zone` lines. Optional following `pose x y z w` lines preserve orientation; old readers ignore them and old files default to identity. `data_root` selects the legacy data root (default `data`). Owner-scoped `quantum.db` retains its `bookmarks(pos,system,name,spots)` SQL schema and `bookmarks.txt.time` marker. Windows marker encoding remains decimal FILETIME. Import is transactional and repeated unchanged files are skipped; absent storage uses legacy text, failed database transactions do not overwrite it.

Optional retained panel provides paged destination/bookmark lists, arrival altitude, bookmark save/delete/read-only inspect and bounded scan. Its movement action is a refusal, not a hidden teleport.

## Build and install
1. Obtain/extract a core+game or combined SDK containing `sc_game_spatial.h`; immutable `sdk-v2.0.0` does not contain this new table. The product pin is not changed by this lane.
2. Configure `cmake -S plugins/quantum -B build-quantum -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/include`, then build `sc-offline-quantum`.
3. Install the quantum component into the product root (`cmake --install build-quantum --prefix /path/to/product --component quantum`). The resulting `data/plugins/quantum/` contains its native entry and manifest; enable through ordinary plugin-folder semantics. Built-in IDs/fallbacks are removed, so `plugins=off` really disables these features.

Native modules are trusted code. GPL-3.0-only. Calls run synchronously on the game thread; copied metadata is never treated as valid across commands/ticks. Optional `sco.widgets` panels avoid host/ImGui headers; absent renderer leaves command-only operation. Missing/readiness/expired tables refuse instead of opening private engine paths. Unload removes panels/hotkeys and retains databases. No unmanaged worker or tick mutation exists.

**Evidence: macOS AppleClang configure/compile/link only. Tests NOT RUN (owner speed order); Windows/MSVC, UI rendering, storage import-twice, dynamic load/reload, sanitizers/checker and game behavior NOT VERIFIED.**
