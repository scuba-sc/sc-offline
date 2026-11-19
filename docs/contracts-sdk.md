# Contracts module

The `contracts` module is a separately loadable plugin that you drive with commands. It does not replace the retained legacy contracts feature. The public service contracts and sco-core's provider exist, but changes to missions and the player's balance stay gated on a backend that is admitted and ready. Until then every request fails closed, with no private fallback. The module uses no engine pointers, detours, account or online balances, or private-session controls.

Nothing on this page has been run in game; see [status.md](status.md).

## Build, check and install

1. Get a checksum-verified SDK archive that contains `sc_missions.h`. The published SDK 2.4.0 archive (`sco-sdk-2.4.0.zip`, tag `sdk-v2.4.0`) does. See [SDK coordinates](sdk-candidate.md).
2. Run the check:

   ```sh
   python3 tools/contracts_sdk_check.py --archive <local-or-published.zip> \
     --sums <adjacent-SHA256SUMS> --work <new-folder-outside-the-checkout> \
     --slice <read|accept|progress|script|economy|metadata>
   ```

   It verifies hashes, extracts into an isolated folder, builds and loads the module, checks its exact public dependencies, installs its two-file folder, and runs the SDK checker with the services absent. On POSIX, `--boundary` adds negative controls for private and transitive headers and unresolved symbols. `--sanitizers` adds a development-only physical-pointer arm.

   Or configure `plugins/contracts` with `SCO_SDK_INCLUDE_DIR=<extracted>/include` and `SCO_WORKFLOW_TESTS=ON`, build `contracts-module-test`, and run one slice.
3. Install the `contracts` component into a separate staging folder. The host registers no `contracts` built-in, so this folder is the only provider.

The read-slice check passed against the published 2.4.0 archive on Linux with synthetic providers. It builds and loads the module, checks its public dependencies, stages it and confirms the refusal when no provider is present. It does not cover the other slices, Windows or gameplay.

## Commands

Read commands: `contracts.status`, `contracts.offer <index>`, `contracts.mission <id>`. Each call reopens the public table and checks every callback extent and that no method is null. Unload clears the module's admission. No worker or queued tick exists, and the host releases the registrations.

| Command | What it does |
| --- | --- |
| `contracts.accept <offer-index> <key>` | Reads the current copied offer and submits its broker and generation unchanged. A replay keeps the same nonzero key, and the provider keeps bounded once-only owner and session receipts. An altered replay is refused. There is no automatic retry, no key generation, no reward payment and no durable receipt. |
| `contracts.objective <id> <expected-phase> <expected-objective>` | Moves progress, using explicit zero-based expected state. |
| `contracts.advance <id> <expected-phase>` | Advances the phase the same way. The provider checks ownership, session and order. The module does no optimistic increment, retry, objective inference or automatic payout. |
| `contracts.abandon <id>` | Requests once-only abandonment of an owned contract. |
| `contracts.cancel` | Blocks further accept, progress and ingest from this module. Reads, receipt lookup and explicit abandon still work. |
| `contracts.resume` | Reopens only that local block. |
| `contracts.ingest <script-ordinal>` | Gets copied script bytes through the public `sco.content` 1.0. See below. |
| `contracts.balance`, `contracts.apply <key> <expected-balance> <signed-delta>`, `contracts.receipt <key>` | Use `game.economy` 1.0. See below. |
| `contracts.references <list-ordinal> <row-index>`, `contracts.reference <list-ordinal> <row-index> <reference-index>` | Read script-reference metadata. See below. |

`cancel` and `resume` never erase provider receipts or retry an uncertain effect. Operations are synchronous, so cancelling cannot roll back one that already committed.

### Mission scripts

`contracts.ingest` never reads a file path. The content broker enforces the declared root and link containment. The module admits only `missions/<lowercase-id>.scom`, at most 4096 bytes, in exactly this LF-separated form:

```text
sco-mission 1
phases 2
objectives 1
```

Counts run from 1 to 64. No XML, Lua, includes, network or extra bytes are allowed. The provider validates too. Registrations last for the session, and a changed script pack needs a host restart. The legacy XML script library is kept but not supported here. It is not converted or bundled. The maintainer has not yet approved this narrower format.

### Economy

`contracts.apply` checks the balance and range before it asks. That check grants no payment authority. The provider atomically checks the expected balance and keeps non-evicting owner and session receipts. A replay sends the same request, not a new key. An uncertain effect returns a refusal and is never retried automatically. Receipt flags stay visible, including a dropped advisory notification. Poll the receipt; do not rely on signals. Completing a mission pays nothing.

### Script-reference metadata

`contracts.references` reads one metadata row from `lists/contract_scripts.txt` in an enabled data pack that you supplied. The list ordinal counts within the public `sco.content` List kind, including other list names, so choosing a different name is refused. Ordinals and indices are zero-based and last for the session. Duplicate pack files and duplicate GUID rows stay separate, in provider and file order. There is no pack precedence and no GUID merge. A reply that does not fit returns `TOO_MANY`, never a cut-off success.

The row format keeps the legacy layout:

```text
# Synthetic metadata; these are not game mission IDs or installed scripts.
0123456789abcdef0123456789abcdef Example first.xml|-|second.xml
abcdef0123456789abcdef0123456789 NoReferences -
```

A row is exactly 32 hexadecimal GUID characters, a space, a nonempty name, a space, and nonempty pipe-separated reference tokens. A `-` adds no reference. Other tokens and duplicates keep their byte order and are display text only. LF and CRLF both work, and the last line may lack a line feed. Empty lines and lines that start with `#` are not rows. A NUL, a control byte in a token, an empty reference token or a malformed row refuses the whole file, even when the row you asked for is valid. Limits: 1 MiB per file, 16,384 rows, 8191 bytes per line, 255 bytes per name, 32 references per row, 255 bytes per reference.

The reply names the GUID as metadata, not a public mission offer ID, and reports native installation as unknown. The published mission table has no query for legacy XML installation and no mapping from GUID to offer. Copied bytes do not show that a script was installed, offered, accepted or run. These commands never call the mission or economy providers, ingest, `HaveScript` or private host functions. They work while changes are cancelled and need the normal game-thread, live-owner content admission. Each call reopens and copies the item, and unload keeps nothing.

There is no import of base files and no new pack staging. Supply an ordinary data pack with the host stopped, then restart. The host's `ReadContractScripts` and its XML availability checks are unchanged.

## Storage is not implemented

No `wallet.txt` import, default-wallet restore, timestamp precedence, database migration, reward-once persistence or save exists. No direct filesystem fallback is allowed. The existing contracts database, IDs and legacy wallet and script code are unchanged. Before the feature can be turned on by default it needs an owner-approved, versioned `sco.storage` migration and atomic payment and save coordination. Owner and session receipts are not durable across a reload or restart.

The synthetic fixture uses stand-in public providers. It does not use the game-pack provider or the real owner-release dispatcher.

Pending: a production adapter; full mobiGlas, UI and mission-kind parity; the 15-second automatic offering; actor-death orchestration; wallet precedence, import, save and payout policy; and persistence across reload.

## Preflight against Core

To check against a real Core without a game, build the contracts module against an extracted, verified SDK, and build the matching Core source's `sco_offline_missions`, `sco_host`, `sco_runtime` and `sco_core` libraries. On POSIX, run:

```sh
tools/test_contracts_core_integration.sh <core-source> <core-build> <contracts-module>
```

It exercises Core's real missions and economy tables with a synthetic backend. It checks owner scoping, withdrawal, a failed uncertain balance change and release cleanup. It does not implement partial rewards, durable settlement or native adapters.
