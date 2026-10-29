# Contracts SDK migration — charter 6C

The separately loadable `contracts` module is a command-first SDK consumer,
not a replacement certification of the retained legacy feature. The public
service contracts and sco-core provider implementation exist, but production
G11/G12 mutations remain gated on a positively admitted, ready backend; requests
fail closed until then, with no private fallback. No engine pointers, detours,
account/online balances or G13 session controls.

## Build / check / install

1. Obtain a checksum-verified SDK archive containing `sc_missions.h` (the pinned
   published `sco-sdk-2.4.0.zip` from `sdk-v2.4.0-rc.2` contains it; 2.0.0 did not).
2. Run `python3 tools/contracts_sdk_check.py --archive <local-or-published.zip>
   --sums <adjacent-SHA256SUMS> --work <new-outside-root> --slice <read|accept|progress|script|economy|metadata>`.
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

## Copied script-reference metadata

`contracts.references <list-ordinal> <row-index>` reads one metadata row from an
explicit enabled data pack's `lists/contract_scripts.txt`. The ordinal is within
the public `sco.content` **List** kind, including other List names; selecting a
different name refuses. Ordinals and row/reference indices are zero-based and
startup-scoped. Duplicate pack files and duplicate GUID rows remain independently
addressable, in provider/file order; no pack precedence or GUID merge is inferred.
`contracts.reference <list-ordinal> <row-index> <reference-index>` reads one
reference, avoiding truncation of a long list. Replies that do not fit return
`TOO_MANY`, never truncated success.

The supported text format preserves the legacy reference layout:

```text
# Synthetic metadata; these are not game mission IDs or installed scripts.
0123456789abcdef0123456789abcdef Example first.xml|-|second.xml
abcdef0123456789abcdef0123456789 NoReferences -
```

A row has exactly 32 hexadecimal GUID characters, a space, a nonempty name token,
a space and nonempty pipe-separated reference tokens. `-` contributes no
reference; other tokens and duplicates retain their byte order and are display
metadata only. LF/CRLF and a final line without LF are supported. Empty lines and
lines beginning `#` do not count as rows. NULs, control bytes in metadata tokens,
empty reference tokens or malformed noncomment rows refuse the whole file, even when an earlier
selected row is valid. Limits are 1 MiB/file, 16,384 rows, 8191 bytes/line, 255
bytes/name, 32 non-dash references/row and 255 bytes/reference. These are bounded
consumer parsing rules, not a new native mission format or ABI.

The reply identifies the source GUID as metadata, **not a public mission offer
ID**, and reports native installation **UNKNOWN/UNAVAILABLE**. The published
mission table has no legacy XML installation query or source-GUID-to-offer
mapping. Copied bytes and `-` do not establish successful script installation,
eligibility, offering, acceptance or execution. These commands never call
mission/economy providers, ingest, `HaveScript` or private host functions. They
remain available while consumer mutations are cancelled and require the normal
public game-thread/live-owner content admission. Each invocation reopens and
copies the selected item; unload retains no metadata/table/ID.

There is no base-file import or new pack staging. Supply an ordinary user-authored
data pack explicitly while the host is stopped, then restart. The retained host
`ReadContractScripts` and its successfully-added XML availability checks remain
unchanged. This diagnostic source slice does not migrate the retained engine or
its native mission, wallet, offering and persistence behavior.

**Pending:** production adapter, full retained mobiGlas/UI/mission-kind parity,
15-second automatic offering, actor-death orchestration, wallet precedence/import/
save/payout policy and reload persistence. Legacy host sources remain untouched,
not counted as migrated. Historical RC1 evidence: strict MSVC 19.44 builds and
all six synthetic workflow checks passed against published RC1 headers, including
loading the production DLL. The metadata fixture failed against the prior accepted
module as expected. The RC1 content provider also passed a separate
synthetic-pack/module check. The `contracts-sdk` job in [CI run 40760241329](https://github.com/scuba-sc/sc-offline/actions/runs/40760241329/job/123069886868),
on sc-offline commit `7335c9bbbc36f6d82c6cc3958cccbfada9b8f222`, passed against
published RC2: an isolated MSVC build, the read-slice module workflow, dependency
traces and the SDK checker. This RC2 evidence does not extend the historical
six-slice or content-provider results to RC2.
Actual game loader fault guards and gameplay acceptance remain unverified.
**Not run in game.** These checks do not establish native XML installation or
durable exactly-once persistence.

For a connected no-game preflight, build the actual contracts module against an
extracted checksum-verified SDK and build the matching Core source's
`sco_offline_missions`, `sco_host`, `sco_runtime` and `sco_core` libraries. On
POSIX, `tools/test_contracts_core_integration.sh <core-source> <core-build>
<contracts-module>` exercises Core's real missions/economy tables with a
synthetic backend. It checks owner scoping, withdrawal, a failed uncertain
balance adjustment and release cleanup. Mission completion does not trigger a
payment; the fixture does not implement partial reward accounting, durable
settlement, native adapters or game acceptance.
