# Offline owning dependency inventory — pre-activation design

Status: inventory and synthetic qualification only. Current coordinated input:
offline #185 head `9d7165038edda5a7196e311f54943c3d0d8dde71` proposes development
source `44ffd1290e261e12684b38ac862ec0c4f08921df`; current main
`9cd25406934d6afd1da400b4aefbf95eb46339c7` still has production gitlink
`1b6782bc5c8b7d189cdff4c83e81957f09af35d7`. The exact RC3 candidate
was published by core workflow run `41332377573` as artifact `13093044361`, with
outer SHA-256
`464b7ca2f05f402cfe5581b2a34033c169a977cf5493a396a36a7c63dc0c2ddc`. Its
host-source archive SHA-256 is
`799511bfa08a7bf77a46fcf8262d3cac4a5f0e6a0ef04a28f53e6e624c01e8a0`; its
core-source archive SHA-256 is
`807a322eab2b3d8c25e10c3582008ec3ec50b6b536bc947f2b28cf4b63095c22`.

Product native preflight is unconditionally unavailable. The entries below are
an ownership inventory, not evidence of safe native activation, shutdown or game
acceptance. Evidence tied to earlier source and package identities is historical
and must not be attributed to this coordinated RC3 input.

## Actual product surfaces requiring ownership

The inventory describes potential retained dependencies in existing product code;
it is not evidence those paths have started in this build. The closure must include
transitive code and data, not only the descriptor or pointer copied into Options.

| Surface and source | Borrowed or exposed dependency | Current evidence and unresolved owner |
| --- | --- | --- |
| `PrepareHostKit`, `src/dllmain.cpp` | Static `sco::Image` descriptor points into the game mapping; `Options::image` is a raw pointer. Resolver code/state and any resolved addresses outlive the resolving call. | The descriptor does not own or pin the image mapping. Real image, resolver and executing-code ownership and all native reader exclusion remain unproven. |
| `Platform` construction, `src/dllmain.cpp` and `sco/app.h` | Capability callback, static reserved-chord strings, Lua runtime/extension, plugin module operations, filesystem paths, DataCore, game services, host menu and optional product host service. | C++ value fields may be copied but callback code, raw context, module tables, scripts and transitive dependencies must stay alive through the core-defined terminal result. Existing product settings are outside the restricted mock fixture. |
| Offline inspection and admission, `src/dllmain.cpp` | Inspector callback code, `g_evidence`, its probe and synchronization, target/firewall/EAC observations. | Static storage is not a reader drain or a guarantee that the module containing the callback remains mapped. Any real positive evidence must remain fresh, and failures continue to refuse admission. |
| Game-thread message service, `src/dllmain.cpp` | Window hook callback, hook handle, tick/quit dispatch, process/game objects and callback code. | Unhooking a handle is not proof that all in-flight callback frames or game-owned data are drained. `WM_QUIT` and native original Quit have distinct lifecycle requirements. |
| Startup entry gate, `src/dllmain.cpp` | Executable-entry jump and allocated stub, saved entry bytes, completion event, wait callback and original entry continuation. | The current preflight refuses installation. The legacy gate has a 60-second timeout and a process-termination path on failed restoration; neither establishes reader exclusion or safe rollback. |
| IIM continuation handoff, `src/services.cpp` | Pending pointers to caller-stack `IimCall` objects, their events and lock, plus original continuation function pointers and game-owned arguments. | A queued or taken continuation and its producer stack/event must remain valid until completed or safely removed. Timeout and shutdown races require an explicit census and drain; retention of a registry snapshot is insufficient. |
| Spawner input worker, `src/spawner.cpp` | Fire-and-forget flight-ready input thread, its code and possible OS input effects. | The thread handle is closed after creation, so this path has no demonstrated owned join or TLS drain. It must be included in the worker census before enabling related native work. |
| Worker entry, `src/dllmain.cpp` | `ModThread`, startup state, module handle, logs, app options and all code it can enter. | `CreateThread` is not retained as a joinable worker by this code. Its termination, in-flight work and TLS destruction have not been proved against shutdown/unload. |
| Menu/render/input, `src/menu.cpp` | Menu thread, window and message callbacks, ImGui and D3D objects, frame protocol and events, client callback table/context. | `Menu_Start` closes the returned thread handle. A real owner must account for thread exit, render frames, callback admission, windows and graphics resources; no complete drain is demonstrated. |
| Legacy feature/ASOP work, `src/dllmain.cpp`, `src/offline_host.cpp` | Contract death subscription and tick callbacks, missions/contracts state, cached native addresses, optional selection/ASOP service context and workers. | Every enabled consumer must be enrolled in exclusion/drain and closure retention; a core registry snapshot or readiness flag does not cover these legacy paths. |
| Quit continuation, core `src/game/host.cpp` | Original quit function, game-owned `system`/`info` arguments and objects that the game may destroy. | Pinned `ForwardQuit` calls original after a failed stop. Retaining product memory alone cannot prove that original-quit continuation leaves game objects usable. This path needs its own independent contract and evidence. |

