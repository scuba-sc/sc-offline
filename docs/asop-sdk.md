# ASOP host composition consumer

The product currently refuses native startup pending a proven reader/worker
exclusion and drain. ASOP compositions are unavailable too: both
`SCO_ASOP_HOST_COMPOSITION` and `SCO_ASOP_SELECTION_COMPOSITION` default to OFF,
and CMake rejects either option until the product has a reviewed nonblocking
stop request and ordered control-thread drain. The selected core pin
`6106fa3639cbcc28f78a1ded8de2707de019f4d3` returns `Unavailable` from ASOP
host Start before installing hooks, and its exposed `Stop` is synchronous.
The core's direct required quit hook calls `BeforeQuit -> StopImpl` without
dispatching `app::Stop` or `game.exit`, so a `game.exit` subscription alone
cannot close ASOP on every quit path.
Neither the pin nor this product enables ASOP gameplay.

`src/asop_shell.*` consumes the game-specific public host-source
`sco::game::asop_host` interface. It is not a plugin service. The published
stable SDK 2.4.0 release (`sdk-v2.4.0`) carries G10 in
`sco-sdk-host-source-sc-2.4.0.zip` (`include/sco/game/asop_host.h`; SDK 2.0.0
lacked it). Header availability alone is insufficient to enable this consumer;
the missing lifecycle contract is the current blocker.

## Current configure status

There is no supported configure command for either ASOP composition. Setting
`SCO_ASOP_HOST_COMPOSITION=ON` or `SCO_ASOP_SELECTION_COMPOSITION=ON` fails
configuration with an explanatory error. The default build leaves both off.
This keeps synchronous provider Stop callbacks out of the game message hook.

Re-enabling the host composition requires a core-owned nonblocking close request,
an owner report for taken callbacks/work, and a control-thread drain/finalization
order that keeps providers live until ASOP teardown completes. Re-enabling the
selection composition also requires preserving its game-thread finalization
contract. See the lifecycle handoff before changing these gates. Do not install
both hook owners.

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

The shell and provider sources remain scaffold material only. Do not call their
synchronous `Stop` from a hook, runtime callback, or start-failure handler. A
future ordered coordinator must close admission without waiting, keep callbacks
and providers alive, then finalize on the required thread and drain accepted
work. A refusal or exception is not evidence that partial installation can be
unloaded. `AlreadyStarted` is not a new installation failure.

Portable compile-only target:

```sh
cmake -S tools/asop-shell -B build-asop -DSCO_HOST_SOURCE_DIR=/path/to/approved-host-source
cmake --build build-asop --target sc-offline-asop-shell
```

No G13 broker, network policy changes, anti-cheat manipulation or game data.
Windows/MSVC, real SEH/caller lifetime, engine startup/shutdown and all gameplay
acceptance NOT VERIFIED. Portable tests do not establish native behavior.
