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

The product gitlink selects local Core `4e78a6a6246636d0c1f2d5f7c16e33f36b1b20dd`, tree `b80a97a9af8d19c447399922fa0664ecc22d4190`.
This unpublished `2.4.1-dev.3` checkpoint adds the build-200 signature and
metadata candidate to the earlier lifecycle, storage, diagnostics and widget
work. Hosted fetch and consumer CI availability are not established.

| Profile | Development archive | SHA-256 |
|---|---|---|
| Combined plugin SDK (`legacy`) | `sco-sdk-2.4.1-dev.3.zip` | `f42b481269de82dc3c3c890e9311616335f5817cfb78a7a57b66b6cedc984a7b` |
| Core SDK | `sco-sdk-core-2.4.1-dev.3.zip` | `197e6c3359d6ee6344bd7efe7399e7c81844fc0430c60bb84eedd74aa424d9da` |
| Core source | `sco-sdk-core-source-2.4.1-dev.3.zip` | `80933d1755358af9aaea8c4a63451c67d29dd674392cb7b56692ca404fa6afe4` |
| Game overlay | `sco-sdk-game-sc-0.8.1-sdk-2.4.1-dev.3.zip` | `0951be8668bdafbbeb23a1e3dbeb6a71e9abdfc555286f422c23f769d8fc5905` |
| Host source | `sco-sdk-host-source-sc-2.4.1-dev.3.zip` | `8964e2fec409f2622024630caa4c3c00a0c4041ad7b6722a519f22df5accda75` |

The game overlay binds the exact core archive; do not overlay either plugin kit
onto host-source or source-checkout headers. Plugin ABI is 1.1; the game pack is
0.8.1. The host uses its exact gitlink or an explicit matching host-source kit.
The published 2.4.0 profiles and checks remain historical; the development
archives are local and unpublished. Static sigcheck reports 347/347 on 197 and
200, not gameplay or semantic equivalence. The 200 hauling target has an observed
collection-layout difference. Native startup and ASOP activation remain refused.

### Previous development pairing

Core `248d2a65ff979909695ece68ec8a0b71cbf26172` and its `2.4.1-dev.2` archives
are historical inputs; they do not qualify the new tuple:

| Profile | Development archive | SHA-256 |
|---|---|---|
| Combined plugin SDK (`legacy`) | `sco-sdk-2.4.1-dev.2.zip` | `f032b05da7e9e6690f4f63ce251dc5bf75685d64a33e1469f13636d300ae5e45` |
| Core SDK | `sco-sdk-core-2.4.1-dev.2.zip` | `21e0aa2a0f997d3813408bd4c873771580a55d1d82d2eff67db45621c5321d55` |
| Core source | `sco-sdk-core-source-2.4.1-dev.2.zip` | `de732bb29f52d34ecc1b2278bf9ef5419fab7872067d8960a58024cb374998bc` |
| Game overlay | `sco-sdk-game-sc-0.8.0-sdk-2.4.1-dev.2.zip` | `8729e97a8f7199ae1ec0ba05716134ec3a6ac0832c0e99d640f1a0f4bf6d55fc` |
| Host source | `sco-sdk-host-source-sc-2.4.1-dev.2.zip` | `06af4a6e74b70ba6a719c371470bf15b6376d425d66d3de073b07b16ae5ce126` |

Consumer integration, hosted availability, reader/worker exclusion, ownership,
Windows runtime behavior and human gameplay acceptance remain separate gates.

Earlier RC2 and pre-release candidate coordinates are historical evidence only;
they are not current consumer pins or substitutes for the stable archive digests
above.
