# Linux (experimental)

> [!WARNING]
> **Experimental.** One report of sc-offline 0.7.0 on Linux exists, from October 2026. Nothing newer has run under Wine, so that report does not establish current Wine support.

`sc-offline.sh` starts `sc-offline.exe` inside your Star Citizen Wine prefix. From 0.8.0 that exe is the Rust launcher, which has not run under Wine. The report below used the C++ launcher, which now ships as `sc-offline-classic.exe`. If the Rust launcher fails under Wine, run `wine sc-offline-classic.exe <command>` yourself, because `sc-offline.sh` always starts `sc-offline.exe`. Leave `menu` at its default `imgui`: the new menu has not run anywhere ([Which menu](../README.md#which-menu)). The launcher's behavior under Wine differs from Windows ([launcher.md](launcher.md)). Not run in game. That covers 0.8.0 under Wine.

## Historical observation

[PR #105](https://github.com/scuba-sc/sc-offline/pull/105) reports about 90 minutes of offline play on this setup:

| Component | Reported value |
| --- | --- |
| sc-offline | 0.7.0 |
| Star Citizen | 4.10.193 |
| Distribution and desktop | Bazzite, Fedora 44, KDE, Wayland |
| Wine setup | LUG Helper; `lug-wine-tkg-git-11.17-1` |
| GPU | NVIDIA RTX 3080 Ti with an AMD integrated GPU |

It describes spawning ships, crew, build mode, outfits and gear, a hauling contract, wallet changes and teleporting to Levski, then a clean quit and launcher status `Installed: no`. It also says launching Wine directly worked and DXVK selected the discrete GPU. These are attributed observations of an older product and game build. They are not a repeated test, hybrid-GPU acceptance or proof of every cleanup step, and the original logs were not rechecked. No test is recorded for the current host, later game builds, other distributions or GPUs, or Lutris and Steam/Proton detection.

## Setup

1. Install the game in a Wine prefix. The script expects the layout the [LUG Helper](https://github.com/starcitizen-lug/lug-helper) creates.
2. Turn off Easy Anti-Cheat the Linux way:
   - add `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to `/etc/hosts`;
   - if `EasyAntiCheat_EOS.exe` exists under `drive_c/Program Files (x86)/EasyAntiCheat_EOS/` in your prefix, rename it.
3. Extract the release zip anywhere and run:

   ```bash
   ./sc-offline.sh
   ```

   It takes the same commands and options as `sc-offline.exe` (`status`, `uninstall`, `--dry-run` and so on; see [launcher.md](launcher.md)), plus `--prefix <dir>` and `--wine <path>` to override what it finds. `./sc-offline.sh --dry-run` prints the prefix, Wine and the exact command, and starts nothing.

The script checks that `/etc/hosts` blocks the EAC download server and prints the line to add if it does not. It sets `WINEDLLOVERRIDES=dinput8=n,b` so Wine loads the mod's `dinput8.dll` instead of its own. It starts Wine directly and never runs the LUG Helper's `sc-launch.sh`; it only reads that file to pick a Wine runner.

## How it finds things

| What | Order it tries |
| --- | --- |
| Prefix | `--prefix` or `$WINEPREFIX`; the LUG Helper's `~/.config/starcitizen-lug/winedir.conf`; a Lutris game whose name mentions Star Citizen (its `prefix:`); a Steam/Proton `compatdata/*/pfx` that contains `Roberts Space Industries` (native and Flatpak Steam); `~/Games/star-citizen` |
| Wine | `--wine` or `$WINE`; `wine_path=` in the prefix's `sc-launch.sh`; the Lutris game's Wine runner; for a Proton prefix, the newest Proton's `wine`; `wine` on `PATH` |

It prints which source it used for each. The report used the LUG Helper layout. The Lutris and Steam/Proton paths follow those tools' file layouts and have no recorded run. When you report a difference between launch paths, include the `--dry-run` output.

**Duplicate install paths.** The Windows launcher compares the physical channel and `Bin64` directories to combine duplicate paths for one install, keeping the first spelling and priority. If the filesystem gives no directory identity, it falls back to a case-insensitive path comparison. Separate channels or roots stay separate, including when they share executables or a `Bin64` junction. Whether a Wine runner gives matching identity for `C:` and `Z:` aliases needs a named Wine fixture. The CI job `install-aliases-wine` (`tools/check_install_discovery_wine.py`) runs Ubuntu's Wine in a fresh prefix. It checks the identity helper against a `C:` symlink and its `Z:` path, and that separate roots, PTU and a shared `Bin64` stay separate. It creates no game install and tests only that Wine runner.

## What the launcher skips under Wine

The launcher skips its firewall, hosts-file and EAC-rename steps under Wine (`NeedsPcChanges` in `launcher/launcher.cpp`). The checks in `sc-offline.sh` and any Linux changes you made by hand are separate. `Installed: no` does not show that a manual change was undone.

## Cleanup before returning online

Close the game and follow [the launcher's cleanup checks](../README.md#go-back-online). Restore any file you renamed and remove only the hosts entries you added, using your saved record of the original state. The launcher cannot confirm those manual changes. Check cleanup and the normal anti-cheat state before you use the official online game.

## Reporting

Include the exact sc-offline, SDK, game, distribution, Wine, GPU and driver versions with the script's output, `data/mod.log` and the game's `Game.log`. Say what ran and what cleanup you saw. Remove account names, user paths and tokens before posting logs ([CONTRIBUTING.md](../CONTRIBUTING.md)).
