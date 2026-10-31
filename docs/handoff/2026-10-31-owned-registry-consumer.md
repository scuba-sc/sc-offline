# Owned-registry consumer migration — local draft

Offline base `772129e09e7838d56165bd3613f43ac49d373bb8`; development core
#305 `a5101d406a9ddbff14e437408686ffb04266be18`. The embedded source and the
unpublished legacy SDK preflight are coordinated to that SHA; published RC2
remains the separate external-plugin archive. Neither source nor artifact is a
final SDK release.

Core #305 advanced after the original `49eb153` preflight. This draft uses its reviewed `a5101d4` source, which also refuses unproven native service startup and unowned provider publication.

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
