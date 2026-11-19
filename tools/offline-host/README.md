# Owned capsule fixture

Synthetic, non-native tests of the host-source target. They do not change product startup, and none of them uses a game. Not run in game; see [status.md](../../docs/status.md). The product rules they test are in [host composition](../../docs/host-composition.md).

```sh
cmake -S tools/offline-host -B /tmp/offline-host-build \
  -DSCO_HOST_SOURCE_DIR="$PWD/external/sco-core" -DBUILD_TESTING=ON
cmake --build /tmp/offline-host-build
ctest --test-dir /tmp/offline-host-build --output-on-failure
```

## Host scenarios

Host state is global and terminal outcomes cannot be reset, so each scenario runs in its own process. Each one builds and roots a real owned capsule, then hands the host a synthetic image descriptor and bytes, a separately owned mock resolver token and an `OwnedShutdown` object. The token owns a real object, but no synthetic resolver callback is registered, so it says nothing about the lifetime of mapped resolver code. The executable owns its static callback code. No game mapping is represented.

The options turn off plugins and private sessions and use external quit and DataCore hook policies. There are no native reads, game services, scripts, builtins, host-service or menu callbacks, runtime inspection, native hooks or barrier callback. App startup still sets up internal UI, widgets, settings, content, IPC and network-service bookkeeping. The admission inspector returns synthetic evidence only.

- `badarg`: invalid options are refused in Idle before the owner is exposed, and all mock objects are destroyed after their local owners are released.
- `admission`, `budget`: a real owned Prepare failure after the startup reservation and a good Options copy. The host closes the owner and enters Failed. The fixture keeps its process root and checks the objects are still live. The budget case is not an exception during the Options copy.
- `host`: real Prepare, Start, wrong-thread Stop, re-entrant Stop from the Tick inspector, and game-thread Stop. Inspector re-entry breaks the contract on purpose. A production inspector must not re-enter. In the older stable Core `6106fa3`, the last one returns Failed and keeps its state even after a successful owned drain, and later Stop and Start refuse. The fixture's root stays live for the whole process.
- `legacy-request`: real Core legacy Prepare followed by its detailed RequestStop. No detailed capsule was adopted, so the report is Unavailable and cannot close the owner. The fixture separately requests owner closure, as the product does, and checks the report stays Unavailable. It does not validate native quit or safe teardown.
- `start-fault`: an owner fault injected through the inspector during Start after Activate. Checks terminal failure and retention.
- `after-app-start`: uses Core's private synthetic `host_testing.h` seam to inject a failure after app startup, and checks that the host enters Failed and keeps the rooted backing. CMake registers it only when that header is present and `SCO_CAPSULE_PRIVATE_SEAM` is `ON`. It is not a public or plugin API. Set the option `OFF` to leave it out.
- `copy`: a synthetic output producer, not a host callback. It holds a real `OwnedShutdown::Read` across the whole vector copy, pauses after the first element, sees closure, and checks that a concurrent drain finishes only after the rest is copied. Destructor counters show the backing survives that time and the output survives the release.
- `copy-negative`: the same without the read lease, with the producer outside the owned worker census. Drain can finish while the copy is paused. It never touches freed memory.

The root is installed before exposure and is not destroyed on terminal host paths. This shows a test-process policy, not a production process or module owner. The fixture provides no native execution barrier, does not prove real module mapping or a worker census, and does not exercise a successful game-host Stop. No result authorizes native activation, teardown or restart. `wait_until_closed` observes closure, not entry into the drain wait.

For older source packages without `HostCapsule`, Linux has a fallback test, `options_copy_fault_test`. It replaces the allocation operators with malloc and free versions and arms a one-shot thread-local allocation failure during the old Options copy. It is not registered for the older stable pin. Stable 2.4.0 runs `host_report_options-copy` instead, which injects a failure during the real partial capsule and Options copy and checks that the owning backing stays retained. That is synthetic Linux evidence, not Windows allocator evidence or a native reader barrier.

## Published host report qualification

The published SDK 2.4.0 source is Core `6106fa3639cbcc28f78a1ded8de2707de019f4d3` (tag [`sdk-v2.4.0`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0)). The host-source archive SHA-256 is `a0aaaba07e3d4eb0d8c264912cd6f2ab23f0d237a99fa07816005790a76efc4c`, and the core-source archive SHA-256 is `df756719da0de54ead18973258d3bc35640e26d44088b3b6ffcf8672ebb1ee51`.

The pull-request check and the manual `qualify_stable_sdk` dispatch download the release assets, check the listed files against `SHA256SUMS`, check package hashes and sizes against `release.json`, check that the release tag peels to the product source pin, and run this fixture against the exact source checkout and against the extracted host-source archive, on Linux and Windows. They do not change the product pin.

When `SCO_HOST_SOURCE_DIR` provides the `HostCapsule` contract and the private source testing header, CMake also builds `host_report_test` against it. That is a separate, test-instrumented copy of Core's host code, as in Core's own report test. The production `sco_game_host` target is not modified. The current stable source has this contract, and the legacy refusal tests stay next to the detailed report tests.

Each report scenario runs in a fresh process. A real backing object owns the synthetic image bytes, the shutdown owner and a resolver backing object that holds the data for the synthetic output copy. The inspector, Core's resolver code and the test code are linked into the executable, so no external resolver state is exercised. The test builds a real immutable `HostCapsule` before entry and checks its descriptor and host-version copies. It clears all caller references and checks weak references and destructor counters. A fresh unsupported configuration is `NeverExposed` with session zero and can be destroyed. After adoption, Core keeps the capsule and its backing, with a nonzero session ID.

