# SDK coordinates

Which sco-core SDK sc-offline builds against, and how to check an archive before you use it. Keep three identities apart: the Core source the host builds from (the `external/sco-core` gitlink), the SDK archive plugin proofs use, and the game. Hash and version labels do not replace each other.

## Target for 0.8.0: SDK 2.4.1 (not yet published)

sc-offline 0.8.0 is built for sco-core SDK **2.4.1**, game pack **0.8.1**, plugin ABI **1.1**, game build **4.10.200.58746**. The release job refuses to package until the owner publishes SDK 2.4.1 and the `external/sco-core` pin matches it. The pin currently selects a development revision of the 2.4.1 line.

The consumer proofs for 0.8.0 ran against a local, unpublished development archive (`--candidate`, profile `legacy`):

| Item | Value |
|---|---|
| Archive | `sco-sdk-2.4.1.zip` |
| SHA-256 | `634ec29627646c37200d4cc570fed1fb5d487bfb3b161fc1271581e491bc4aa7` |
| Core source (`COMPATIBILITY.json`) | `5858fdb65bf816ae28133d14683828ad63e451b3` |
| Proofs that passed | inspector, crew, npc, spawn, build, loadout, mining, discord, teleport, quantum, contracts (read slice). Synthetic, MSVC 2022. |

These are development receipts. When the SDK is published, the release's own `SHA256SUMS` and `release.json` replace them. Not run in game; see [status.md](status.md). Earlier development archives (`2.4.1-dev.2`, `2.4.1-dev.3` and `2.4.1-dev.6`) are superseded, and their hashes are in this file's git history.

## Published SDK 2.4.0

Tag [`sdk-v2.4.0`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0). The annotated tag peels to Core source `6106fa3639cbcc28f78a1ded8de2707de019f4d3`. The candidate run ([41342490537](https://github.com/scuba-sc/sco-core/actions/runs/41342490537)) and the publication run ([41343086992](https://github.com/scuba-sc/sco-core/actions/runs/41343086992)) both finished successfully at that source. The release manifest records the candidate artifact (`13098182566`), the source identity, the components manifest hash, all payload sizes and digests, and the five SDK profiles.

The five archives were checked against the release's `SHA256SUMS` and `release.json`, then rebuilt from a clean checkout of `6106fa36` and compared byte for byte with the published assets.

| Profile | Archive | SHA-256 | Files |
|---|---|---|---:|
| Combined core and game SDK (`legacy`) | `sco-sdk-2.4.0.zip` | `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc` | 355 |
| Core SDK | `sco-sdk-core-2.4.0.zip` | `d2a8313ec5e3a7ae9ff95c253841b40e75454bec9799dccba5cbab9346432a7b` | 158 |
| Core source | `sco-sdk-core-source-2.4.0.zip` | `df756719da0de54ead18973258d3bc35640e26d44088b3b6ffcf8672ebb1ee51` | 443 |
| Star Citizen game overlay | `sco-sdk-game-sc-0.8.0.zip` | `a5881e321bb456a3a17894413a07ece9ce6d3787bd6d1ad7dcc34fc288951f6b` | 203 |
| Star Citizen host source | `sco-sdk-host-source-sc-2.4.0.zip` | `a0aaaba07e3d4eb0d8c264912cd6f2ab23f0d237a99fa07816005790a76efc4c` | 986 |

The external plugin jobs in CI use the combined 2.4.0 archive, not the host pin. The host builds from its gitlink or from an explicit matching host-source kit (`-DSCO_HOST_SOURCE_DIR`, see [host composition](host-composition.md)).

## Check an archive

Proof scripts such as `tools/check_inspector_sdk.py` verify the archive's SHA-256 against a checksum list, extract it into a fresh folder, check the extracted inventory, and build the module against only the extracted `include/`. To use an unpublished archive, pass the explicit options listed in [SDK workflows](sdk-workflows.md). These checks bind an identity to the archive's contents. They do not verify source rights or release authority.

## Loadout profile candidate composition

The profile `player-offline-loadout-candidate` in `tools/release-plugin-profiles.json` is the 11 plugins of `player-offline` plus Loadout, 12 in all. Discord is disabled; the other eleven are enabled. It is for inspection only. It does not change the release profile, publish a package, or approve anything.

Run the staging tool with real, caller-built artifacts for all 12 entries. The tool builds nothing. Choose an existing absolute staging parent outside any game or launcher folder, and restrict its access to your own account:

```sh
python -B tools/stage_loadout_candidate.py --artifact-root <absolute-artifact-root> --staging-parent <absolute-existing-non-install-directory>
```

The tool validates the artifacts and the tracked plugin defaults, then creates a unique `sco-loadout-candidate-*` folder under the parent and prints its path. Files are created exclusively. The tool refuses an occupied path and never replaces a file. It reports module hashes. It cannot establish where each binary came from.

It also copies the tracked list files to their pack paths:

| Tracked source | Path in the staged tree |
| --- | --- |
| `data/buildables.txt` | `data/plugins/build_catalog/lists/buildables.txt` |
| `data/ships.txt` | `data/plugins/picker_catalog/lists/ships.txt` |
| `data/npcs.txt` | `data/plugins/picker_catalog/lists/npcs.txt` |

The top-level `data/buildables.txt` is not copied, and Loadout's optional `loadout_lists` pack gets no second `buildables.txt`, so the Build list has one source. A missing, redirected, changed or conflicting list stops the run before the folder is created.

Product defaults come from committed `HEAD`. The tool refuses staged or working-tree changes, changes made during the run, and extra untracked or ignored paths under `data/plugins` (it does not open them). It stages checkout bytes only after Git's clean filters map them back to the committed blobs, so line endings in the checkout may differ from the raw blobs.

The items and outfits pack is left out by default. To include it, pass `--lists-source <absolute-directory>` holding `items.txt` and `outfits.txt`. The tracked Loadout manifest is used unless you pass `--lists-manifest <absolute-file>`, which needs a list source. The no-replace list helper runs with buildables off, and conflicting destinations are refused. If list staging fails late, the plugin tree stays and the items and outfits pack can be partial; there is no rollback. A write failure can likewise leave the staged folder incomplete. The printed path names it.

The parent must exist, must not be a redirect, and must be separate from the source, artifact and list paths. Nothing else may change these paths while the tool runs. The path checks do not stop a hostile replacement of the parent on Windows, and the tool cannot find every installed game or launcher folder, so check the parent yourself. The tool never writes into an installation. This is a fresh inspection tree, not an upgrade.

## Loadout consumer proofs

The Loadout consumer ran six synthetic CTest cases against a development `legacy` archive, and a content-host check ran 1 of 1 against a clean Core source, using `tools/test_loadout_content_host.ps1`. Both are synthetic. Loadout has not been run in game, and neither check shows full outfit or provider parity.
