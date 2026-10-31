# ASOP host composition consumer

The product currently refuses native startup pending a proven reader/worker
exclusion and drain. The opt-in composition below is not active in this build.
In addition, core `a5101d406a9ddbff14e437408686ffb04266be18` itself returns
`Unavailable` from enabled ASOP Start until a safe phase is established. With
`SCO_ASOP_HOST_COMPOSITION=ON`, the legacy fleet/services implementation is
excluded at build time; there is no runtime fallback when that owner refuses.
The coordinated pin does not restore ASOP gameplay.

`src/asop_shell.*` consumes the game-specific public host-source
`sco::game::asop_host` interface. It is not a plugin service. The published
SDK 2.4.0 release candidate 2 (`sdk-v2.4.0-rc.2`) carries G10 in
`sco-sdk-host-source-sc-2.4.0.zip` (`include/sco/game/asop_host.h`; SDK 2.0.0 lacked it). This staged cutover
remains opt-in: `SCO_ASOP_HOST_COMPOSITION` defaults to OFF.

## Configure

For a Windows x64 host build using a local dependency containing G10:

```sh
cmake -S . -B build -A x64 -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source -DSCO_ASOP_HOST_COMPOSITION=ON
cmake --build build --config Debug --target dinput8
```

The option refuses an older dependency. With it ON, `fleet.cpp` and `services.cpp`
leave the DLL target; product fleet entitlement/retrieve and elevator detours are
not compiled/installed. The game pack becomes the only ASOP/hangar/ATC owner.
With it OFF the legacy build is retained, not a runtime fallback
from a failed G10 request. Do not install both hook owners.

The legacy path now encodes a never-reused session ID in each synthetic entitlement,
not the current row position. IDs and immutable historical class names survive reorder,
removal and re-addition; only currently issued IDs decode from terminal URNs. The 8192
historical-class limit refuses new classes rather than recycling an ID. This is identity
hardening only: the legacy catalogue still builds once, and no selection editor,
live-rebuild trigger, buffer-retirement policy or SDK cutover is introduced.
The coordinated development core currently refuses native `Reconfigure` with
`Unavailable`. Saving a selection is not evidence that a running terminal has
changed; this consumer does not call `Reconfigure` or claim a live rebuild.
`python3 tools/test_fleet_identity.py` compiles the actual legacy build/mapping/retrieve
bodies under ASan/UBSan with synthetic engine reads and platform stand-ins. It does not
exercise Windows locks/SEH, native hooks or game behavior.

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

If the provider returns `Unavailable` or throws a C++ exception from Start, the
product immediately calls Stop and reports `Unavailable`. Stop closes custom admission while retaining
installed hooks and backing for process lifetime; it is not rollback. Core must
also close admission as part of its own failed installation path: the product
cannot prevent callers entering while Start is still executing. This protection
does not establish native-reader exclusion or make a partial installation safe to
unload. A repeated `AlreadyStarted` response is not a new installation failure
and does not stop the existing owner.

Portable compile-only target:

```sh
cmake -S tools/asop-shell -B build-asop -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source
cmake --build build-asop --target sc-offline-asop-shell
```

No G13 broker, network policy changes, anti-cheat manipulation or game data.
Windows/MSVC, real SEH/caller lifetime, engine startup/shutdown and all gameplay
acceptance NOT VERIFIED. Portable tests do not establish native behavior.
