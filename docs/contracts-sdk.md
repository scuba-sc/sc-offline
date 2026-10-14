# Contracts SDK migration — charter 6C

The separately loadable `contracts` module is a command-first SDK consumer,
not a replacement certification of the retained legacy feature. The production
G11/G12 engine adapter is unavailable: requests refuse, with no private fallback.
No engine pointers, detours, account/online balances or G13 session controls.

## Build / check / install

1. Obtain a checksum-verified SDK archive containing `sc_missions.h` (the pinned
   published `sco-sdk-2.2.0.zip` contains it; 2.0.0 did not).
2. Run `python3 tools/contracts_sdk_check.py --archive <local-or-published.zip>
   --sums <adjacent-SHA256SUMS> --work <new-outside-root> --slice <read|accept|progress|script|economy>`.
   This verifies hashes, extracts into an isolated root, builds/loads the actual
   module, checks its exact public dependencies, installs its two-file folder,
   and runs the SDK checker with absent services. POSIX `--boundary` adds
   private/transitive-header and unresolved-symbol negative controls;
   `--sanitizers` adds a development-only physical-pointer red/green arm.
   Or configure `plugins/contracts` with `SCO_SDK_INCLUDE_DIR=<extracted>/include`
   and `SCO_WORKFLOW_TESTS=ON`; build `contracts-module-test`; run only the named slice.
3. Install component `contracts` into a separate staging root. Do not enable a
   second `contracts` ID while the legacy built-in is still registered.

`contracts.accept <offer-index> <key>` reads the current copied offer and submits
its broker/generation unchanged. Replays retain the explicit nonzero key; the
provider owns bounded once-only owner/session receipts. Altered replay refuses.
No automatic retry, key generation, reward payment or durable receipt claim.

`contracts.objective <id> <expected-phase> <expected-objective>` and
`contracts.advance <id> <expected-phase>` use explicit zero-based expected state.
The provider checks ownership/session and ordered progression; no consumer-side
optimistic increment, retry loop, objective inference or automatic payout occurs.

`contracts.abandon <id>` requests owned once-only abandonment explicitly.
`contracts.cancel` blocks further consumer accept/progression/ingestion (not reads,
receipt lookup or explicit abandon); `contracts.resume` reopens only this local gate.
These commands never erase provider receipts or retry uncertain effects. Operations
are synchronous, so cancellation cannot roll back an already committed operation.

`contracts.ingest <script-ordinal>` obtains copied script bytes through public
`sco.content` 1.0, never filesystem paths or direct file opens. The public content
broker enforces declared-root/link containment. Consumer size/path/declaration checks
admit only `missions/<lowercase-id>.scom`, at most 4096 bytes, exact LF grammar:

```text
sco-mission 1
phases 2
objectives 1
```

Canonical counts 1..64 only, no XML/Lua/includes/network or extra bytes. The provider
also validates; registrations are session-lifetime. The legacy XML script library is
retained but unsupported here, not silently converted or bundled. Owner approval of
this narrower format remains pending. Pack snapshots require host restart to change.

`contracts.balance`, `contracts.apply <key> <expected-balance> <signed-delta>`
and `contracts.receipt <key>` use `game.economy` 1.0. Balance/range preflight never
creates payment authority; the provider atomically checks expected balance and
owns non-evicting owner/session receipts. Replays submit the same explicit request,
not a fresh key. Uncertain effects return refusal and are never automatically
retried. Receipt flags (including dropped advisory notification) remain visible;
receipt polling is authoritative, not signals. No payout on mission completion.

## Storage policy — PENDING, not implemented

No `wallet.txt` import, default-wallet restore, timestamp precedence, DB migration,
reward-once persistence or save is implemented. No direct filesystem fallback is
permitted. The existing contracts database/IDs and legacy wallet/script code are
retained unchanged. An owner-approved, versioned `sco.storage` migration and atomic
payment/save coordination are required before flipping the default feature.
Owner/session receipts are NOT durable exactly-once across reload or restart.
The synthetic fixture uses stand-in public providers, not the game-pack provider
or real owner-release dispatcher; loader/engine acceptance remains separate.

Read commands: `contracts.status`, `contracts.offer <index>`,
`contracts.mission <id>`. Every invocation reopens the public table and validates
complete callback extents and non-null methods. Unload clears consumer admission;
no unload mutation or worker/queued tick exists. The host releases registrations.

**Pending:** production adapter, full retained mobiGlas/UI/mission-kind parity,
15-second automatic offering, actor-death orchestration, wallet precedence/import/
save/payout policy and reload persistence. Legacy host sources remain untouched,
not counted as migrated. Windows/MSVC, actual loader fault guards and gameplay
acceptance are NOT VERIFIED. Dynamic synthetic public-provider proof is separate
from engine/provider acceptance and from durable exactly-once persistence.
