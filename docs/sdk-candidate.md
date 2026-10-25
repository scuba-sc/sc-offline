# SDK 2.4.0 candidate adoption

SC Offline consumes the latest available published candidate as checked on
2026-10-25: [`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1).
Its tag and source are pinned explicitly, independently of GitHub's release flag.

| Coordinate | Pin |
|---|---|
| Product base | `f2ac58da7800fc23c458f73867f9b473b4b25608` |
| Previous embedded host | `e616ed69d8f9f77b0f4827881c86d91e4a7d1689` (post-2.3 source) |
| Candidate tag | `sdk-v2.4.0-rc.1` |
| Embedded host source / candidate target | `48d7055ef49e1b073ec59e4ce8ded7a6006329eb` |
| SDK / plugin ABI / Star Citizen game pack | `2.4.0` / `1.1` / `0.8.0` |
| External plugin archive | `sco-sdk-2.4.0.zip` |
| Archive SHA-256 | `124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b` |
| Published host-source alternative | `sco-sdk-host-source-sc-2.4.0.zip` |
| Host-source archive SHA-256 | `5ca76ff9dac6bbaa183c6f308d610e08d9c1b0daf6ebda6458e222277406dfc6` |
| Static signature target | Star Citizen `4.10.197.2878`; not gameplay evidence |

The product builds the embedded host from the exact candidate source submodule,
which preserves its existing source-build model. It does not silently select
newer `main`. External consumers retain the existing published combined archive
pin and verify both its release checksum and internal file inventory. Host source
and plugin SDK archives have different roles; do not overlay their headers.

No plugin feature behavior is added by this update. The release workflow stages
11 external folders (creative, discord, crew, spawn, build, mining, teleport,
quantum, inspector, npc, contracts), with creative/Discord disabled. Loadout
staging, optional bridges and multiplayer are deferred. New SDK tables do not
authorize turning on native backends: required versions, complete member checks,
offline admission and unavailable/default-off behavior remain in force.

## Evidence and release limits

Source adoption is separate from final SDK publication approval. The candidate's
[release scope](https://github.com/scuba-sc/sco-core/blob/48d7055ef49e1b073ec59e4ce8ded7a6006329eb/docs/releases/sdk-v2.4.0.md)
still describes an unsigned development candidate, even though its archives are
published. It provides no final signed approval or in-game acceptance.

The SDK release review also identified a distribution-policy conflict around
`native_index.json` and needs-review attribution for generated catalogue/guides.
This update neither resolves those upstream records nor asserts legal/provenance
approval. Product packaging does not copy the SDK archive or native index into the
player ZIP. A final product release still requires maintainer resolution of
applicable distribution questions, exact-source CI and separate acceptance.

Synthetic plugin proofs, the public-header gate and the portable host build can
show contract/build compatibility. Windows x64/MSVC binary verification, release
assembly, real launcher restoration, renderer and in-game smoke must each be
reported explicitly. No game, launcher or driver settings are changed by running
the source checks. No tag, release, merge or gameplay certification is authorized
by this adoption document.
