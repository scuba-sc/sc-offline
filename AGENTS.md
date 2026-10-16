# Agent and contributor guide

Rules for anyone changing this repository, human or AI agent (OmO, Codex, Claude, Hermes, cron
reviewers). They are the same in scubamount/sco-core, scubamount/sc-offline and
scubamount/sco-titanlink-northstar. Cite them by number in reviews (`rule 4`). A change that breaks
one is not ready, however well it works. Repository specifics follow the shared list.

## Design principles

| # | Rule | How it is checked |
|---|---|---|
| 1 | **Game-neutral kernel.** `sco_api.h`, the generic `sco_*.h` services, generic `scosdk/`, core `Sco.Sdk` and `sco_lua` never include or name a game header or type. Game services live in the optional game pack and the `game-sc` SDK extension. | sco-core: `tools/ip_audit.py zip --profile core`, CI `kernel-only`, `sdk/test-zip.sh <core.zip>` |
| 2 | **The ABI only grows.** `sco_api.h` 1.x keeps its layout; no field reorder or removal, enum width or calling-convention change; tables stay size-prefixed. | `tests/abi_*.c` and the C# pins |
| 3 | **Products are SDK consumers.** sc-offline's built-ins and the bridges use public SDK headers and game services only. A capability a product needs goes into sco-core first; the product follows and moves its pin. | sc-offline `tools/sdk-headers.py` |
| 4 | **One owner per game function.** A game function is detoured once, by the layer that owns it, which publishes an event; other code subscribes. `sco::hook` refuses a second detour (`already_hooked`), and the second feature silently stops working. | sigcheck addresses of hooked rows are distinct |
| 5 | **Provenance for every distributed file.** A new shipped file gets an entry in sco-core's `docs/legal/provenance.json` (and `sdk/components.json` if it is in the SDK). Nothing `unresolved` ships. | `tools/ip_audit.py repo` |
| 6 | **No game material beyond interoperability facts.** Byte patterns and the names of strings and functions the game references are fine. Game binaries, dumps, extracted data and real record GUID tables in examples are not. | artifact gate; review |
| 7 | **Licence statements stay accurate.** sco-core is GPL-3.0-only, with four MIT wire headers; vendored code keeps its own licence and pinned hashes. Don't write that plugins "are" GPL. | `reuse lint`, `tools/ip_audit.py` |
| 8 | **Evidence before claims.** A PR says what ran: the CI jobs, sigcheck output for row changes, and the in-game result or the words **"not run in game"**. | PR description |
| 9 | **Releases are human acts.** Agents never tag, publish a release, or write a `docs/legal/release-decisions/` approval. | `release.yml` gate |
| 10 | **Small, stacked, reviewable PRs.** One concern per PR; branches are deleted on merge; no long-lived side branches. | repository setting |
| 11 | **Offline only.** No connection to Cloud Imperium Games' servers, no anti-cheat bypass, telemetry or account tampering ([plugin rules](https://github.com/scubamount/sco-core/blob/main/sdk/docs/plugin-rules.md)). | review |
| 12 | **Public hygiene.** No secrets, no personal names or email addresses (commit as `scubamount`, `scubamount@users.noreply.github.com`), no private links. | commit hooks; review |

## Before opening a PR

1. Say which layer the change is in: kernel, game pack, product, plugin or bridge.
2. Run the checks for every rule the change touches, and paste the results into the PR.
3. Update the docs, counts and `CHANGELOG.md` the change affects, in the same PR.
4. List what was not verified, with "not run in game" where it applies.

## Agents

- Read this file and `CONTRIBUTING.md` before planning.
- Don't merge, approve, close, tag, publish, force-push to `main`, or rewrite history.
- Leave other agents' branches and comments alone. Answer a review finding with `fixed in <sha>` or `won't fix: <reason>`.
- If you aren't sure, write "unverified:" rather than asserting.

## This repository: sc-offline

The offline mod built on sco-core (`external/sco-core`, a submodule pin).

- Checks: `tools/check.sh` (0 new diagnostics; not a build), `python3 tools/sdk-headers.py` when `src/builtins/` changes, CI `check`, `build`, `bridges`, `no-scans`, `sdk-headers`.
- Moving the sco-core pin is its own PR; name the sco-core commit or tag and what changed.
- Before hooking a game function, check whether sco-core's game pack already hooks it and publishes an event (`external/sco-core/src/game/`). Subscribe instead (rule 4).
- Behaviour in game is unproven until someone runs it: say "not run in game" and list the `mod.log` lines that would prove it.
- Releases are tagged by the maintainer after an in-game check.
