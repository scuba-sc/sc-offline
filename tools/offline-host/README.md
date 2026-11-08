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
  adversarial contract violation; production inspectors must be non-reentrant.
  At the current pinned core source `6106fa3`, the last returns Failed and
  retains state even after a successful owned drain; later Stop and Start
  refuse. The fixture's root remains live for process lifetime.
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
prove real module mapping or worker census, or exercise a successful game-host
Stop. The published pinned source provides a detailed game-host result, and the
report fixture below checks its synthetic ownership behavior. No result
authorizes native activation, teardown or restart. Product
`NativeActivationAvailable()` remains false. This test is not game acceptance.

`wait_until_closed` observes closure, not entry into the drain wait. In the
positive copy case the still-held read itself prevents drain completion at that
point; the fixture does not claim a separate drain-wait observer.

For older source packages without `HostCapsule`, Linux has a legacy
`options_copy_fault_test` fallback. It replaces ordinary allocation operators
with malloc/free-backed equivalents and arms a one-shot thread-local allocation
failure during the old Options copy. This fallback is not registered for the
current stable pin. Stable 2.4.0 instead runs `host_report_options-copy`, which
injects a failure during the actual partial capsule/Options copy and verifies
that the owning backing remains retained. It is synthetic Linux evidence, not
Windows allocator evidence or a native reader barrier.

## Published stable host report qualification

The current product pin and published SDK source are core commit
`6106fa3639cbcc28f78a1ded8de2707de019f4d3`, tag
[`sdk-v2.4.0`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0).
The host-source archive SHA-256 is
`a0aaaba07e3d4eb0d8c264912cd6f2ab23f0d237a99fa07816005790a76efc4c`; the
core-source archive SHA-256 is
`df756719da0de54ead18973258d3bc35640e26d44088b3b6ffcf8672ebb1ee51`. The
The PR-specific check and the manual `qualify_stable_sdk` dispatch download the
official release assets, check listed files against `SHA256SUMS`, check package
hashes and sizes against `release.json`, verify the release tag peels to the
product source pin, and run this fixture against both the exact source checkout
and the extracted published host-source archive on Linux and Windows. These
package checks do not change the product pin.

When `SCO_HOST_SOURCE_DIR` provides the `HostCapsule` contract and private
source testing header, CMake additionally builds `host_report_test` against
that source. It compiles a separate, test-instrumented copy of core's host
implementation, following core's own report-test pattern. The production
`sco_game_host` target is not modified. The current stable source includes this
contract; legacy refusal tests remain available alongside detailed report tests.

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
* `retained-reap` is registered only when the source provides
  `reap_retained_until`. An actual owned worker remains blocked across the
  game-host Stop deadline, and maintenance reports `PendingRetained`. The
  worker's own maintenance call is refused. Its thread-local destructor then
  blocks separately, and maintenance remains pending until that destructor is
  released. `ReapedRetained` follows the destruction of the worker's callable
  and TLS, and is repeatable; the owner stays closed and not ready, worker
  admission and host restart remain refused, and the actual capsule and backing
  stay process-retained. A fresh owner also refuses maintenance. This exercises
  the real owned-worker join path, but the worker is synthetic and is not a
  census of native readers or evidence of a game execution barrier.
  When the source's observation-only `owned_shutdown_test_access.h` is present,
  it also synchronizes an overlapping maintenance refusal and checks that
  worker and helper handles were reaped; it does not perform those joins.

Every adopted terminal result stays `ProcessRetained`; a failed stop does not
authorize restart, destruction or unload. These tests do not establish native
reader exclusion, worker census, mapped code ownership, original-quit safety,
Windows failure behavior, or game acceptance. The detailed contract is part of
the published SDK source and the current product pin; these synthetic tests do
not establish safe native activation, shutdown, or gameplay.

## Product Stop overlap refusal

`stop_overlap_test` links the actual product composition wrapper to a controllable
synthetic Core stub. Separate processes check overlapping and reentrant Stop
requests, wrong-thread results, and sequential successful stops. In the overlap
and reentry cases, the second request changes Stopping to Retained while the
first holds the reservation. It refuses a second Core entry and forces the
first result to fail even if the stub returns success. The successful transition
from Stopping to Idle releases the reservation before the wrapper returns, so
calls overlapping only in wall-clock time can both succeed if the later call
acquires the reservation after that transition. The existing failure and
exception tests cover the terminal latch. Pinned Core
already has its own control-entry gate; this wrapper safeguard is not a new
Core drain mechanism. The test does not model real readers, workers, module
ownership, game-thread behavior, or a safe native shutdown.