The product `offline_host::Stop` latches on failure/exception and prevents restart;
it does not hold any of the objects above alive. A copied signature snapshot owns
its metadata, not an image lease or a native instruction/read barrier. A static
variable, raw `HMODULE`, counter, callback returning success or no-op deleter is
not proof of the actual closure or of safe unload.

## Capsule and transition requirements for later agreement

Before `Prepare` can expose any borrowed dependency, an actual owner must already
hold the complete closure: mapped code and image bytes, resolver/provider code and
state, callback code and contexts, tables and referenced data, and every worker
and native entry that may use them. The root must exist before exposure and be
retainable without allocating on a failure path. Whether module pinning and
process-exit retention are implementable by the product is unresolved.

The present product path is `offline_host::Start -> game::host::Start ->
app::detail::StartOwned`. Public `app::StartDetailed` is not a replacement for the
game-host transition. Core and product must agree on a detailed **host** result
that distinguishes fresh refusal, exposure/partial prepare, started, and
retained/uncertain outcomes, with the complete closure held before the call.
Names and fields of the forthcoming core result are not assumed here.

The product needs the eventual contract to answer, at minimum:

- **Before entry:** what typed owning capsule or owners are accepted and when
  their code, image, resolver, callback/context and worker dependencies become
  potentially exposed; who establishes the root before any partial copy or callback.
- **On Prepare/Start return or exception:** the invocation and phase to which
  the result applies; whether it refused before exposure, started, or may have
  exposed or retained dependencies; and the sticky retention obligation when
  an earlier invocation already retained aliases.
- **On Stop and repeated/wrong-thread/reentrant calls:** whether new admission
  closed, which readers/workers/callback frames actually drained, whether any
  cleanup is complete, and whether restart or owner destruction is prohibited.
  A boolean readiness result must not imply these distinct facts.
- **At the output-copy boundary:** which owner and admission lease remain valid
  through the caller’s last copied byte, how closure rejects new work, and how
  failure/timeout keeps backing valid without relying on an unsafe callback.
- **For tests:** a deterministic, source-owned seam for a failure during actual
  partial Options/capsule copy, plus observable start and drain phases. A hook
  that throws before copying is not evidence about partial-copy behavior.

These are requirements to reconcile with the core owner, not declarations of a
new API or evidence that the current host already provides them.

For the app-level contract only: `RefusedBeforeExposure` covers that invocation
and cannot erase earlier aliases; `RetainDependencies` is process-retained even
if a later stop says OK; after `Started`, an unsuccessful/uncertain stop cannot
free the closure or allow restart. A real exclusion-and-drain protocol is still
required for native entry, instruction execution, callback output copying and
workers. Original-quit continuation requires separately proved game-owned object
lifetimes.

## Restricted fixture boundary

A portable test may own a synthetic byte image, image descriptor, resolver and
callback/context objects in a real capsule, with destruction counters. The test
must root that owner before host Prepare and use the pinned game-host route with
external synthetic quit/DataCore choices and native reads, game services, hooks,
plugins, builtins, scripts, host adapters and private sessions disabled. The
pinned restricted app may still initialize internal UI, widgets, settings,
content, IPC and network-service bookkeeping; disabling private sessions does
not mean those services were never initialized. If a scenario simulates a host
result or a controlled reader, label it synthetic;
never supply a success-only native barrier. Owned read admission held through the
last output byte can demonstrate the mock's lifetime rule, not a native census.

The pinned owned-host Stop deliberately retains and returns failure even after a
successful reader drain because failure-atomic cleanup is not established. Test
expected retained state rather than expecting a fabricated successful retirement.
Fresh refusal, partial prepare, failed start, wrong thread, reentry, failed stop,
repeated stop and paused output-copy cases must state whether they reach actual
host code or a mock seam and verify that the real owner is not destroyed early.

