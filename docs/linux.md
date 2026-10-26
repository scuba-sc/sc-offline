# Linux (experimental)

> [!WARNING]
> **Experimental.** A historical Linux run with sc-offline 0.7.0 was reported in October 2026. The current SDK 2.4.0 RC1 host and plugins have not been run under Wine in this review. The older report does not establish current Wine support.

`sc-offline.sh` starts `sc-offline.exe` inside your Star Citizen Wine prefix. The Windows launcher handles mod installation and removal in that prefix; its Wine path differs from Windows PC setup ([launcher.md](launcher.md)).

## Historical observation

[PR #105](https://github.com/scuba-sc/sc-offline/pull/105) reports about 90 minutes of offline play with this specific setup:

| Component | Reported value |
| --- | --- |
| sc-offline | 0.7.0 |
| Star Citizen | 4.10.193 |
| Distribution and desktop | Bazzite, Fedora 44, KDE, Wayland |
| Wine setup | LUG Helper; `lug-wine-tkg-git-11.17-1` |
| GPU | NVIDIA RTX 3080 Ti with an AMD integrated GPU |

The report describes spawning ships, crew, build mode, outfits/gear, a hauling contract, wallet changes and teleporting to Levski. It reports a clean quit and launcher status `Installed: no` afterward. It also reports that launching Wine directly worked and DXVK selected the discrete GPU on that setup. These are attributed observations from that older product/game tuple, not a repeated test, current hybrid-GPU acceptance or proof of every cleanup step. Its original logs were not revalidated for this documentation change.

There is no current test tuple recorded here for the SDK 2.4.0 RC1 host, later game builds, other distributions/GPUs, or Lutris/Steam/Proton detection. Keep those separate from the historical LUG Helper result.

## Setup

1. Install the game in a Wine prefix. The script expects the layout that the [LUG Helper](https://github.com/starcitizen-lug/lug-helper) creates.
2. Turn off Easy Anti-Cheat, the Linux way:
   - add `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to `/etc/hosts`;
   - if `EasyAntiCheat_EOS.exe` exists under `drive_c/Program Files (x86)/EasyAntiCheat_EOS/` in your prefix, rename it.
3. Extract the release zip anywhere, then run:

   ```bash
   ./sc-offline.sh
   ```

   It takes the same commands and options as `sc-offline.exe` (`status`, `uninstall`, `--dry-run`, ...;
   see [launcher.md](launcher.md)), plus `--prefix <dir>` and `--wine <path>` to override what it finds.
   `./sc-offline.sh --dry-run` prints the prefix, Wine and the exact command, and starts nothing.

## How it finds things

| What | Order it tries |
| --- | --- |
| Prefix | `--prefix` or `$WINEPREFIX`; the LUG Helper's `~/.config/starcitizen-lug/winedir.conf`; a Lutris game whose name mentions Star Citizen (its `prefix:`); a Steam/Proton `compatdata/*/pfx` that contains `Roberts Space Industries` (native and Flatpak Steam); `~/Games/star-citizen` |
| Wine | `--wine` or `$WINE`; `wine_path=` in the prefix's `sc-launch.sh`; the Lutris game's Wine runner; for a Proton prefix, the newest Proton's `wine`; `wine` on `PATH` |

It prints which source it used for each. The historical report used the LUG Helper layout. Lutris and Steam/Proton detection follow those tools' file layouts; no current acceptance run is recorded here for either path.

The Windows launcher compares the physical channel and Bin64 directories to combine duplicate install paths, preserving the first discovered spelling and priority. If the filesystem cannot supply directory identity, it retains the existing case-insensitive path comparison. Separate channels or install roots remain separate, including when they share executable files or a Bin64 junction. Whether a Wine runner supplies matching identity for C: and Z: aliases still requires a named Wine fixture; Windows filesystem tests do not establish that behavior.

It also checks that `/etc/hosts` blocks the EAC download server and prints the line to add if not.

The script sets `WINEDLLOVERRIDES=dinput8=n,b`, so Wine loads the mod's `dinput8.dll` instead of its own.

It launches Wine directly and does not execute the LUG Helper's `sc-launch.sh`. Reading that file to select a Wine runner is separate from running its setup. Use `--dry-run` to record the selected prefix, runner and command when reporting a difference between the two launch paths; this wrapper behavior does not by itself establish which graphics/runtime setup works on the current game build.

The launcher's `OnWine()` path skips its Windows firewall, hosts-file and EAC-rename operations (`NeedsPcChanges` in `launcher/launcher.cpp`). The wrapper's existing checks and manually made Linux changes are separate. `Installed: no` does not certify that a manual Linux change was undone; follow the existing cleanup instructions before returning online.

## Cleanup before returning online

Close the game and follow [the launcher's cleanup checks](../README.md#go-back-online). Restore any files you manually renamed and remove only the hosts entries you added, using your saved record of the original state. The launcher cannot certify those manual Linux changes were restored. Verify cleanup and the normal anti-cheat state before using the official online game.

## Reporting

Include the exact product, SDK/host, game, distribution, Wine, GPU and driver versions with the script's output, `data/mod.log`, and the game's `Game.log`. State what ran and what cleanup you observed. Remove account names, user paths and tokens before posting logs, as required by [CONTRIBUTING.md](../CONTRIBUTING.md).
