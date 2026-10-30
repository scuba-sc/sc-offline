# ASOP host composition consumer

`src/asop_shell.*` consumes the game-specific public host-source
`sco::game::asop_host` interface. It is not a plugin service. The published
SDK 2.4.0 release candidate 2 (`sdk-v2.4.0-rc.2`) carries G10 in
`sco-sdk-host-source-sc-2.4.0.zip` (`include/sco/game/asop_host.h`; SDK 2.0.0 lacked it). This is the default
build path: `SCO_ASOP_HOST_COMPOSITION` defaults to ON. OFF builds the legacy in-repo
hooks, which still build their fleet from the full `ships.txt` catalogue.

## Configure

For a Windows x64 host build using a local dependency containing G10:

```sh
cmake -S . -B build -A x64 -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source -DSCO_ASOP_HOST_COMPOSITION=ON
cmake --build build --config Debug --target dinput8
```

The option refuses an older dependency. With it ON, `fleet.cpp` and `services.cpp`
leave the DLL target; product fleet entitlement/retrieve and elevator detours are
not compiled/installed. The game pack becomes the only ASOP/hangar/ATC owner.
With it OFF the unchanged pinned legacy build is retained, not a runtime fallback
from a failed G10 request. Do not install both hook owners.

The shell preserves `SC_OFFLINE_ASOP` (unset/on versus off) and
`SC_OFFLINE_ASOP_FLEET_LIST` (game versus ships). Invalid values refuse.
The owned-ship list is product policy: `asop_ships.txt` beside `ships.txt`, read once
at startup and copied after a bounded (262144-byte) read. Names are case-insensitively
unique (first spelling kept). A bad line, more than 2048 unique names, or an unreadable
file refuses the whole list atomically, and a missing file is an empty list. A file
with a pending replacement marker, or one that does not read identically twice, also
refuses startup; recovery belongs to the selection service's file adapter. On this
path `ships.txt` is never the terminal list. No session persistence claim is added. Readiness bits mean individual groups installed, not gameplay proof.
Delivery/store/retrieve/claim, lift state and pad ownership belong to the game pack.

With `SCO_ASOP_SELECTION_COMPOSITION` (default ON) the selection service owns the
saved selection document. Its fleet busy-class gate refuses a committed save that
removes a class with a stored, retrieved, delivering or queued ship, and after a
committed save the host rebuilds the owned-ship list mid-session through
`asop_host::Reconfigure` with stable fleet slots, so stored and in-transit ships keep
resolving. This mid-session rebuild is the foundation for future ship ownership flows
(buying, selling, trading), not only the ASOP terminal refresh; whether the native
terminal re-queries entitlements without a reopen is unverified.

Start occurs after the first game-thread host-service startup with positive product
offline/EAC evidence. Timing against real loading callers requires Windows acceptance;
the existing G10 contract requires startup before callers. The shell never re-installs
legacy hooks when the new owner refuses. Every window message calls the provider Pump
before the 100-ms feature throttle; never queue caller-frame continuations as plugin
next-tick tasks. Stop closes admission and wakes untaken waiters before app teardown.
Taken frames remain the provider's process-lifetime responsibility; join engine workers
before unloading the image. Stop is not restoration/hot-reload of inherited patches.

Portable compile-only target:

```sh
cmake -S tools/asop-shell -B build-asop -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source
cmake --build build-asop --target sc-offline-asop-shell
```

No G13 broker, network policy changes, anti-cheat manipulation or game data.
Windows/MSVC, real SEH/caller lifetime, engine startup/shutdown and all gameplay
acceptance NOT VERIFIED. `asop-environment-test` (tools/asop-shell) runs the environment and selection-file
policy on Windows MSVC; it is not engine evidence.
