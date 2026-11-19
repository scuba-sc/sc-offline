# sc-offline

**An offline mode for Star Citizen: a launcher, a mod and an in-game menu.**

[![Latest release](https://img.shields.io/github/v/release/scuba-sc/sc-offline?style=flat-square&label=release&color=2ec4d6)](https://github.com/scuba-sc/sc-offline/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/scuba-sc/sc-offline/build.yml?branch=main&style=flat-square&label=build)](https://github.com/scuba-sc/sc-offline/actions/workflows/build.yml)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-f0851f?style=flat-square)](LICENSE)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/NJKeVfYCCC)

[**Download**](https://github.com/scuba-sc/sc-offline/releases/latest) · [**Discord**](https://discord.gg/NJKeVfYCCC) · [**Features and status**](docs/features.md) · [**Changelog**](CHANGELOG.md) · [**Issues**](https://github.com/scuba-sc/sc-offline/issues)

sc-offline starts Star Citizen offline with a mod loaded and takes the mod out again when the game closes. In game, press **M** for a menu with ships, NPCs, gear, saved positions and other single-player tools. It is built on [sco-core](https://github.com/scuba-sc/sco-core), and its plugins use the same public SDK that other plugin authors use.

> [!WARNING]
> Using this mod may breach the Star Citizen EULA and Terms of Service, and it may get your account banned. Use it only offline, never on Cloud Imperium Games' servers. You are responsible for your own account. Before going online again, close the game and [check cleanup](#go-back-online).

## What you need

- Your own installed copy of Star Citizen. Version 0.8.0 is built for game build 4.10.200.58746.
- 64-bit Windows. Linux through Wine is [experimental](docs/linux.md).

## Install and start

1. Download **`sc-offline-<version>.zip`** from [Releases](https://github.com/scuba-sc/sc-offline/releases/latest), not GitHub's source-code zip.
2. Extract it into its own folder and keep the files together. Do not copy the DLLs into the game folder yourself.
3. Close the game, open **`sc-offline.exe`** and click **Play**. Windows asks for administrator rights once, for the [PC changes](docs/launcher.md#pc-changes) that cut the game's network while you play.
4. Wait for startup. The launcher holds the game while the mod starts, so about 15 seconds pass before the game window appears (up to 3 minutes at most). In game, press **M**.

If `sc-offline.exe` does not start or fails on your PC, run **`sc-offline-classic.exe`** from the same folder instead. It is the older launcher and takes the same commands ([details](docs/launcher.md#the-classic-launcher)).

Coming from 0.7.x? Extract 0.8.0 into the old folder or run `sc-offline.exe update`. Your `sc-offline.ini` and data files are kept ([how](docs/launcher.md#updating-from-07x)).

## Go back online

The launcher undoes its changes when the game closes. After a crash or power loss, close the game, then press **Repair** in the launcher window, or run:

```powershell
.\sc-offline.exe status
.\sc-offline.exe uninstall
.\sc-offline.exe status
```

If the Rust launcher cannot do it, `.\sc-offline-classic.exe uninstall` reads the same `%ProgramData%\sc-offline\pc-changes.txt` record and undoes the same changes.

**Green** means the mod is out of the game folder and the recorded PC changes are undone. It does not speak for your account or for changes you made by hand. [PC changes](docs/launcher.md#pc-changes) lists each one.

## Which menu

The game menu you get by default is the **classic** menu. A **new** menu (written in Rust) is opt-in: choose **New** under **Settings > In-game menu** in the launcher window, or set `menu = rust` in `sc-offline.ini`. It works only with `sc-offline.exe`. The release folder holds two game DLLs for this reason ([why](docs/decisions.md#2026-11-18-two-game-dlls)):

| File | What it is |
| --- | --- |
| `dinput8.dll` | The mod with the classic menu. Installed by default, and by the classic launcher always. |
| `dinput8-rust-menu.dll` | The same mod with the new menu hook. `sc-offline.exe` installs it as `dinput8.dll` only when the ini says `menu = rust`. |

The new menu has not been seen in game. Not run in game. The change takes effect on the next **Play**.

## What works

[Features](docs/features.md) lists what each part is for. [Status](docs/status.md) is the one page that says what was seen in game and what was not. Read it before you rely on a feature.

## Troubleshooting

Start through `sc-offline.exe`. If you use an NVIDIA GPU, turn **NVIDIA Smooth Motion off** for Star Citizen. For a bug report, include the mod version, the game build, and `data/launcher.log`, `data/mod.log` and `Game.log`. Read the logs for personal information before you post them. [Open an issue](https://github.com/scuba-sc/sc-offline/issues/new/choose) or see the [launcher guide](docs/launcher.md).

## Documentation

| Page | Covers |
| --- | --- |
| [Launcher](docs/launcher.md) · [Linux](docs/linux.md) | Commands, settings, updates, recovery, Wine |
| [Features](docs/features.md) · [Status](docs/status.md) | What each feature does, and what has been run |
| [Data files](docs/data-files.md) · [Capabilities](docs/capabilities.md) | Lists, saves, environment variables, capability switches |
| [Plugin UI](docs/plugin-ui.md) · [Pickers](docs/pickers.md) · [Localization](docs/localization.md) | Menu pages, hotkeys, catalogs, launcher language |
| [AI agents](docs/ai-agents.md) | Connect Claude, Codex, Cursor or VS Code to your install (`sc-offline.exe mcp`) |
| [Architecture](docs/architecture.md) · [Decisions](docs/decisions.md) | How the layers fit, and why |

## For plugin developers

sc-offline is the reference consumer of the sco-core SDK. Reusable infrastructure lives in sco-core and product behavior here. When a plugin needs something the SDK lacks, add the contract and provider in sco-core and consume it here.

- Start with the [SDK guide](https://github.com/scuba-sc/sco-core/blob/main/sdk/README.md), the [SDK reference](https://scuba-sc.github.io/sco-core/) and the [SDK to plugin tutorial](https://github.com/scuba-sc/sco-core/blob/main/docs/sdk-mcp-tutorial.md).
- [SDK workflows](docs/sdk-workflows.md) has the commands. [Build](docs/build.md) builds the Windows host from the source revision pinned in `external/sco-core`.
- A plugin is a folder, `data/plugins/<id>/`, with a `plugin.ini`. The host, the SDK archive, the plugin ABI and each plugin have separate versions.
- The Creative plugin lives in its own repository, [`sc-creative`](https://github.com/scuba-sc/sc-creative), carried as the `external/sc-creative` submodule.

Offline play is the scope. There is no connection to CIG servers, no anti-cheat bypass, no telemetry and no account or entitlement changes ([CONTRIBUTING](CONTRIBUTING.md), [SECURITY](SECURITY.md)). Co-presence lives in [sc-mp](https://github.com/scuba-sc/sc-mp), and the optional [bridges](docs/bridges.md) are separate. Neither is in the release.

## Credits and license

Based on **ChrisWareOffline 0.9.0-rc1** by **Chris Ware and cloudyyrust**. Maintained independently by **scubamount**, without endorsement from the original authors. Dated modifications: [CHANGELOG.md](CHANGELOG.md).

sc-offline's own code is **GPL-3.0-only** ([LICENSE](LICENSE)). Third-party code keeps its own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Contributions: [CONTRIBUTING.md](CONTRIBUTING.md).

AI was used in a limited way to make this project.

This project is not affiliated with, endorsed by, or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen is a trademark of Cloud Imperium Games.
