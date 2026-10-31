# Owned-registry consumer migration — local draft

Offline base `772129e09e7838d56165bd3613f43ac49d373bb8`; development core
#305 `49eb153d1d0952ebbc7baa258988e6dc6cc67320`. The embedded source and the
unpublished legacy SDK preflight are coordinated to that SHA; published RC2
remains the separate external-plugin archive. Neither source nor artifact is a
final SDK release.

At the last remote check, #305 had advanced to
`b2c9aa50e6888c0a317494422ede86b0ab1e9fd5`. Its additional changes refuse
unproven native app-service startup and unowned provider publication. This draft
and its fixtures are pinned to the requested `49eb153` candidate, not that newer
head. Coordinate and retest the final dependency SHA before publishing.

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