Before any production wiring: obtain the core owner's exact reviewed result and
ownership contract, match and verify exact source and package hashes, integrate
real product closure ownership and native exclusion/drain, perform Windows/MSVC
failure-injection and exact-build native analysis, then separately authorized
offline game acceptance. No game or native runtime was run for this inventory.

## Historical synthetic evidence at the earlier pinned source

The standalone `tools/offline-host` fixture exercises actual restricted
`game::host::Prepare`, `Start` and `Stop` with the owned synthetic capsule;
its separate output-copy producer is not a game-host callback. Pinned source and
the generated, inventory-verified host-source kit both passed 14/14 CTests with
the private post-app-start fault seam enabled. The kit passed 13/13 with that
seam disabled. Host-source archive SHA-256:
`7bd1898343606b2e973fb3403464907a63461d465309c65d2a58a3df21561227`.

A separate Linux/GNU-only fresh-process allocation-fault case reached a real
partial Options-copy failure: the fault fired, Prepare and phase were Failed,
the owner closed, the inspector was not called, and the image and resolver
owners each retained two references after caller Options were cleared. The
fixture owns those objects; it does not assert a no-op deleter or free backing
while it may be referenced. This is ordinary `new`/`new[]` interception on the
tested Linux/GNU environment, not deterministic Windows or arbitrary allocator
coverage. No native barrier, mapped game code, production capsule, native hook
installation, plugin, private session or game execution was exercised.

## Historical local HostReport candidate

At the earlier core candidate source `4c09ab2f73caa3d1103214adc56193d2434a8795`
and host-source package SHA-256
`d043bd459b6570bd49b072014e96e6844a58c7078af7c5852a0825d736f30e39`, the
standalone fixture tested the additive `HostCapsule` and `HostReport` contract
without changing the production gitlink. Those report tests used the real
game-host transition and a genuine synthetic backing owner, dropped caller
roots after adoption, and verified core's nonzero process-retained report and
surviving backing. They cover fresh pre-adoption refusal, inspector
reentry, wrong-thread Stop, post-app-start failure, repeated Stop/Start
refusal, an allocation fault inside partial Options assignment, and a
separate final-output copy with an owned read and a no-read negative control.
The candidate's instrumented copy of host.cpp is test-only. It preserves the
private `app::detail::StartOwned` route and does not supply a native barrier.
These results are synthetic source evidence only; the unresolved production
ownership, native drain and game-acceptance requirements above still apply.

## Coordinated RC3 candidate qualification

The current opt-in candidate workflow uses core source
`44ffd1290e261e12684b38ac862ec0c4f08921df`, whose history includes the reviewed
core #315 head `4c09ab2f73caa3d1103214adc56193d2434a8795`. It checks that #315 is
still open at that exact head or is positively reported merged from that exact
head onto `main`; a closed-unmerged PR, different head or base, or incomplete
merge receipt is rejected. This permits reproducible checks after the approved
core PR merge without accepting an abandoned or substituted producer.

Core workflow run [41332377573](https://github.com/scuba-sc/sco-core/actions/runs/41332377573)
published artifact `13093044361` with outer SHA-256
`464b7ca2f05f402cfe5581b2a34033c169a977cf5493a396a36a7c63dc0c2ddc`. The
candidate's combined SDK, core-source and host-source packages were independently
rebuilt from exact source `44ffd...` and byte-matched. The host-source package
hash is `799511bfa08a7bf77a46fcf8262d3cac4a5f0e6a0ef04a28f53e6e624c01e8a0`;
the core-source package hash is
`807a322eab2b3d8c25e10c3582008ec3ec50b6b536bc947f2b28cf4b63095c22`. Their
compatibility metadata names core and game-sc source `44ffd...`, and packaged
checksums pass.

At this updated input, the 13-case candidate-PR guard suite passes. The same
eight `HostReport` tests pass 8/8 when compiled against the exact core source
tree and again against the extracted host-source package. They cover refusal,
reentry, wrong-thread stop, failed start, partial Options copy, output copy,
the no-read negative control and retained-worker reaping. These are synthetic
Linux tests; the workflow adds exact-source and extracted-kit Linux/Windows
jobs. They establish neither native-reader exclusion nor product closure
ownership, and do not enable native startup. No game or native runtime was run.
