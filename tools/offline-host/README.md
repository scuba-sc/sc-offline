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
host has no such game-host-facing detailed result; a future contract belongs to
core. No result authorizes native activation, teardown or restart. Product
`NativeActivationAvailable()` remains false. This test is not game acceptance.

`wait_until_closed` observes closure, not entry into the drain wait. In the
positive copy case the still-held read itself prevents drain completion at that
point; the fixture does not claim a separate drain-wait observer.

The pinned core offers no deterministic injection seam for an exception during
`options = o`. This fixture does not test a partial Options copy; qualifying
that path requires a core-owned test seam or another safe deterministic
allocation-failure mechanism. The owner-fault Start case does not reach the
post-app-start seam; the separate optional case does.
