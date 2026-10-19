# ASOP ship selection

An external `asop_selector` plugin, built only against the published SDK2.3.
It reads copied `lists/ships.txt` content, renders bounded checkbox pages through
`sco.widgets`, and saves a chosen class subset through `sco.storage`. It has no
game hooks, raw memory access, filesystem globals or entitlement behavior.

Save affects the **next game start only**. The optional product G10 host-source
composition reads the plugin's committed raw snapshot after plugin loading and
before ASOP startup. Missing plugin/no saved selection keeps the configured
catalogue. Invalid, unavailable or out-of-catalogue saved data refuses startup;
an explicitly saved empty subset stays empty. Failed saves keep the previous
committed snapshot. Selection cannot live-reconfigure an installed ASOP owner.

Build separately with `SCO_SDK_INCLUDE_DIR` pointing to a checksum-verified
published plugin archive. `SCO_WORKFLOW_TESTS=ON` adds the actual-DLL synthetic
workflow; install component `asop_selector` stages its folder. Install the copied
`picker_catalog` data pack too. Optional G10 composition remains OFF by default;
without it this plugin stores preferences but does not filter terminals.
No current-game filtering or game verification is claimed.
