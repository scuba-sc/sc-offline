# SDK 2.4.0 candidate adoption

**Development override, 2026-10-31:** the embedded gitlink is locally updated to
sco-core #304 `1b6782bc5c8b7d189cdff4c83e81957f09af35d7` for coordinated
integration. That draft is not RC2 or a release. The archive coordinates below
remain the separate external-plugin inputs; no archive or release is implied by
the gitlink. The historical RC2 source coordinate below describes the previous
embedded pin, not the current development host.

SC Offline consumes the latest available published candidate as checked on
2026-10-28: [`sdk-v2.4.0-rc.2`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2).
The tag and source are pinned explicitly. GitHub lists this release as published
and not a draft, with `prerelease=false`; the `-rc.2` tag still identifies it as
a release candidate. This differs from sco-core's release checklist, which says
RC tags publish as prereleases.

| Coordinate | Pin |
|---|---|
| Product base | `f2ac58da7800fc23c458f73867f9b473b4b25608` |
| Previous embedded host | `e616ed69d8f9f77b0f4827881c86d91e4a7d1689` (post-2.3 source) |
| Candidate tag | `sdk-v2.4.0-rc.2` |
| Embedded host source / candidate target | `844e136ccb280ad8cb43f73bc43c9447e8f6799d` |
| SDK / plugin ABI / Star Citizen game pack | `2.4.0` / `1.1` / `0.8.0` |
| External plugin archive | `sco-sdk-2.4.0.zip` |
| Archive SHA-256 | `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e` |
| Published host-source alternative | `sco-sdk-host-source-sc-2.4.0.zip` |
| Host-source archive SHA-256 | `3d711463f7dff3dcd372ffb70b09951ecf21f1d0e2c5f0a899cccbb9977b8201` |
| Static signature target | Star Citizen `4.10.197.2878`; not gameplay evidence |

The product builds the embedded host from the exact development source submodule;
it does not silently select newer `main`. External consumers retain the existing published combined archive
pin and verify both its release checksum and internal file inventory. Host source
and plugin SDK archives have different roles; do not overlay their headers.

The embedded development host and CI consumer-preflight source are both
`1b6782bc5c8b7d189cdff4c83e81957f09af35d7`. The locally packaged legacy
preflight archive is locked to SHA-256
`bb52b8bf1cab5aea09939e68a387705149411ce6c8af48e3f3f2576fc8a83c31`; it is
not a published archive. The separate external-plugin input remains the published
RC2 archive and digest in the table. The preflight
fixtures and the RC2 archive's own plugin checker do not execute RC2-built
plugins on the #304 host. A mixed-pair synthetic integration test and Windows
package validation remain required before claiming compatibility or release
readiness; no automatic fallback to another SDK is authorized.

No plugin feature behavior is added by this update. The release workflow stages
11 external folders (creative, discord, crew, spawn, build, mining, teleport,
quantum, inspector, npc, contracts), with creative/Discord disabled. Loadout
staging, optional bridges and multiplayer are deferred. New SDK tables do not
authorize turning on native backends: required versions, complete member checks,
offline admission and unavailable/default-off behavior remain in force.

## Evidence and release limits

This development adoption is not the final SDK 2.4.0 release, and makes no gameplay
acceptance claim. The merged sco-core [release-policy change in #284](https://github.com/scuba-sc/sco-core/pull/284)
uses the protected GitHub `release` environment and verifies the selected
candidate run, artifact, tag and source, profiles, checksums, build attestations,
and downloaded draft assets. It requires no separate per-file release decision
or maintainer SSH signature. `needs-review` provenance metadata is informational;
audits still block unresolved or prohibited material, and unknown or disputed
source rights must remain recorded as unresolved. Required licence notices and
attribution remain in the published packages.

Product packaging does not copy the SDK archive or `native_index.json` into the
player ZIP. Windows x64/MSVC binary verification, release assembly, real launcher
restoration, renderer and in-game smoke must each be reported explicitly. No game,
launcher or driver settings are changed by running the source checks. This
adoption document authorizes no sc-offline publication, merge or gameplay
certification.

Synthetic plugin proofs, the public-header gate and the portable host build can
show contract/build compatibility. Windows x64/MSVC binary verification, release
assembly, real launcher restoration, renderer and in-game smoke must each be
reported explicitly. No game, launcher or driver settings are changed by running
the source checks. No tag, release, merge or gameplay certification is authorized
by this adoption document.
