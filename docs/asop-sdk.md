# ASOP host composition

This page is about the Core-owned ASOP host (`sco::game::asop_host`), which is not in 0.8.0. The ship terminals, hangars and ATC in [Features](features.md#ship-terminals-hangars-and-atc) are a different thing: the mod's own code (`src/fleet.cpp` and `src/hooks.cpp`), which is compiled into both game DLLs and on by default (`asop = on`). It has not been run in game on this build. Nothing on this page has been run in game either; see [status.md](status.md).

Both CMake options `SCO_ASOP_HOST_COMPOSITION` and `SCO_ASOP_SELECTION_COMPOSITION` default to `OFF`, and CMake rejects either one with an error. They stay rejected until the product has a reviewed, non-blocking stop request and an ordered control-thread drain. This is separate from native startup, which is on with a Core that provides the process-retained composition ([host composition](host-composition.md)).

The Core at the `external/sco-core` gitlink still returns `Unavailable` from the ASOP host `Start` before it installs any hook, even when validation and admission pass. Invalid or disabled configurations can return other results. Its `Stop` is synchronous. The legacy quit detour requests owner closure and then forwards the original call on the same stack, without draining the owner. The separate `ForwardQuit` helper calls `BeforeQuit`, which runs `Stop`. Neither path shows engine-object lifetime or a safe native drain. See [SDK coordinates](sdk-candidate.md) for the Core version.

`src/asop_shell.*` consumes the game-specific public host-source interface `sco::game::asop_host`. It is not a plugin service. The published SDK 2.4.0 carries it in `sco-sdk-host-source-sc-2.4.0.zip` (`include/sco/game/asop_host.h`). Having the header does not enable the consumer. The missing lifecycle contract is the blocker.

## Configure status

There is no supported configure command for either composition. Setting either option to `ON` fails configuration with an explanation. The default build leaves both off, which keeps synchronous provider Stop callbacks out of the game message hook.

To re-enable the host composition, Core needs a non-blocking close request, an owner report for taken callbacks and work, and a control-thread drain and finalization order that keeps providers live until ASOP teardown completes. The selection composition also needs its game-thread finalization contract kept. Do not install both hook owners.

## The shell

`SC_OFFLINE_ASOP` (unset or on, versus off) and `SC_OFFLINE_ASOP_FLEET_LIST` (`game` or `ships`) are kept. Invalid values are refused. `ShipsFilePath` stays product policy. The catalogue is read once, bounded and copied, and duplicate or invalid classes refuse the whole read. Readiness bits mean individual groups installed, not that gameplay works. Delivery, store, retrieve, claim, lift state and pad ownership belong to the game pack.

The legacy path writes a never-reused session ID into each synthetic entitlement, not the current row position. IDs and immutable historical class names survive reorder, removal and re-addition. Only IDs currently issued decode from terminal URNs. A limit of 8192 historical classes refuses new classes rather than reusing an ID. This hardens identity only: the legacy catalogue still builds once, and there is no selection editor, live-rebuild trigger, buffer-retirement policy or SDK cutover.

Core refuses native `Reconfigure` with `Unavailable`. A saved selection does not show that a running terminal changed. The optional selection-file adapter keeps the newest committed selection notice pending until `Reconfigure` returns `Ok`. A parse failure, an exception or a result that is not `Ok` leaves the notice for a retry and emits a deferred-apply message. An older in-flight apply cannot clear a newer notice. The product's ASOP option check refuses before the adapter starts, so none of this is active.

`python3 tools/test_fleet_identity.py` compiles the legacy build, mapping and retrieve code under ASan and UBSan with synthetic engine reads and platform stand-ins. It does not exercise Windows locks, structured exceptions, native hooks or the game.

The shell and provider sources are scaffolding. Do not call their synchronous `Stop` from a hook, a runtime callback or a start-failure handler. A future coordinator must close admission without waiting, keep callbacks and providers alive, then finalize on the required thread and drain accepted work. A refusal or an exception does not show that a partial installation can be unloaded. `AlreadyStarted` is not a new installation failure.

## Portable compile target

```sh
cmake -S tools/asop-shell -B build-asop -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source
cmake --build build-asop --target sc-offline-asop-shell
```

This is a compile check only. There is no private-session broker, no network policy change, no anti-cheat manipulation and no game data. Windows and MSVC behavior, real structured exceptions, caller lifetime and engine startup and shutdown are not verified.
