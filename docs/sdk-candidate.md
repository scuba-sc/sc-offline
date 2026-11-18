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

## Current development source

The `external/sco-core` gitlink selects Core `4ba313da3ac76c887cda03772d6106452f33a5d4`:
unpublished development SDK `2.4.1-dev.6`, Star Citizen game pack `0.8.1`, plugin ABI
`1.1`. It adds the process-retained host composition on top of `94dffc5`.
No development archive digests are recorded for this Core yet, and none of the
receipts below qualify it. The tables that follow are earlier pairings, kept as
historical records.

## Previous development pairing: Core e2dc4ac (2.4.1-dev.3)

At product commit `f38dcdd4625e0290f61855074d907a6749ea980c` the gitlink selected
Core `e2dc4ac933b5565417fb067735ddc12104c8030d`, tree
`60f3e544244a8e487b08f32adbd4a81eab344d43`: unpublished Core `2.4.1-dev.3` and
game pack `0.8.1`. The local development archives below have embedded
compatibility metadata naming that exact Core source. They are local artifacts,
not published release assets.

| Profile | Local archive | SHA-256 |
|---|---|---|
| Combined core + game SDK (`legacy`), earlier local build | `sco-sdk-2.4.1-dev.3.zip` | `5b88cc99fe7fa254458aaf6c5bdf3a3318b20611c8fe664a172e0ae210145be4` |
| Combined core + game SDK (`legacy`), reproduced by product `6e20945` preflight | `sco-sdk-2.4.1-dev.3.zip` | `8ed1fe246e44097f89ac79104e01a7dae095a3023e7fac96f14fcf8d9db147af` |
| Core SDK | `sco-sdk-core-2.4.1-dev.3.zip` | `d3f7c27c43d8044dd3beba638af2264fe579984059f45a5285b5fba89ea71a78` |
| Star Citizen game overlay | `sco-sdk-game-sc-0.8.1-sdk-2.4.1-dev.3.zip` | `28e87a3c691419a70b706db23d7e049ea5852c6386be8473444409f816740fea` |

The `8ed1fe24` archive was generated twice by the committed development SDK
preflight at product commit `6e2094583d4864ad3f3b002a289833b582e0fa30`; the
two ZIPs were byte-identical. That run selected clean Core source
`e2dc4ac933b5565417fb067735ddc12104c8030d` (tree
`60f3e544244a8e487b08f32adbd4a81eab344d43`), audited the `legacy` profile,
and verified the full archive inventory and compatibility metadata. The ZIP
contains 374 files: 373 payload files covered by its embedded `SHA256SUMS`,
plus that inventory. `COMPATIBILITY.json` identifies profile `legacy`, Core
and game-SC source `e2dc4ac`, Core SDK `2.4.1-dev.3`, game overlay `0.8.1`,
and plugin ABI `1.1`. The `5b88cc99` archive remains a distinct earlier local
build with the same filename, version label, profile, and source metadata. The
version label does not define a universal archive hash; neither archive is a
published release asset.

The standalone game overlay metadata names the Core archive above by its exact
SHA-256. The combined archive contains both profiles and its metadata names Core
and game-SC source `e2dc4ac`. No `core-source` or `host-source-sc` archive
identity is recorded for this pairing; do not infer those hashes from the
gitlink or from these plugin-kit archives.

The e2 Core source qualification recorded a full Windows RelWithDebInfo build
and CTest run (429 registered, 395 passed, 34 skipped, zero failed). Its SDK
package suite ran 87 tests with one Linux-only skip, and extracted core-source
CTest passed 145/145. Core-only and core-plus-game-overlay development archive
checks each ran 44 tests with two Windows control-byte path/name skips. C# and
Rust checks were not enabled; Lua runtime checks were skipped with
`SCO_SDK_NO_LUA=1`. These checks do not establish native service availability,
product startup, runtime activation or in-game behavior. No game, launcher or
native activation was run; this is not release acceptance.

The local development archives are not substitutes for the published 2.4.0
release assets. The host uses its exact gitlink or an explicit matching
host-source kit; the external plugin proof uses a public SDK archive. Keep
host-source and plugin-archive identities separate, and keep static resolution,
synthetic tests, host integration, native activation and game acceptance
separate.

## Loadout profile candidate composition

