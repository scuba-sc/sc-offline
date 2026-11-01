# Standalone owned capsule fixture

This fixture is a synthetic, non-native qualification exercise against the
host-source target. It does not alter product startup. Configure and run, for
example:

```sh
cmake -S tools/offline-host -B /tmp/offline-host-build \
  -DSCO_HOST_SOURCE_DIR="$PWD/external/sco-core" -DBUILD_TESTING=ON
cmake --build /tmp/offline-host-build
ctest --test-dir /tmp/offline-host-build --output-on-failure
```

The host scenarios run in separate processes because host state is global and
terminal outcomes cannot be reset. Each constructs and roots a real owned
capsule before handing the host a synthetic image descriptor and bytes, a
separately owned mock resolver token, and an `OwnedShutdown` object. The token
owns an actual object, but no synthetic resolver callback is registered and it
is not evidence of mapped resolver code lifetime. The executable itself owns
its static callback code. No game mapping is represented. The options disable
plugins and private sessions and use external quit and DataCore hook policies;
there are no native reads, game services, scripts, builtins, host-service or
menu callbacks, runtime inspection, native hooks, or supplied barrier callback.
Restricted app startup still initializes internal UI, widgets, settings,
content, IPC and network-service bookkeeping; disabling private sessions does
not mean that these internal components are absent. The
admission inspector returns synthetic evidence only.

* `badarg` checks invalid options are refused in Idle before owner exposure and
  that all mock objects are destroyed after their local owners are released.
* `admission` and `budget` exercise actual owned Prepare failure after startup
  reservation and a successful Options copy. The pinned host closes the owner
  and enters Failed; the fixture retains its process root and checks the objects
  are still live. The budget case is not an exception during Options copy.
* `host` exercises actual Prepare, Start, wrong-thread Stop, reentrant Stop
  from the Tick inspector, and game-thread Stop. Inspector reentry is an
  adversarial contract violation; production inspectors must be non-reentrant. At core a5101d4, the last
  returns Failed and retains state even after a successful owned drain; later
  Stop and Start refuse. The fixture's root remains live for process lifetime.
* `start-fault` injects an owner fault through the inspector during the actual
  Start transition after Activate, and verifies terminal failure and retention.
* `after-app-start` uses the pinned core's existing private synthetic
  `host_testing.h` seam to inject failure after app startup. It checks the
  actual host enters Failed and retains the rooted backing. CMake registers it
  only when that source header is present and `SCO_CAPSULE_PRIVATE_SEAM` is ON;
  it is not a public or plugin API. Set the option OFF to omit it.
* `copy` is a separate synthetic output producer, not a host callback. It
  holds a real `OwnedShutdown::Read` across the entire vector copy, pauses
  after the first element, observes closure, and verifies a concurrent drain
  completes only after the remainder is copied. Destructor counters verify
  backing survives that interval and the independent output survives release.
* `copy-negative` omits the read lease, keeps the producer outside the owned
  worker census and holds backing live, and demonstrates that drain can finish
  while the copy is paused. It never accesses freed memory.

The root is installed before exposure and is intentionally not destroyed on
terminal host paths; this demonstrates a test-process policy, not a production
process/module owner. The fixture does not provide a native execution barrier,
prove real module mapping or worker census, exercise a successful game-host
Stop, or classify game-host startup exposure with a detailed result. The pinned
baseline has no such game-host-facing detailed result. The separate candidate
contract exercise below tests the later core report interface. No result authorizes native activation, teardown or restart. Product
`NativeActivationAvailable()` remains false. This test is not game acceptance.

`wait_until_closed` observes closure, not entry into the drain wait. In the
positive copy case the still-held read itself prevents drain completion at that
point; the fixture does not claim a separate drain-wait observer.

On Linux, a separate fresh-process `options_copy_fault_test` replaces ordinary
allocation operators with malloc/free-backed equivalents and arms a one-shot,
thread-local allocation failure after constructing a long `pluginRoot` and the
capsule. It intercepts ordinary scalar/array `new`, not aligned allocation,
direct `malloc`, or allocations on other threads. Against the pinned source it requires the fault to fire, real Prepare
to return Failed with a closed owner and Failed phase, no inspector calls, and
both image and resolver reference counts to remain two after caller Options
are cleared: one reference from the capsule and one from core's partially
assigned Options. These checks, together with pinned member order, identify a
partial Options assignment. If no allocation reaches the hook, CTest reports a
skip; any fired hook with a failed oracle is a test failure. This is evidence
for the tested Linux/GNU library and pinned source, not Windows, other standard
libraries, or arbitrary allocation failures. The owner-fault Start case does
not reach the post-app-start seam; the separate optional case does.

## Detailed report candidate

When `SCO_HOST_SOURCE_DIR` provides the `HostCapsule` contract and private
source testing header, CMake additionally builds `host_report_test` against
that source. It compiles a separate, test-instrumented copy of core's host
implementation, following core's own report-test pattern. The production
`sco_game_host` target is not modified. The baseline pinned source lacks the
contract, so its legacy tests remain available; this is not a gitlink update.

Each report scenario runs in a fresh process. A real backing object owns the
synthetic image bytes, shutdown owner and a resolver backing object containing
the data used by the synthetic output copy. The inspector, core resolver code
and test code are linked into the executable; no external resolver state is
exercised. It constructs an
actual immutable `HostCapsule` before entry and verifies its descriptor and
host-version copies. The tests clear all caller-owned references and check
weak references and destructor counters. Fresh unsupported configuration is
`NeverExposed` with session zero and can be destroyed; after adoption, core
retains the capsule and its real backing with a nonzero session ID.

* `reentry` checks nested detailed Start and Stop from the synthetic inspector
  refuse and preserve the adopted report. Inspector reentry is an adversarial
  test, not an allowed production inspector.
* `wrong-thread` checks Stop refuses on a different thread while Running;
  game-thread Stop then fails, closes admission, and retains dependencies.
* `failed-start` uses the private post-app-start fault to exercise terminal
  failure and repeated refusal. Successful starts use the game-host's existing
  private `app::detail::StartOwned` route, not public app startup.
* `options-copy` uses the core's fixed allocation fault armed inside the actual
  Options assignment. A long `pluginRoot` forces an ordinary allocation in the
  tested C++ library. The fault must fire after image/resolver owner fields are
  copied: reference counts show the backing, capsule Options and partially
  assigned core Options all retain the real owners. This is not proof for every
  allocator or Windows standard library.
* `copy` holds an `OwnedShutdown::Read` across a separate synthetic producer's
  final output copy while actual game-host Stop closes and drains admission.
  `copy-negative` omits the lease, holds backing alive, and demonstrates that
  Stop can finish before that copy. Neither producer is a game-host callback or
  native reader; no use-after-free or success-only barrier is introduced.

Every adopted terminal result stays `ProcessRetained`; a failed stop does not
authorize restart, destruction or unload. These tests do not establish native
reader exclusion, worker census, mapped code ownership, original-quit safety,
Windows failure behavior, or game acceptance. The detailed contract is an
unpublished host-source candidate, separate from the production SDK pin.
