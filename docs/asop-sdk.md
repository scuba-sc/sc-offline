# ASOP host composition consumer

`src/asop_shell.*` consumes the game-specific public host-source
`sco::game::asop_host` interface. It is not a plugin service. The published
SDK 2.0.0 pin is unchanged and lacks G10, so this staged cutover is opt-in.

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
`ShipsFilePath` remains product policy; the catalogue is read once, bounded and
copied, with duplicate/invalid classes refused atomically. No session persistence
claim is added. Readiness bits mean individual groups installed, not gameplay proof.
Delivery/store/retrieve/claim, lift state and pad ownership belong to the game pack.

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
acceptance NOT VERIFIED. Tests NOT RUN under the owner's speed order.
