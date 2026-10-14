<div align="center">

<a href="https://discord.gg/NJKeVfYCCC"><img src="https://img.shields.io/badge/JOIN%20THE%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the sc-offline Discord" height="64"></a>

## [👉 Join the Discord 👈](https://discord.gg/NJKeVfYCCC)

# sc-offline

**An offline, single-player mod menu for Star Citizen.**<br>
Spawn ships and buildings, travel the star systems and wear Squadron 42 outfits from the in-game menu; spawn and manage NPCs through the external plugin commands.

<sub>Based on **ChrisWareOffline 0.9.0-rc1** by Chris Ware and cloudyyrust (GPL-3.0). The original project was shut down and its repository removed; sc-offline is maintained independently and is not endorsed by its authors. What this repository has changed since then, with dates: [CHANGELOG.md](CHANGELOG.md). Licensed under GPL-3.0-only ([LICENSE](LICENSE)); third-party code keeps its own licence ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). This project is not affiliated with, endorsed by, or sponsored by Cloud Imperium Games or Roberts Space Industries.</sub>

[![Latest release](https://img.shields.io/github/v/release/scubamount/sc-offline?style=flat-square&label=release&color=2ec4d6)](https://github.com/scubamount/sc-offline/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/scubamount/sc-offline/build.yml?branch=main&style=flat-square&label=build)](https://github.com/scubamount/sc-offline/actions/workflows/build.yml)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20(Wine%2C%20experimental)-555?style=flat-square)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-f0851f?style=flat-square)](LICENSE)

[**Download**](https://github.com/scubamount/sc-offline/releases/latest) ·
[**Discord**](https://discord.gg/NJKeVfYCCC) ·
[Setup](#setup) ·
[Play](#play) ·
[Troubleshooting](#troubleshooting) ·
[Go back online](#go-back-online) ·
[Docs](#more-docs)

</div>

> [!WARNING]
> Using this mod may breach the Star Citizen EULA and Terms of Service, and it may get your
> account banned. Use it only offline, never on Cloud Imperium Games' servers. You are
> responsible for your own account.
>
> **Before you play online again**, run `sc-offline.exe status`. If it reports anything left
> over (for example after a crash or a power cut), run `sc-offline.exe uninstall`, which takes
> the mod out of the game folder and undoes the PC changes the launcher recorded. See
> [Go back online](#go-back-online).

> [!NOTE]
> **Release vs. gameplay testing status.** These are three different states, and none of them
> stands in for the others:
> - **Shipped `v0.7.7`** (built on sco-core `2197dfb`, game pack 0.2.0): its
>   [release notes](https://github.com/scubamount/sc-offline/releases/tag/v0.7.7) report that the
>   maintainer ran it in game with NVIDIA Smooth Motion off. That was one run, not a check of
>   each feature.
> - **Current `main`** pins the published sco-core SDK **2.0.0** (tag `sdk-v2.0.0`, `cd9a686`;
>   plugin ABI 1.1, game pack 0.3.0) and ships NPC and the inspector as external plugins. It
>   has **not been run in game** ([what is not verified](docs/status.md)).
> - **SDK 2.1.0:** draft PR [#126](https://github.com/scubamount/sc-offline/pull/126) is
>   preparing the move to the published `sdk-v2.1.0` release (`8ef2424`, game pack 0.4.0). It
>   is not merged; until it is, `main` stays on SDK 2.0.0.
>
> In-game verification of each capability, and of SDK 2.1.0, is still outstanding. Other PCs and game patches can behave differently; known problems are tracked
> in [Issues](https://github.com/scubamount/sc-offline/issues). If something breaks, see
> [Troubleshooting](#troubleshooting) and [report it](https://github.com/scubamount/sc-offline/issues).
> Linux through Wine was played once with 0.7.0, before the SDK conversion, on a LUG Helper install
> ([docs/linux.md](docs/linux.md)); the converted builds haven't been run on Linux yet.

## Quick start

1. **Download** `sc-offline-<version>.zip` from the [latest release](https://github.com/scubamount/sc-offline/releases/latest) and extract it anywhere.
2. **Close** the RSI Launcher and the game.
3. **Open** `sc-offline.exe`, click **Play**, and say yes to the administrator prompt.
4. **Press `M`** once the game has loaded you in.

The launcher puts the mod into your game folder while you play, takes it out again when the game closes, and undoes the PC changes it recorded ([what it changes](#2-what-the-launcher-changes)). After a crash or a power cut, run `sc-offline.exe status`, and `sc-offline.exe uninstall` if it lists anything, before you go online.

## What you need

| Requirement | Details |
| --- | --- |
| **Game** | A copy of Star Citizen that you own |
| **OS** | Windows. Linux through Wine is experimental (played once with 0.7.0, before the SDK conversion); see [docs/linux.md](docs/linux.md) |
| **Rights** | Administrator: Windows asks once each time you play |
| **Disk** | About 1.4 MB for the zip |

## Setup

### 1. Download the mod

1. Download `sc-offline-<version>.zip` from the newest release on [Releases](https://github.com/scubamount/sc-offline/releases).
2. Right-click the zip, choose **Extract All**, and put the folder anywhere, for example your Desktop. Keep all the files together.

Don't copy anything into your game folder yourself.

### 2. What the launcher changes

The mod can't run while Easy Anti-Cheat (EAC) is on, so each time you play, the launcher's helper:

| Step | Change | Why |
| --- | --- | --- |
| 1 | Adds Windows Firewall rules that block `StarCitizen.exe`, the RSI Launcher and the game's crash reporter (with `multiplayer = on`, the game keeps your local network for [co-presence](#scope)) | The game can't reach the internet while modded |
| 2 | Adds `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to your hosts file | The RSI Launcher doesn't download EAC again |
| 3 | Renames `EasyAntiCheat_EOS.exe` to `EasyAntiCheat_EOS.exe.bak` | EAC doesn't start |

Each change is recorded as it is made. When the game closes, the launcher undoes the recorded changes and prints what it undid. Anything you set up yourself is left alone. If the game crashes or the PC shuts down, the changes stay in place: the next run of `sc-offline.exe` lists them and offers to undo them, `sc-offline.exe status` shows them, and `sc-offline.exe uninstall` undoes them. Each step can be turned off in `sc-offline.ini`; see [docs/launcher.md](docs/launcher.md#pc-changes).

> [!IMPORTANT]
> The firewall rules keep the game off the internet. They don't remove other traces on your PC by itself. When the game closes, the launcher lists the logs the game wrote during this session (`Game.log`, new files in `logbackups` and `Crashes`) and asks twice before deleting them; keep `Game.log` if you want to report a bug. Older logs are never touched. Use the mod at your own risk.

## Play

Double-clicking `sc-offline.exe` opens its window:

<p align="center">
  <img src="images/launcher.png" alt="The sc-offline window: a green light saying it's safe to go online, buttons for Play, Status, Update, Install, Uninstall, Open settings and Open logs, and the output of Status (the previous window; this version's is drawn with Direct3D 11 and has a Plugins page)" width="85%">
  <br><sub>The launcher after clicking <b>Status</b> (captured under Wine, hence the <code>Wine:</code> line).</sub>
</p>

| Button | Does the same as |
| --- | --- |
| **Play** | `sc-offline.exe play`: add the mod, start the game, take the mod out when it closes |
| **Status** | `sc-offline.exe status`: check the setup, change nothing |
| **Update** | `sc-offline.exe update`: check GitHub for a newer sc-offline |
| **Install** / **Uninstall** | `sc-offline.exe install` / `uninstall` |
| **Settings** / **Logs** (top bar) | open `sc-offline.ini` in Notepad / the `data` folder |
| **Plugins** (sidebar) | switch the plugins in `data\plugins` on or off and change their settings ([docs/launcher.md](docs/launcher.md#the-plugins-page)) |

While you play, your Discord profile shows **Playing sc-offline** with the version you're running, where you are and your ship, plus buttons to this Discord and the download page (the `discord` plugin; switch **Discord** off in the top bar to hide it). The light at the top shows whether it's safe to go online. Questions (`Update now?`, `Delete these files?`) get **Yes** and **No** buttons. Every command still works from a terminal.

1. Close the RSI Launcher and the game.
2. Double-click `sc-offline.exe` and click **Play**. The window shows the game folder it found. Windows asks for administrator rights; say yes. That prompt is for the small helper that makes the changes above and copies the mod in and out; the game itself never runs as administrator.
3. Once the game has loaded you in, press <kbd>M</kbd> to open the menu.

| Key | Action |
| --- | --- |
| <kbd>M</kbd> | Open or close the menu |
| <kbd>F6</kbd> | Turn build mode on or off |
| <kbd>F7</kbd> | Save your position |
| <kbd>F8</kbd> | Teleport back to the saved position |

The launcher finds your install by itself: it asks the RSI Launcher where the game is, checks the usual folders, then searches your drives, and if all that fails it lets you pick the folder. It remembers the answer for next time. To force a folder, set `game =` in `sc-offline.ini`. The same file also chooses the boot map, the channel (`LIVE`, `PTU`, and so on) and an optional start in a ship over Daymar (`start = Daymar` with `start_ship`); see [docs/launcher.md](docs/launcher.md). The mobiGlas vehicle manager and character creation don't work offline; the ship terminals do, with `asop = on` (the default). See [Ship terminals, hangars and ATC](docs/features.md#ship-terminals-hangars-and-atc) and [Not available offline](docs/features.md#not-available-offline).

What's in each menu tab: [docs/features.md](docs/features.md).

## Update

The launcher checks for a newer release each time you play (it gives up after 3 seconds if you're offline) and asks before changing anything. Say `y` and it downloads the release zip (Windows asks for administrator rights only if the folder is under Program Files), checks it against the SHA-256 that GitHub publishes, replaces the program files, keeps your saves (`data\storage\`, `wallet.txt`, saved places, bookmarks) and your `sc-offline.ini`, and restarts itself. If anything goes wrong, it puts the old files back.

To check on demand, run `sc-offline.exe update`. To turn the check off, set `check_updates = off` in `sc-offline.ini`.

<details>
<summary><b>Updating by hand</b></summary>

1. Close the game.
2. Copy the `storage` folder, `wallet.txt`, `spawn.txt`, `bookmarks.txt` and `locations_found.txt` out of the old `data` folder, and `sc-offline.ini` if you changed it.
3. Delete the old folder, then extract the new zip.
4. Put those files back in the new folder.
</details>

## Troubleshooting

<details>
<summary><b>The menu doesn't open</b></summary>

Check that you started the game with `sc-offline.exe`, not the RSI Launcher.
</details>

<details>
<summary><b>The launcher can't find the game, or picks the wrong one</b></summary>

Set `game =` in `sc-offline.ini` to your `StarCitizen` folder (for example `game = E:\Game\Star Citizen\StarCitizen`), or delete `data\game-path.txt` to make it search again.
</details>

<details>
<summary><b>Is everything set up?</b></summary>

Run `sc-offline.exe status` from a terminal. It checks the DLL, the game version, Easy Anti-Cheat and the hosts file, and changes nothing.
</details>

<details>
<summary><b>The launcher stops with "Easy Anti-Cheat is active"</b></summary>

The launcher won't start a modded game while Easy Anti-Cheat is active. Leave `eac_rename = on` (the default) in `sc-offline.ini` so the launcher handles it for the session and restores it when the game closes, then run the launcher again.
</details>

<details>
<summary><b>The launcher couldn't add the firewall rule or edit hosts</b></summary>

Say yes to the administrator prompt; some antivirus tools lock the hosts file.
</details>

<details>
<summary><b>The game crashes while loading on an NVIDIA GPU</b></summary>

Turn off **Smooth Motion** for Star Citizen in the NVIDIA App (Graphics → Program Settings → Star Citizen → Smooth Motion → Off), or in the global settings if you set it there. With Smooth Motion on, the game can crash during startup; `mod.log` then shows `[crash]` lines naming `NvPresent64.dll` or `nvcuda64.dll`. If it still crashes with Smooth Motion off, [report it](https://github.com/scubamount/sc-offline/issues) with matching `Game.log`, `mod.log` and `launcher.log`.
</details>

<details>
<summary><b>The game closes by itself while loading</b></summary>

Start `sc-offline.exe` with normal rights. An elevated launcher attempts to use the desktop token for the game; if unavailable, it falls back and warns. Remove *Run this program as an administrator* from `StarCitizen.exe` Properties to launch with normal rights. On NVIDIA GPUs, check Smooth Motion first (see above). Crash diagnostics identify the fault module/offset and raw-stack executable-address candidates, not an unwound call chain.
</details>

<details>
<summary><b>The game crashed or the PC shut down mid-game</b></summary>

Run `sc-offline.exe status`, then `sc-offline.exe uninstall` before you play online. It removes the mod and undoes the PC changes the launcher recorded.
</details>

<details>
<summary><b>A game update broke the mod</b></summary>

That's expected until the mod is updated for the new game version.
</details>

**Reporting a bug:** open an [Issue](https://github.com/scubamount/sc-offline/issues) and attach `data/launcher.log`, `data/mod.log`, and the game's `Game.log`.

## Go back online

1. Close the game.
2. In the sc-offline window, check the light at the top. **Green** means the mod is out of the game folder and every PC change the launcher recorded is undone. **Red** lists what is left: click **Uninstall**, which is enabled only then. (From a terminal: `sc-offline.exe status`, then `sc-offline.exe uninstall` if needed.)
3. Only changes the launcher made itself are recorded. Anything changed outside it (for example while `eac_hosts` or `eac_rename` was off) isn't, so `status` can't report it and `uninstall` can't undo it.

## How it's built

sc-offline is the host and the reference consumer of [sco-core](https://github.com/scubamount/sco-core), pinned to published tag `sdk-v2.0.0` (`cd9a686bda4184f1befbdf0b74b503a1565540dd`): SDK **2.0.0**, plugin ABI **1.1**, Star Citizen game pack **0.3.0**. The host builds from the tagged submodule; standalone plugins build against verified published SDK archives. It loads as `dinput8.dll`, applies the offline patches, hooks the game's main thread and starts sco-core's host kit: the signature rows, the plugin loader and API, and the game pack, which publishes the `game.*` services. Nine features remain built-in plugins on the same API that plugins made with the [sco SDK](https://github.com/scubamount/sco-core/blob/main/sdk/README.md) use: teleport (F7/F8), the ship spawner, crew & seats, loadout (gear and outfits), quantum travel, build mode (F6), contracts, natural mining and multiplayer. The menu is a shell over sco-core's `sco.ui`: each built-in registers its own tab and keys.

What runs on what: teleport on `teleport.spatial`; ship spawns on `spawn.entities` 1.2; crew seats on `game.vehicles` and `game.actors`; the external NPC plugin on `game.actors`; build props on `game.entities` (`keep()` leaves placed props in the world) and build's ground ray and camera on `game.world`; mining on the `game.mining` row; the other features on sco-core's signature rows, with no raw scans in `src/`. Noclip, god mode and infinite ammo are not built in: they are `creative`, an optional plugin on `game.creative` that ships **off** (turn it on in the launcher's Plugins page, with `plugins = on` in `sc-offline.ini`). External plugins live in `data/plugins/<id>/`. Step 1C also moves **NPC** out of the host, following the external inspector. The four external feature folders are `npc`, `inspector`, `creative` and `discord`; NPC, inspector and Discord ship enabled, while creative ships off. `data/plugins/npc/{plugin.ini,npc.dll}` has no `disabled` marker and requires `plugins = on`. It registers `npc.spawn`, `npc.observe`, `npc.remove` and `npc.clear`, with owned-only removal and explicit pending/unknown observations. Disabling NPC or inspector, or setting `plugins = off`, leaves no built-in fallback for either. Both are command-first with no new ImGui dependency. The old NPC menu tab is removed pending renderer-neutral widgets ([sco-core #139](https://github.com/scubamount/sco-core/issues/139)); Crew keeps its shared archetype picker. Inspector Lua is a non-default example, not another feature. See the [build/check/install tutorial](docs/sdk-workflows.md). Normal external-consumer reload (`sco.reload`) is a running-host route subject to plugin/provider lifetime rules, not verified game behavior. Native faults are restart-required, not safe reload or recovery.

CI keeps this honest: `no-scans` (no raw scan under `src/`) and `sdk-headers` (`src/builtins/` and `plugins/` include only SDK headers), next to `check`, `build` and `bridges`. The built-ins still call sc-offline's own engines for features with no `game.*` service yet, which is what `tools/sdk-headers-allow.txt` lists. Details: [docs/architecture.md](docs/architecture.md) and [docs/build.md](docs/build.md). What has not been run in game since the conversion: [docs/status.md](docs/status.md).

## Source, dependencies and data

Each release is built by [`.github/workflows/build.yml`](.github/workflows/build.yml) from this repository at the release tag. GitHub publishes the SHA-256 of every release asset, and `sc-offline.exe update` checks the download against it. No prebuilt binary is kept in git. The binaries are compiled from more than `src/`:

| Source | What it is | Licence |
| --- | --- | --- |
| `src/` (except `src/third_party/`), `launcher/`, `plugins/` | sc-offline's own code: the mod (`dinput8.dll`), the launcher (`sc-offline.exe`) and the plugin DLLs. The plugins compile against sco-core's SDK headers; NPC and inspector against the published SDK archive | GPL-3.0-only ([LICENSE](LICENSE)) |
| `src/third_party/imgui/` | Dear ImGui 1.92.8 WIP, vendored; in `dinput8.dll` and `sc-offline.exe` | MIT |
| `external/sco-core` (git submodule) | [sco-core](https://github.com/scubamount/sco-core) at the commit pinned for that release (on `main` today: `sdk-v2.0.0`, `cd9a686`): the host kit, game pack and plugin runtime, plus the Lua 5.4.8 (modified), SQLite 3.53.4 and toml++ 3.4.0 it vendors | sco-core: GPL-3.0-only; Lua and toml++: MIT; SQLite: public domain |

Licence texts and where each component ends up: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), which also ships in the release zip.

`data/` ships in the release as tracked in git. It is not only identifiers:

- **Game identifiers**, one per line: `ships.txt` (vehicle entity class names taken from the game's data), `npcs.txt` (NPC archetypes) and `items.txt` (gear item names, in slot sections).
- **Contract list:** `contract_scripts.txt`, the IDs and internal names of the 796 contracts that need no CIG mission script, generated from the game's `Game2.dcb`.
- **Named lists built on game identifiers:** `locations.txt` (system, the name the menu shows, entity name, radius), `outfits.txt` (named outfits, each a list of item names) and `buildables.txt` (the game's object-container paths, in named groups).
- **Offline profile:** `OfflineDB/default_1.xml`, the starting money and ships, which the launcher copies into the game's user folder.
- **Game-data patch:** `builtin/quantum/datacore/quantum_drive.toml`, a DataCore data pack that sco-core applies as the game loads its data.
- **Plugin settings:** `plugins/*/plugin.ini`, and the `disabled` marker that keeps creative off.

Formats: [docs/data-files.md](docs/data-files.md).

## Scope

Offline play is the default and stays that way. sc-offline never connects to Cloud Imperium Games' servers, and while modded, the launcher's firewall rules keep the game off the internet. (`sc-offline.exe` itself stays online to check GitHub for updates.)

What runs is a local recreation: ships, NPCs, contracts and build mode run on this project's own data and services, not on CIG's server-side systems. The local contracts are the 796 that need no CIG mission script. This repository ships none of CIG's Subsumption mission scripts, so contracts that need them aren't offered, and nothing done offline restores or affects online progression. None of this makes online play with the mod safe: the launcher's EAC handling keeps you offline while modded, and nothing here guarantees your account won't be flagged.

Two additions go beyond one offline game, and neither uses or changes the game's own netcode or servers:

- **Private co-presence** (the Multiplayer tab, on sco-core's `sco.net`): friends who also run sc-offline play alongside each other, peer to peer between sc-offline clients only, over a LAN or a VPN you set up yourself (Tailscale, ZeroTier). Each player still runs their own offline game; peers exchange their own authenticated messages (poses, spawn announcements), and other players show up as "ghost" entities your game spawns. Nothing connects until you press Host or Join ([limits](docs/features.md#multiplayer-lan-or-vpn)).
- **Bridges to other games** on the same PC, over local shared memory, for example Titanfall 2 (through Northstar) or a Minecraft-style voxel game. Optional builds only, **not in the release** ([docs/bridges.md](docs/bridges.md)).

sc-offline never runs with Easy Anti-Cheat active: while you play, the launcher renames `EasyAntiCheat_EOS.exe` and blocks the EAC download host ([what the launcher changes](#2-what-the-launcher-changes)). Online play, cheating, getting around anti-cheat and the rest of the bans in [CONTRIBUTING](CONTRIBUTING.md#what-fits-this-project) stay out for good.

## More docs

| Doc | For |
| --- | --- |
| [Features](docs/features.md) | Every menu tab, including Squadron 42 |
| [Launcher](docs/launcher.md) | `sc-offline.exe`, `sc-offline.ini`, and what the launcher changes on disk |
| [Data files](docs/data-files.md) | The text files in `data/` and the environment variables |
| [Linux](docs/linux.md) | Running under Wine (experimental) |
| [Architecture](docs/architecture.md) | sc-offline as host and reference consumer of sco-core: which `game.*` service each feature runs on, built-ins versus plugins, the CI gates |
| [Not verified in game](docs/status.md) | What the SDK conversion still has to show in a game session |
| [SDK workflows](docs/sdk-workflows.md) | Build, check and install standalone NPC and inspector from the published SDK |
| [Migration decisions](docs/decisions.md) | Owner-approved 13-feature target, separate optional bridges and policy boundaries |
| [Plugin UI](docs/plugin-ui.md) | Menu tabs, overlays and hotkeys for plugins |
| [Bridges](docs/bridges.md) | The optional Titanfall 2 and voxel bridges (not in the release) |
| [Build](docs/build.md) | Building from source, checks, CI and releases |
| [Changelog](CHANGELOG.md) | What changed in each release |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Third-party code in the release and its licences |

## Star history

<a href="https://www.star-history.com/#scubamount/sc-offline&Date">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=scubamount/sc-offline&type=Date&theme=dark" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=scubamount/sc-offline&type=Date" />
    <img alt="sc-offline GitHub stars over time" src="https://api.star-history.com/svg?repos=scubamount/sc-offline&type=Date" width="600" />
  </picture>
</a>

## Credits

sc-offline is **based on ChrisWareOffline 0.9.0-rc1** by Chris Ware and cloudyyrust (GPL-3.0). The original project has been shut down and its repository removed; this repository is maintained independently and is not endorsed by its authors.

This repository adds the launcher, CI and releases, the Squadron 42 tab and later fixes. It is licensed under GPL-3.0-only; see [LICENSE](LICENSE). Report bugs on [Issues](https://github.com/scubamount/sc-offline/issues). Want to help? See [CONTRIBUTING.md](CONTRIBUTING.md); security problems go to [SECURITY.md](SECURITY.md).

<sub>AI was used in a limited way to make this project. It's a fan project, not made by or affiliated with Cloud Imperium Games or Roberts Space Industries. Star Citizen is a trademark of Cloud Imperium Games.</sub>
