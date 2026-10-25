# sc-offline

**An offline Star Citizen mod, built on [sco-core](https://github.com/scuba-sc/sco-core).**

[![Latest release](https://img.shields.io/github/v/release/scuba-sc/sc-offline?style=flat-square&label=release&color=2ec4d6)](https://github.com/scuba-sc/sc-offline/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/scuba-sc/sc-offline/build.yml?branch=main&style=flat-square&label=build)](https://github.com/scuba-sc/sc-offline/actions/workflows/build.yml)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-f0851f?style=flat-square)](LICENSE)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/NJKeVfYCCC)

sc-offline brings the launcher, offline game session and mod interface together. It is also a reference consumer of the sco-core SDK: a real product built on the same public contracts available to other plugin authors.

[**Download**](https://github.com/scuba-sc/sc-offline/releases/latest) · [**Discord**](https://discord.gg/NJKeVfYCCC) · [**SDK documentation**](https://scuba-sc.github.io/sco-core/) · [**Changelog**](CHANGELOG.md) · [**Issues**](https://github.com/scuba-sc/sc-offline/issues)

> [!WARNING]
> Using this mod may breach the Star Citizen EULA and Terms of Service, and it may get your account banned. Use it only offline, never on Cloud Imperium Games' servers. You are responsible for your own account. Before returning online, close the game and [check cleanup](#go-back-online).

## Built with sco-core

The project separates the **product**, the **framework** and its **extensions**:

| Layer | Responsibility |
| --- | --- |
| **sc-offline** | The launcher, `dinput8.dll` bootstrap, configuration and menu shell. It connects the game session to the framework and packages the product. |
| **sco-core** | The shared runtime: plugin loading, commands, events, services, settings, storage and UI contracts. Its Star Citizen game pack owns the shared game-integration layer. |
| **Feature plugins** | Independently built modules that use public SDK services rather than depending on the product's private internals. |

This keeps reusable infrastructure in sco-core and product behavior in sc-offline. When a plugin needs a missing capability, the intended path is to add the SDK contract/provider in sco-core, then consume it here—not introduce another private implementation in the plugin.

**The migration is ongoing.** External plugins already follow this boundary; remaining host-side feature engines and exceptions are documented in the [architecture](docs/architecture.md) and [verification status](docs/status.md). This is a reference consumer in development, not a claim that every legacy path has been externalized.

### From SDK contract to installed plugin

The development loop follows the [sco-core SDK build/check/install guide](https://github.com/scuba-sc/sco-core/blob/main/sdk/README.md) and its [SDK → MCP → plugin tutorial](https://github.com/scuba-sc/sco-core/blob/main/docs/sdk-mcp-tutorial.md):

1. **Discover the contract.** Read the API catalogue or query sco-core-mcp for service versions, capabilities, ownership and lifecycle requirements before writing a consumer.
2. **Build against the SDK.** Verify a published SDK archive and compile external plugins against its public headers in an isolated build, without product-private include paths.
3. **Check the module.** Use `sco-plugin-check` and focused module fixtures to exercise registration, missing services and cleanup. Synthetic checks are development evidence, not gameplay validation.
4. **Install as a folder.** Package the module with its `plugin.ini` under `data/plugins/<id>/`, then enable and validate it in the matching Windows host.

The **host and plugin builds are separate**: this repository builds the Windows host from the source revision pinned in `external/sco-core`; plugin SDK archives supply the external consumers. sco-core also publishes a separate [host-source kit](https://github.com/scuba-sc/sco-core/blob/main/docs/host-source-sc.md). Host, SDK package, plugin ABI and individual plugin versions are separate coordinates.

For the actual commands and worked examples, start with [sc-offline's SDK workflows](docs/sdk-workflows.md). For a source build, follow [Build](docs/build.md).

## Quick start

You need your own installed copy of Star Citizen and **64-bit Windows**. Linux through Wine is [experimental](docs/linux.md).

1. Download **`sc-offline-<version>.zip`** from [Releases](https://github.com/scuba-sc/sc-offline/releases/latest), not GitHub's source-code zip.
2. Extract it into its own folder and keep the files together. Do not copy the DLL into the game folder yourself.
3. Close the game, open **`sc-offline.exe`** normally and click **Play**. Approve the setup helper's administrator prompt.
4. Let initialization finish. The launcher holds the game at its first instruction while the mod starts (up to 60 seconds by design), so a pause of roughly 15 seconds before the game window appears is expected. Then **M** is meant to open the menu in game; opening the menu has **not been confirmed in game** on a current release ([Status](docs/status.md)).

Use the launcher's **Plugins** page to manage extensions. Feature availability depends on the release, game build and enabled services; see [Features](docs/features.md) and [Status](docs/status.md), rather than assuming `main` and a downloaded release are identical.

## What has actually been run

Read this before relying on any feature description. Only the owner runs the game; a green build, a synthetic check or a resolved signature is never a game result.

- **Observed in game (once or twice, startup only):** v0.7.8.1 offline startup on two Windows PCs: offline evidence line clear, every offline patch applied, signatures resolved, plugins loaded, the game loading into the universe. Play beyond loading in, the menu and every feature below are **not verified in game**.
- **Checked on a development machine only:** the external plugins build against published SDK archives and pass the SDK's plugin checker with no game services. That is a build and contract check, not gameplay.
- **Current pins:** the Windows host and external plugins target sco-core SDK 2.4.0 release candidate 1 ([`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1)). The embedded host is pinned to exact candidate source `48d7055ef49e1b073ec59e4ce8ded7a6006329eb`; external plugins use the checksum-pinned published `sco-sdk-2.4.0.zip`. Plugin ABI 1.1 is unchanged, Star Citizen game pack is 0.8.0, and static signature evidence targets game build 4.10.197.2878. This is candidate adoption, with final signed SDK approval and in-game acceptance still pending. New or default-off services keep their SDK refusal boundaries.

Details and the list of owed in-game checks: [Status](docs/status.md).

## Go back online

The launcher records its session changes and arranges cleanup when the game exits. After a crash or power loss, **close the game**, check **Status**, and use **Uninstall** if anything remains. Check again before returning online.

```powershell
.\sc-offline.exe status
.\sc-offline.exe uninstall
.\sc-offline.exe status
```

**Green** means the mod is out of the game folder and the launcher's recorded PC changes are undone. It describes tracked local state, not account safety or unrelated manual changes. Keep the default protections enabled; [the launcher guide](docs/launcher.md#pc-changes) explains firewall/EAC handling and restoration.

## Troubleshooting

Start through `sc-offline.exe`, and turn **NVIDIA Smooth Motion off** for Star Citizen if you use an NVIDIA GPU. For a bug report, include the mod version, game build and matching `data/launcher.log`, `data/mod.log` and `Game.log`; review logs for personal information before posting. See [launcher guidance](docs/launcher.md) or [open an issue](https://github.com/scuba-sc/sc-offline/issues/new/choose).

Build success and resolved signatures do not establish gameplay correctness. Outstanding Windows/in-game checks are recorded in [Status](docs/status.md).

## Documentation

| Start here | Covers |
| --- | --- |
| [SDK guide](https://github.com/scuba-sc/sco-core/blob/main/sdk/README.md) · [SDK reference](https://scuba-sc.github.io/sco-core/) | Public contracts, tools, examples and plugin development |
| [SDK workflows](docs/sdk-workflows.md) · [Build](docs/build.md) | Build, check and install consumers; build the Windows host |
| [Architecture](docs/architecture.md) · [Status](docs/status.md) | Layer boundaries, migration progress and verification limits |
| [Launcher](docs/launcher.md) · [Linux](docs/linux.md) | Setup, configuration, updates, recovery and experimental Wine support |
| [Features](docs/features.md) · [Data files](docs/data-files.md) | User-facing capabilities, catalogs and saves |

Offline play is the scope: no connection to CIG servers or online services, no anti-cheat bypass, no telemetry and no account or entitlement changes (see [CONTRIBUTING](CONTRIBUTING.md) and [SECURITY](SECURITY.md)). Optional co-presence lives separately in [sc-mp](https://github.com/scuba-sc/sc-mp); it and the optional [cross-game bridges](docs/bridges.md) are excluded from the standard release.

## Credits and license

Based on **ChrisWareOffline 0.9.0-rc1** by **Chris Ware and cloudyyrust**. Maintained independently by **scubamount**, without endorsement from the original authors. Dated modifications: [CHANGELOG.md](CHANGELOG.md).

sc-offline's own code is **GPL-3.0-only** ([LICENSE](LICENSE)); third-party code retains its own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Contributions: [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).

AI was used in a limited way to make this project.

This project is not affiliated with, endorsed by, or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen is a trademark of Cloud Imperium Games.
