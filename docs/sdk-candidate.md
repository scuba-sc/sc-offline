# SDK 2.4.0 release coordinates and consumer qualification

The following section records the historical published stable SDK 2.4.0
release, tag [`sdk-v2.4.0`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0).
The annotated tag peels to Core source `6106fa3639cbcc28f78a1ded8de2707de019f4d3`.
Candidate run [41342490537](https://github.com/scuba-sc/sco-core/actions/runs/41342490537)
and publication run [41343086992](https://github.com/scuba-sc/sco-core/actions/runs/41343086992)
both completed successfully at that exact source. The release manifest records
candidate artifact `13098182566`, the source identity, the components manifest hash,
all payload sizes and digests, and the five SDK profiles.

## Published archive identities

All five ZIPs below were independently checked against the release's `SHA256SUMS`
and `release.json`, then regenerated from a clean checkout of the exact `6106fa36`
source and byte-compared with the published release assets.

| Profile | Published archive | SHA-256 | Files |
|---|---|---|---:|
| Combined core + game SDK (`legacy`) | `sco-sdk-2.4.0.zip` | `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc` | 355 |
| Core SDK | `sco-sdk-core-2.4.0.zip` | `d2a8313ec5e3a7ae9ff95c253841b40e75454bec9799dccba5cbab9346432a7b` | 158 |
| Core source | `sco-sdk-core-source-2.4.0.zip` | `df756719da0de54ead18973258d3bc35640e26d44088b3b6ffcf8672ebb1ee51` | 443 |
| Star Citizen game overlay | `sco-sdk-game-sc-0.8.0.zip` | `a5881e321bb456a3a17894413a07ece9ce6d3787bd6d1ad7dcc34fc288951f6b` | 203 |
| Star Citizen host source | `sco-sdk-host-source-sc-2.4.0.zip` | `a0aaaba07e3d4eb0d8c264912cd6f2ab23f0d237a99fa07816005790a76efc4c` | 986 |

## Current coordinated development source

The product gitlink selects Core `d33932bc14b85af0caccc4636b42028f44f97710`, tree `c7563a009994c07598edae124608053dde85a92a`.
This unpublished development candidate repairs SDK archive member mode preservation.
Its locally built and audited `2.4.1-dev.1` combined plugin archive has SHA-256
`c24f81580b6b1f95bbd1a55f4346a75c7b3512b7d09755981dd0c795734aed1f`;
its host-source archive has SHA-256
`241c628f00bc00c315031a9e2c16b5bf1b8d9624eaad67a809cb3c2ad32e3fe8`,
and its core-source archive has SHA-256
`01d1ec68a60042f126aa3691fd64045534875cf5c552709d7b8e1f1b86fda745`.
Consumer pairing and language qualification remain separate; these are not
release coordinates. Native startup remains refused.

The development host uses the exact `external/sco-core` gitlink,
which is newer than this historical release. Current development plugin proofs
must use the matching source-identified development `legacy` archive. The published
`legacy` archive and its release checksum inventory remain historical inputs. The separate host-source
archive is available to explicit `SCO_HOST_SOURCE_DIR` development overrides;
its digest is recorded here, but it is not the default embedded-host input. The
profiles have different roles and their headers must not be overlaid. The
preflight and hosted consumer matrix check the official package coordinates
before use.

The SDK release package verification does not establish native reader exclusion,
production ownership of the game image/resolver, Windows runtime behavior, or
gameplay acceptance. Product native startup and ASOP native activation remain
refused pending the required reader/worker exclusion and drain proof. Static
signature metadata targets game build 4.10.197.2878 and remains pending in-game
verification. CI and synthetic fixtures prove only the build paths and contracts
they exercise.

Earlier RC2 and pre-release candidate coordinates are historical evidence only;
they are not current consumer pins or substitutes for the stable archive digests
above.
