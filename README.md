# sc-offline

**An offline mode for Star Citizen: a launcher, a mod and an in-game menu.**

[![Latest release](https://img.shields.io/github/v/release/scuba-sc/sc-offline?style=flat-square&label=release&color=2ec4d6)](https://github.com/scuba-sc/sc-offline/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/scuba-sc/sc-offline/build.yml?branch=main&style=flat-square&label=build)](https://github.com/scuba-sc/sc-offline/actions/workflows/build.yml)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-f0851f?style=flat-square)](LICENSE)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/NJKeVfYCCC)

[**Download**](https://github.com/scuba-sc/sc-offline/releases/latest) · [**Discord**](https://discord.gg/NJKeVfYCCC) · [**Features and status**](docs/features.md) · [**Changelog**](CHANGELOG.md) · [**Issues**](https://github.com/scuba-sc/sc-offline/issues)

> [!IMPORTANT]
> **This is 0.8.0-rc.2, the second release candidate for testers. It is not the final 0.8.0.** It fixes the launcher window problems the first candidate's testers reported. Nothing in it has been run in game as a package ([Status](docs/status.md)). The update check on the default `stable` channel does not offer it, and the **Download** link above still gives the last full release. Get the candidate from its [pre-release page](https://github.com/scuba-sc/sc-offline/releases/tag/v0.8.0-rc.2), and report what you find as an [issue](https://github.com/scuba-sc/sc-offline/issues/new/choose). Attach the zip that `sc-offline.exe report` (or **Settings > Create report**) writes: it holds the status check and the logs with your account names replaced. To go back, run `sc-offline-classic.exe uninstall`, then use the 0.7.7 zip ([how](docs/launcher.md#release-candidates)). The final 0.8.0 follows after testers have reported.

sc-offline lets you play Star Citizen offline with a mod. The launcher starts the game with the mod loaded and takes the mod out again when the game closes. In game, press **M** for a menu with ships, characters, gear, saved positions and other single-player tools.

> [!WARNING]
> Using this mod may breach the Star Citizen EULA and Terms of Service, and it may get your account banned. Use it only offline, never on Cloud Imperium Games' servers. You are responsible for your own account. Before going online again, close the game and [check cleanup](#go-back-online).

## What you need

- Your own installed copy of Star Citizen. Version 0.8.0 is built for game build 4.10.200.58746.
- 64-bit Windows. Linux through Wine is [experimental](docs/linux.md).

## Install and start

1. Download **`sc-offline-<version>.zip`** from [Releases](https://github.com/scuba-sc/sc-offline/releases/latest), not GitHub's source-code zip.
2. Extract it into its own folder and keep the files together.
3. Close the game, open **`sc-offline.exe`** and click **Play**. Windows asks for administrator rights once, for the [PC changes](docs/launcher.md#pc-changes) that cut the game's network while you play.
4. Wait for the game window. The launcher holds the game while the mod starts, so this takes about 15 seconds, and 3 minutes at the most.
5. In game, press **M**.

If `sc-offline.exe` does not start or fails on your PC, run **`sc-offline-classic.exe`** from the same folder. It is the older launcher and takes the same commands ([details](docs/launcher.md#the-classic-launcher)).

Coming from 0.7.x? Extract 0.8.0 into the old folder or run `sc-offline.exe update`. Your `sc-offline.ini` and data files are kept ([how](docs/launcher.md#updating-from-07x)).

## Go back online

The launcher undoes its changes when the game closes. After a crash or power loss, close the game, then press **Repair** in the launcher window, or run:

```powershell
.\sc-offline.exe status
.\sc-offline.exe uninstall
.\sc-offline.exe status
```

If the new launcher cannot do it, `.\sc-offline-classic.exe uninstall` reads the same `%ProgramData%\sc-offline\pc-changes.txt` record and undoes the same changes.

A green status means the mod is out of the game folder and the recorded PC changes are undone. It says nothing about your account or about changes you made by hand. [PC changes](docs/launcher.md#pc-changes) lists each one.

## What works

[Status](docs/status.md) is the one page that says what was run in game and what was not. Read it before you rely on a feature. [Features](docs/features.md) says what each part is for.

## Which menu

The default in-game menu is the **classic** menu. In `sc-offline.ini` that is `menu = imgui`. A **new** menu is optional: choose **New** under **Settings > In-game menu** in the launcher window, or set `menu = rust`. It works only with `sc-offline.exe`, it has not been run in game ([Status](docs/status.md#ships-in-080-but-not-run-in-game)), and the change takes effect on the next **Play**. [Why there are two game DLLs](docs/decisions.md#2026-11-18-two-game-dlls).

## What is in the folder

| File | What it is |
| --- | --- |
| `sc-offline.exe` | The launcher. Start here. |
| `sc-offline-classic.exe` | The older launcher, for when the new one fails. |
| `sc-offline.ini` | Your settings. |
| `sc_offline_menu.dll` | The new in-game menu. Loaded only when `menu = rust`. |
| `dinput8.dll` | The mod with the classic menu. The launcher puts it in the game folder for you. |
| `dinput8-rust-menu.dll` | The mod with the hook for the new menu. The launcher uses it only when `menu = rust`. |
| `data` | Lists, saves, logs and the plugins in `data/plugins`. |
| `docs` | These pages. |
| `sc-offline.sh` | Starts the launcher under Wine on Linux. |
| `manifest.json` | The list of files the updater checks. |
| `README.md`, `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | Notes and licences. |

You can ignore both game DLLs. Do not copy them into the game folder by hand.

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

AI coding assistants helped write parts of this project. [AGENTS.md](AGENTS.md) holds the rules they follow.

This project is not affiliated with, endorsed by, or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen is a trademark of Cloud Imperium Games.