The local profile `player-offline-loadout-candidate` selects the 11 plugins from
the stable `player-offline` profile plus enabled Loadout. It is an unpublished,
inspection-only composition. The stable release profile and release gate are
unchanged. Its disabled default remains explicit: `discord` is
disabled; the other eleven entries, including `creative`, are enabled. This local profile does not publish
an SDK or plugin package, approve runtime activation, or qualify upgrades.

Supply real caller-built artifacts for all 12 entries; the tool does not build
or substitute modules. Use a controlled, existing absolute staging parent. Restrict its ACL to intended user accounts and verify that the
parent and generated child are outside any game or launcher installation:

```sh
python -B tools/stage_loadout_candidate.py --artifact-root <absolute-artifact-root> --staging-parent <absolute-existing-non-install-directory>
```

The tool snapshots and validates selected artifacts and tracked product plugin
defaults before writing, then creates a unique `sco-loadout-candidate-*` child
under the supplied parent and prints its path. Candidate files use exclusive
creation: occupied paths are refused and existing files are never replaced. It
reports module hashes but cannot independently establish all binary provenance.
Synthetic peer artifacts appear
only in disposable tests and do not stand in for the real 11 peer binaries.

The inspection tree also snapshots the tracked `data/buildables.txt`,
`data/ships.txt` and `data/npcs.txt` bytes and stages them at their existing pack
paths:

| Tracked source | Candidate path |
| --- | --- |
| `data/buildables.txt` | `data/plugins/build_catalog/lists/buildables.txt` |
| `data/ships.txt` | `data/plugins/picker_catalog/lists/ships.txt` |
| `data/npcs.txt` | `data/plugins/picker_catalog/lists/npcs.txt` |

The legacy top-level `data/buildables.txt` is not copied into the candidate
tree, and no second `buildables.txt` is added to Loadout's optional
`loadout_lists` pack. This keeps the candidate's default Build list to the
single `build_catalog` source. Missing, redirected, changed or conflicting
catalog inputs stop composition before the candidate tree is created.
Product defaults are repository-only, with committed `HEAD` as the trusted
baseline. The tool derives its allowlist from files tracked there, refuses
staged or working-tree changes and changes detected during verification, and
fails on extra untracked or ignored paths beneath `data/plugins` without
opening the extra contents. It stages checkout bytes only after Git's
path-aware clean filters map them back to committed blobs, so checkout line
endings may differ from raw blob bytes.

By default, the items/outfits data pack is absent. To include it, pass
`--lists-source <absolute-directory>` containing caller-supplied `items.txt` and
`outfits.txt`. The tracked Loadout manifest is used unless an explicit
`--lists-manifest <absolute-file>` is given; the manifest flag requires a list
source. The existing no-replace list helper runs with buildables OFF. Conflicting
destinations are refused. If list staging fails late, the already-composed plugin
tree remains and the items/outfits pack may be partial; there is no rollback.
A candidate-file write failure can also leave the unique child incomplete; the
reported path identifies that tree.

The supplied parent must be a pre-existing, nonredirected directory disjoint from
the source, artifact and optional list paths. The parent, its ancestors and
caller-supplied inputs must not be changed concurrently by another process. The
path checks do not defend against hostile Windows parent replacement, and the
tool cannot discover every possible installed game or launcher directory. Check
the selected parent yourself. The tool does not write into an installation or
provide an upgrade-preservation guarantee; this is a fresh inspection tree.

### Loadout consumer proof identity

The Loadout consumer proof at product commit `3e68712` used the combined local
`legacy` archive `sco-sdk-2.4.1-dev.3.zip`, SHA-256
`5b88cc99fe7fa254458aaf6c5bdf3a3318b20611c8fe664a172e0ae210145be4`. Its
embedded metadata identifies Core and game-SC source
`e2dc4ac933b5565417fb067735ddc12104c8030d`. The six synthetic consumer CTest
cases passed (6/6). The follow-on content-host integration check at product
commit `f38dcdd` passed 1/1 against the clean e2 Core source using the then
allowlisted `5b88cc99` archive. These are historical product checkpoints. They
cover SDK consumer tests and that offline host integration case; they do not
establish native activation, full outfit/provider parity or game acceptance.

