# Native reader barrier: integration design and evidence gap

Status: design plus product refusal. No native registry owner is enabled. The
offline #184 baseline at `00989b4443ad0de1a33ab45a9139892ff08b0546` and
coordinated core #304 at `1166c9f12d7327dd50a58b1d9261e7d6cc21bef6` do not
contain the separate owned-registry integration or its explicit refusal. Do not
interpret a successful build of these sources as a registry activation proof.
The registry consumer must keep its explicit refusal until a real barrier exists.

Before the product preflight added in this local work, the #184/#304 candidate did **not** refuse game-service startup merely because a native-reader barrier was missing. It sets `gameServices = true` and leaves
`nativeDataCoreReads = false`; core checks `preEngineQuiesced` only for that
opt-in read mode. Positive target, anti-cheat, firewall and TCP evidence gates
Prepare; required quit and DataCore hooks can then be installed, and the product
applies its offline patches before later game-thread Start rechecks admission.
Unknown evidence and required-hook failure refuse, but those checks do not prove
reader exclusion or safe owned-registry activation. The old executable-entry jump was installed before those checks. Its installation could fail silently; on a
60-second timeout the entry could resume while the worker was still modifying
state. Its restoration-failure path calls `TerminateProcess`. None of
these behaviors establishes the owner-requested non-fail-stop reader barrier.

The product now refuses before `host::Prepare`, `host::Start`, product patches,
plugin activation, or startup-hook installation. It emits an unavailable diagnostic
and leaves those features inactive. This is an unconditional product refusal, not
a reader barrier or gameplay proof. The coordinated gitlink is core #304
`571fe8e2c911d7c63c372103724896feaabe58f2`; that source also refuses native
ASOP Start until the safe phase exists.

## Phase and lifetime requirements

1. Preparation may inspect original immutable image bytes and construct owned
   metadata. It must not publish the registry, install a detour, alter native
   code or expose partial state to readers. Missing admission or image lifetime
   leaves no published owner and refuses.
2. A real host-provided exclusion mechanism must cover **every native entry**
   capable of reading the registry or entering the affected targets. It must
   prevent new entries and drain entries already in progress before activation,
   and remain held through all native mutation and publication. A boolean or a
   synthetic success callback is not a barrier. Determine actual entry points,
   owners and concurrency independently; unresolved coverage means refusal.
3. While exclusion is held, recheck admission and source identity, install the
   prepared state atomically, and release exclusion only after a complete commit.
   On failure, do not admit consumers. If rollback cannot be proven complete,
   retain all possibly referenced storage and code until process exit, refuse
   restart and report the incomplete outcome. Do not terminate the process as a
   substitute for safe shutdown.
4. Shutdown must close all new admissions and independently drain native readers,
   callbacks and workers before destroying their data or unloading code. If any
   cannot be drained, retain backing for process lifetime and report incomplete
   stop. Revoking a capability, removing a detour, or closing the product mutation
   gate alone does not drain in-flight native calls or undo direct writes.

The existing executable entry gate is not sufficient: `StartOffline` runs on
the worker before it releases that gate; `StartHostKit` runs later through the
game window's message hook, after native execution has resumed. The entry wait
also has a timeout. Holding it until a game-thread callback would prevent that
callback from running. An activation split therefore needs a new, proved
exclusion boundary rather than extending that wait or asserting quiescence.
The current gate can fail to install; a timeout releases the entry while the
worker may continue mutation. It also has an existing process-termination path
if restoring entry bytes fails. These are unresolved current behavior, not proof
of safe failure, and require separate redesign and Windows validation.

## Required evidence before enabling

- Core and product agree on exact reviewed source SHAs, phase contract, owner,
  ownership of image and native storage, reader set, and stop/failure semantics.
- Windows/MSVC synthetic native targets exercise entry-before-close, concurrent
  entrants, drain, activation, failure at every install step, partial rollback,
  reentry, timeout, lost admission, concurrent shutdown and retained lifetime.
  Assert no reader observes partial publication and no backing is freed early.
- Independent exact-build analysis identifies and covers all native callers and
  worker creation, with a real exclusion mechanism, lock ordering and lifetime
  evidence; static signature resolution or synthetic target tests cannot prove it.
- Complete product and core Windows builds and the relevant automated suites.
  Live firewall and later owner-authorized offline game acceptance are separate;
  no game execution is authorized by this design.
