# Contracts: mission and balance commands

Contracts reads mission offers and your aUEC balance through the game's public services and, when the game allows it, accepts, advances and abandons missions and applies balance changes. It is command-only: it has no menu page and no key.

In 0.8.0 every command that changes something refuses. The mission and economy capabilities (`game.missions.read`, `game.missions.accept`, `game.missions.progress`, `game.missions.scripts`, `game.economy.read`, `game.economy.apply`) are off in Core and the launcher has no switch for them. The reference commands, `contracts.cancel` and `contracts.resume` need no provider and work.

## Commands

Pages, keys and other plugins call commands through the host's command registry (`sco_api.invoke`). Ids are positive signed 64-bit integers. Phase and objective numbers start at zero.

| Command | What it does |
| --- | --- |
| `contracts.status` | Summary of the current offers and missions. |
| `contracts.offer <index>` | Shows one offer. |
| `contracts.mission <id>` | Shows one mission. |
| `contracts.accept <offer-index> <key>` | Accepts an offer. A replay with the same non-zero key is safe; an altered replay refuses. |
| `contracts.objective <id> <expected-phase> <expected-objective>` | Completes an objective, if the mission is in the state you expect. |
| `contracts.advance <id> <expected-phase>` | Moves a mission to the next phase, if it is in the phase you expect. |
| `contracts.abandon <id>` | Abandons a mission you own. |
| `contracts.ingest <script-ordinal>` | Registers a mission script from a data pack. Only `missions/<lowercase-id>.scom`, at most 4096 bytes: the lines `sco-mission 1`, `phases N` and `objectives N` with N from 1 to 64. |
| `contracts.balance` | Reads your aUEC balance. |
| `contracts.apply <key> <expected-balance> <signed-delta>` | Changes the balance if it equals the expected value. A replay with the same key is safe. |
| `contracts.receipt <key>` | Looks up the receipt for a key. |
| `contracts.cancel`, `contracts.resume` | Block or reopen accept, progress and ingest for this session. Reads, receipts and abandon stay available. |
| `contracts.references <list-ordinal> <row-index>`, `contracts.reference <list-ordinal> <row-index> <reference-index>` | Read the script-reference rows of a `lists/contract_scripts.txt` file in a data pack. The rows are labels, not mission ids, and say nothing about whether a script is installed. |

Nothing retries by itself. A refusal with an unclear effect is reported as such, and no reward is paid when a mission completes. Receipts last for the session only, so they do not survive a restart. Saving the balance and the mission state is not implemented; see [docs/contracts-sdk.md](../../docs/contracts-sdk.md) for the limits.

## Settings

None. Pack files are read through `sco.content` and need a game restart to change.

## What it needs

- Services: `game.missions` 1.0, `game.economy` 1.0, `sco.content` 1.0.
- Capabilities per command: `game.missions.read` (status, offer, mission, accept), `game.missions.accept` (accept, abandon), `game.missions.progress` (objective, advance), `game.missions.scripts` (ingest), `game.economy.read` (balance, receipt), `game.economy.apply` (apply).

Not run in game. See [docs/status.md](../../docs/status.md).

## Developer notes

Build against the extracted SDK 2.4.1 `include/` directory, which must hold `sc_missions.h` and `sc_economy.h`.

```sh
cmake -S plugins/contracts -B build-contracts -DSCO_SDK_INCLUDE_DIR=<sdk>/include -DSCO_WORKFLOW_TESTS=ON
cmake --build build-contracts --config Release --target sc-offline-contracts
ctest --test-dir build-contracts -C Release --output-on-failure
cmake --install build-contracts --config Release --prefix <stage> --component contracts
```

`python tools/contracts_sdk_check.py --archive <sdk zip> --sums <SHA256SUMS> --work <new folder> --slice <read|accept|progress|script|economy|metadata>` runs one slice from a verified SDK archive. Details and the other options are in [docs/contracts-sdk.md](../../docs/contracts-sdk.md).