At product commit `6e2094583d4864ad3f3b002a289833b582e0fa30`, 12 individual
synthetic SDK consumer checks passed against the reproduced `8ed1fe24` archive:
NPC, Inspector, Loadout, Creative, Discord, Crew, Spawn, Build, Mining,
Teleport, Quantum, and the Contracts read slice. The real-Core Loadout host
proof was also run with that exact archive and clean Core source
`e2dc4ac933b5565417fb067735ddc12104c8030d` (tree
`60f3e544244a8e487b08f32adbd4a81eab344d43`); the isolated CTest passed 1/1.
It built the Loadout module against the verified SDK and exercised Core plugin
startup, synthetic list-pack content/reload/read behavior, and shutdown. The 12
individual checks are not the consumer-proof aggregator or its separate list
staging test. None of these synthetic results establishes native activation,
full outfit/provider parity, or game acceptance. At that commit native activation
was refused. Loadout has not been run in game. The `2.4.1-dev.3` label is
unpublished development metadata, not release-status evidence.

A prior Loadout proof used a separate `legacy` archive with the same filename,
`2.4.1-dev.3` label, and SHA-256
`26fc210e9f236f9462a55a54375357abf27ace810c6896cf0b7548fbfaf50529`. Its
metadata identifies Core source
`c591513e194e9f3e0221cde62361a63a95453895`. The six consumer CTest cases, two
clean MSVC dependency traces and zero-failure `sco-plugin-check` result belong
to that exact historical archive. Its digest is not a universal hash for the
version label, does not identify the e2 archive above, and does not qualify the
other 11 profile artifacts.

### Historical 2.4.1-dev.3 coordinates for Core 4e78a6a

These five development archive hashes were recorded for an earlier local Core
source commit `4e78a6a6246636d0c1f2d5f7c16e33f36b1b20dd`, tree
`b80a97a9af8d19c447399922fa0664ecc22d4190`, not the e2 archive identities above.
The version labels match; tell the builds apart by source commit and archive SHA-256.

| Profile | Historical development archive | SHA-256 |
|---|---|---|
| Combined plugin SDK (`legacy`) | `sco-sdk-2.4.1-dev.3.zip` | `f42b481269de82dc3c3c890e9311616335f5817cfb78a7a57b66b6cedc984a7b` |
| Core SDK | `sco-sdk-core-2.4.1-dev.3.zip` | `197e6c3359d6ee6344bd7efe7399e7c81844fc0430c60bb84eedd74aa424d9da` |
| Core source | `sco-sdk-core-source-2.4.1-dev.3.zip` | `80933d1755358af9aaea8c4a63451c67d29dd674392cb7b56692ca404fa6afe4` |
| Game overlay | `sco-sdk-game-sc-0.8.1-sdk-2.4.1-dev.3.zip` | `0951be8668bdafbbeb23a1e3dbeb6a71e9abdfc555286f422c23f769d8fc5905` |
| Host source | `sco-sdk-host-source-sc-2.4.1-dev.3.zip` | `8964e2fec409f2622024630caa4c3c00a0c4041ad7b6722a519f22df5accda75` |

For that historical pair, static sigcheck reported 347/347 on builds 197 and
200, not gameplay or semantic equivalence; build 200 had an observed
collection-layout difference. Native startup and ASOP activation were refused.
Those results do not transfer to the e2 source or archives.

### Previous development pairing

Core `248d2a65ff979909695ece68ec8a0b71cbf26172` and its `2.4.1-dev.2` archives
are historical inputs and do not qualify later pairings:

| Profile | Development archive | SHA-256 |
|---|---|---|
| Combined plugin SDK (`legacy`) | `sco-sdk-2.4.1-dev.2.zip` | `f032b05da7e9e6690f4f63ce251dc5bf75685d64a33e1469f13636d300ae5e45` |
| Core SDK | `sco-sdk-core-2.4.1-dev.2.zip` | `21e0aa2a0f997d3813408bd4c873771580a55d1d82d2eff67db45621c5321d55` |
| Core source | `sco-sdk-core-source-2.4.1-dev.2.zip` | `de732bb29f52d34ecc1b2278bf9ef5419fab7872067d8960a58024cb374998bc` |
| Game overlay | `sco-sdk-game-sc-0.8.0-sdk-2.4.1-dev.2.zip` | `8729e97a8f7199ae1ec0ba05716134ec3a6ac0832c0e99d640f1a0f4bf6d55fc` |
| Host source | `sco-sdk-host-source-sc-2.4.1-dev.2.zip` | `06af4a6e74b70ba6a719c371470bf15b6376d425d66d3de073b07b16ae5ce126` |

Consumer integration, hosted availability, reader/worker exclusion, ownership,
Windows runtime behavior and human gameplay acceptance remain separate gates.

Earlier RC2 and pre-release candidate coordinates are historical evidence only.
