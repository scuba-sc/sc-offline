# SDK 2.4.0 candidate adoption

**Current development input (2026-11-01):** the embedded host and candidate consumer checks pin exact sco-core source `44ffd1290e261e12684b38ac862ec0c4f08921df`. Core workflow run [41332377573](https://github.com/scuba-sc/sco-core/actions/runs/41332377573) produced release-candidate artifact `13093044361`, whose ZIP digest is `464b7ca2f05f402cfe5581b2a34033c169a977cf5493a396a36a7c63dc0c2ddc`. This is candidate evidence, not final SDK release promotion.

## Exact candidate package coordinates

The candidate artifact contains the following SDK packages. They were independently extracted, checksum-verified, regenerated from exact core source `44ffd129`, and byte-compared against the downloaded artifact:

| Package | SHA-256 | Archive inventory |
|---|---|---:|
| Combined SDK, `sco-sdk-2.4.0.zip` | `bcd5c946ed8658214571d95bb79df594c9ea83748f63869d1963ef1fa77f51d9` | 355 files |
| Core source, `sco-sdk-core-source-2.4.0.zip` | `807a322eab2b3d8c25e10c3582008ec3ec50b6b536bc947f2b28cf4b63095c22` | 442 files |
| Star Citizen host source, `sco-sdk-host-source-sc-2.4.0.zip` | `799511bfa08a7bf77a46fcf8262d3cac4a5f0e6a0ef04a28f53e6e624c01e8a0` | 985 files |

Each package compatibility descriptor records the exact source commit. All archive SHA256SUMS passed. The combined SDK, core-source and host-source archives passed the core IP/provenance audits. The host-source kit contains the owned signature snapshot and detailed host report/stop interfaces used by the synthetic consumer fixtures.

This package check does not establish a native reader barrier, production ownership of game image/resolver code, Windows runtime behavior or gameplay acceptance. The offline product startup preflight remains unconditionally unavailable. External plugin production defaults remain separately pinned to the published RC2 archive below.

## Published external-plugin default

SC Offline's external plugin jobs still consume the published SDK 2.4.0 RC2 release: [`sdk-v2.4.0-rc.2`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2). Its source is `844e136ccb280ad8cb43f73bc43c9447e8f6799d`, and its combined archive `sco-sdk-2.4.0.zip` has SHA-256 `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`. The candidate combined archive above is a distinct file with the same versioned basename; never confuse its hash or source with the published RC2 input.

The host builds from the exact committed source submodule. Plugin proof jobs use verified published archives, not source-checkout include paths. Host-source and plugin SDK profiles have different roles; do not overlay their headers. A candidate host build alone does not prove that RC2-built external plugins work with the candidate host.

## Scope and remaining evidence

The candidate keeps the SDK version at 2.4.0, plugin ABI at 1.1, and Star Citizen game-pack version at 0.8.0. Static signature metadata targets game build 4.10.197.2878 and remains pending in-game verification. The release candidate workflow artifact and this development pin do not create a final release or authorize native activation.

Windows x64/MSVC binary checks, consumer-specific Linux/Windows fixture runs, release assembly, real launcher restoration, renderer checks and in-game smoke must each be stated separately. No game, launcher or driver settings are changed by source checks. CI and synthetic tests prove only the contracts and build paths they actually exercise.
