# Owned-registry consumer migration — local draft

Historical snapshot from 2026-10-31: offline base
`9cd25406934d6afd1da400b4aefbf95eb46339c7` and development core candidate
`44ffd1290e261e12684b38ac862ec0c4f08921df`. Those source and candidate checks
were coordinated at the time; they are superseded by the published stable SDK
2.4.0 source and package identities in [sdk-candidate.md](../sdk-candidate.md).

At this snapshot, the draft used core #305 source `a5101d4`; the then-current
candidate contained coordinated owned-registry and detailed host work. Product
startup remained disabled until native readers and workers could be safely
excluded and drained. The stable SDK release does not remove that product gate.

The old, unregistered multiplayer consumer interprets the network API's
idle result as ordinary state; the owned-runtime observer refusal is not yet
distinguished there. Multiplayer is not staged or activated by this draft and
must not be described as owned-runtime ready.

Product diagnostics acquire a snapshot and keep it alive while reading results;
missing snapshots, rows and failed capability updates refuse. A snapshot retains
its own data, not an image lease, a reader permit or a native execution barrier.
The strict owned game host keeps its candidate private and does not expose it via
`AcquireHostSignatureSnapshot`, including after activation. Product code must not
treat the convenience registry as a way to access that candidate.

The real product has legacy native targets, plugins, host adapters and raw cached
addresses that the strict owned host cannot currently admit or drain. It supplies
no synthetic image owner, shutdown owner or success-only barrier. The existing
unconditional product preflight keeps native Prepare/Start, entry-hook installation
and product mutation unavailable. `Stop` failures or C++ exceptions latch the
product against restart; the process, module and backing must remain valid until
exit. The latch does not own that backing or drain readers or workers. Core's
terminal paths retain its owned closure and forbid restart, but legacy product
retention is not proven. No game execution has been performed.

Restoring startup requires a reviewed native exclusion and drain implementation,
real image/resolver/provider ownership, a lease-compliant migration of every
retained native reader and worker, and independent Windows plus later authorized
game evidence. Synthetic snapshot and refusal fixtures do not establish these.