- `reentry`: nested detailed Start and Stop from the inspector refuse and keep the adopted report. Inspector re-entry is an attack test, not an allowed inspector.
- `wrong-thread`: Stop refuses on another thread while Running. Game-thread Stop then fails, closes admission and keeps the dependencies.
- `failed-start`: the private post-app-start fault gives a terminal failure and repeated refusal. Successful starts use the game host's private `app::detail::StartOwned` route, not public app startup.
- `options-copy`: Core's allocation fault, armed inside the real Options assignment. A long `pluginRoot` forces an ordinary allocation. The fault must fire after the image and resolver owner fields are copied. Reference counts show the backing, the capsule Options and the partly assigned Core Options all keep the real owners. This does not hold for every allocator or Windows standard library.
- `copy`, `copy-negative`: as above, with the real game-host Stop closing and draining admission. Neither producer is a game-host callback or native reader.
- `retained-reap`: registered only when the source provides `reap_retained_until`. A real owned worker stays blocked across the game-host Stop deadline, and maintenance reports `PendingRetained`. The worker's own maintenance call is refused. Its thread-local destructor then blocks separately, and maintenance stays pending until that is released. `ReapedRetained` follows the destruction of the worker's callable and TLS, and it can repeat. The owner stays closed and not ready, worker admission and host restart stay refused, and the capsule and backing stay retained for the process. A fresh owner also refuses maintenance. The worker is synthetic and is not a census of native readers. When the source has the observation-only `owned_shutdown_test_access.h`, the test also synchronizes an overlapping maintenance refusal and checks that worker and helper handles were reaped. It does not do those joins.

Every adopted terminal result stays `ProcessRetained`. A failed stop does not allow a restart, a destruction or an unload. These tests do not show native reader exclusion, a worker census, mapped code ownership, safety of the original quit, Windows failure behavior or game behavior.

## Product non-draining Stop request

The product does not call Core's synchronous Stop through this seam. Its request path closes the product request gate, requests owner closure, then asks Core for its non-draining report. A separate controller may drain only after the request work completes and retention is published. Publication can come before the wrapper's return instruction. When native activation is refused (a Core without the process-retained composition), the product refuses before any of this work.

- `request_gate_test` exercises the real atomic request gate with synthetic work: normal completion, re-entry, a competing request and a failure. It checks that restart is refused and that contention cannot allow a drain while the first request is running.
- `refusal_test` checks that the real wrapper does not enter Core when native activation is refused.
- `host_report_cooperative-request` uses the same helper with a restricted synthetic detailed capsule and its exact owner. A synthetic reader requests closure, then an independent controller waits for it before draining. It checks retained backing and refusal of new reads.
- `host_report_owner-mismatch` is a negative control: a different owner can be drained while the adopted capsule's owner still has a live read. The helper does not check identity, so a future detailed product adapter must prove that its owner is the adopted capsule's exact owner before exposure.

None of these is native adoption, an engine hook, a direct quit, or evidence that native unload is safe.

## Capsule-bound non-native consumer

`src/capsule_lifecycle.h` is a portable, restricted adapter. These tests use it, but Windows product startup does not. It owns an immutable capsule and uses only that capsule's shutdown owner for reads, requests and drains. There is no per-operation owner argument. Its own completed, successful `PrepareDetailed` call creates the association. A nonzero session in a refused, busy or failed result does not. `Associated()` tells this input's association from a Core report about another session. The adapter refuses request, start and drain before association, including during Prepare. It does not support a request from inside a Prepare callout.

Before it calls Core, the adapter retains its capsule root for the whole process, including after a refusal and after an uncertain or busy result. This over-retention avoids releasing dependencies after an unclear outcome. It does not show that Core adopted the input. Construction alone is private and can be released. Preparation is a single attempt, with no retry, reset or cleanup call. An exposed adapter can be destroyed after its public calls finish while a returned Read is still alive: the owner and backing stay rooted. The capsule backing must own every dependency, as Core requires.

The `host_report_bound-*` cases use the real restricted Core route:

- `private`: destroying an adapter before any Prepare releases its private inputs.
- `repeat-start`: a repeated Start that is not Ok latches refusal of ordinary use and restart, and keeps same-owner request and drain association.
- `copy`: a reader requests stop, keeps its lease through the final output copy, and an independent controller drains the same capsule owner.
- `destroy-read`: destroying the adapter and the caller's references does not release the capsule or backing while a returned Read is live.
- `timeout`: a held read passes the drain deadline. Releasing it later cannot turn the failed drain into success or allow a restart.
- `foreign`: an already adopted other capsule cannot bind this adapter.
- `busy`: inspector re-entry while Core's control gate is held returns another capsule's nonzero session without association. Re-entrant inspectors stay forbidden in production.
- `refusal`, `root-allocation`, `failed-prepare`, `failed-start`: unsupported inputs, failure before Core's root allocation, failure after adoption but before the Options copy, and failure after app startup. Each keeps the backing, refuses ordinary use and never allows a restart.

The earlier cooperative and wrong-owner negative controls are unchanged. These tests do not enroll native readers or workers, and do not show native ownership, Windows teardown, app retirement or game behavior. ASOP stays refused. No frozen plugin C ABI or Core pin change is involved.
