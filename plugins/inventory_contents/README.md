# Stored inventory contents

SDK-only read consumer for the optional `game.inventory.contents` 1.0 service.
Command: `inventory_contents.read <session-local-container-id>`.

Build standalone with `SCO_SDK_INCLUDE_DIR` pointing at an extracted candidate
SDK whose checksum inventory is present. The published product SDK pin remains
unchanged; this plugin is not staged into official release archives yet.
It depends only on the public SDK headers, never the host checkout.

Stored inventory is not equipped item ports. Unknown containers return NOT_FOUND;
an absent native adapter/capability returns UNAVAILABLE, never a fabricated empty
inventory. Results include count, total_seen, generation, volume-known and truncation.
No movement, equipment mutation, native offsets, hooks or private fallback exist.

The matching scuba-engine candidate implements this exact ABI with synthetic
containers 289 (two items) and 290 (empty), allowing real DLL load/read/reload
tests without the game. Synthetic results do not establish native container layouts.
Production adapter and in-game verification remain pending.
