# Linux (experimental)

> [!NOTE]
> **Played in game on Linux with 0.7.0** (October 2026), on one setup: Bazzite (Fedora 44, KDE Plasma, Wayland),
> RTX 3080 Ti with an AMD iGPU, the game installed with the [LUG Helper](https://github.com/starcitizen-lug/lug-helper),
> its `lug-wine-tkg-git-11.17-1` runner, and game 4.10.193. A 90-minute session worked: the launcher, the menu,
> ship and NPC spawning, crew, build mode, outfits and gear, contracts, the wallet and teleport. The mod was removed
> from `Bin64` when the game closed. Later versions, and Lutris or Steam/Proton installs, haven't been run on Linux yet.
> If you try one, please [report back](#reporting).

`sc-offline.sh` starts `sc-offline.exe` inside your Star Citizen Wine prefix. Once it's running, everything works the same as on Windows ([launcher.md](launcher.md)).

## Setup

1. Install the game in a Wine prefix. The script expects the layout that the [LUG Helper](https://github.com/starcitizen-lug/lug-helper) creates.
2. Turn off Easy Anti-Cheat, the Linux way. Under Wine the launcher doesn't do this for you:
   - add `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to `/etc/hosts`;
   - if `EasyAntiCheat_EOS.exe` exists under `drive_c/Program Files (x86)/EasyAntiCheat_EOS/` in your prefix, rename it.

   ```bash
   grep -q 'modules-cdn.eac-prod.on.epicgames.com' /etc/hosts ||
       echo '127.0.0.1 modules-cdn.eac-prod.on.epicgames.com' | sudo tee -a /etc/hosts
   P=$(head -n 1 "${XDG_CONFIG_HOME:-$HOME/.config}/starcitizen-lug/winedir.conf")   # your prefix; set it by hand without the LUG Helper
   EAC="$P/drive_c/Program Files (x86)/EasyAntiCheat_EOS/EasyAntiCheat_EOS.exe"
   if [ -f "$EAC" ]; then mv "$EAC" "$EAC.bak"; fi
   ```

   Both lines are safe to run again: the hosts line is added only once, and the rename is skipped when
   there is nothing to rename (already renamed, or no EAC in the prefix).

3. Extract the release zip anywhere, then run:

   ```bash
   ./sc-offline.sh
   ```

   It takes the same commands and options as `sc-offline.exe` (`status`, `uninstall`, `--dry-run`, ...;
   see [launcher.md](launcher.md)), plus `--prefix <dir>` and `--wine <path>` to override what it finds.
   `./sc-offline.sh --dry-run` prints the prefix, Wine and the exact command, and starts nothing.

   If `play` stops with `Easy Anti-Cheat is active`, step 2's rename didn't happen: do it and run again.
   Under Wine the launcher never renames EAC itself. `--skip-eac-check` skips that check; use it only if
   you know Easy Anti-Cheat can't start.

You don't need to change `block_network`: under Wine the launcher skips the Windows Firewall rules (and the
hosts and EAC steps) whatever `sc-offline.ini` says.

## Going back online

Close the game, check `./sc-offline.sh status` says `Installed: no`, then undo step 2:

```bash
P=$(head -n 1 "${XDG_CONFIG_HOME:-$HOME/.config}/starcitizen-lug/winedir.conf")   # your prefix, as in Setup
EAC="$P/drive_c/Program Files (x86)/EasyAntiCheat_EOS/EasyAntiCheat_EOS.exe"
if [ -f "$EAC.bak" ]; then mv "$EAC.bak" "$EAC"; fi
sudo sed -i '/modules-cdn.eac-prod.on.epicgames.com/d' /etc/hosts
```

The rename back is skipped when there's no `.bak` (EAC was never renamed); the RSI Launcher reinstalls
Easy Anti-Cheat if it's missing.

Then start the game from the RSI Launcher as usual.

## How it finds things

| What | Order it tries |
| --- | --- |
| Prefix | `--prefix` or `$WINEPREFIX`; the LUG Helper's `~/.config/starcitizen-lug/winedir.conf`; a Lutris game whose name mentions Star Citizen (its `prefix:`); a Steam/Proton `compatdata/*/pfx` that contains `Roberts Space Industries` (native and Flatpak Steam); `~/Games/star-citizen` |
| Wine | `--wine` or `$WINE`; `wine_path=` in the prefix's `sc-launch.sh`; the Lutris game's Wine runner; for a Proton prefix, the newest Proton's `wine`; `wine` on `PATH` |

It prints which source it used for each. The LUG Helper layout (`winedir.conf` and `wine_path=` in `sc-launch.sh`)
has been run on a real install; the Lutris and Steam guesses follow those tools' usual file layout and haven't been
run by anyone.

It also checks that `/etc/hosts` blocks the EAC download server and prints the line to add if not.

The script sets `WINEDLLOVERRIDES=dinput8=n,b`, so Wine loads the mod's `dinput8.dll` instead of its own.

It starts Wine directly, **not** through the LUG Helper's `sc-launch.sh`. Anything that script exports
(esync/fsync and so on) doesn't apply; DXVK lives in the prefix, so it still loads. On the tested setup the game
ran fine like this, and DXVK picked the discrete NVIDIA GPU over the AMD iGPU without any variables set. If the game
works from the LUG Helper but not from here, export those same variables before running `./sc-offline.sh`, and
mention it in your report. On a hybrid-GPU PC where the wrong GPU is picked, try
`DXVK_FILTER_DEVICE_NAME="<part of your GPU's name>" ./sc-offline.sh`.

## Messages you can ignore

These appear on a working setup and aren't errors:

- `MESA-EGL: warning: ... failed to create dri2 screen`, several times per launch, on PCs with an NVIDIA and an AMD GPU.
- `OpenXR-Loader ... failed to load a runtime` and `OpenVR: Failed to locate module` when no VR runtime is installed.
- `readMonitorEdidFromKey: Failed to get EDID reg key size` and `DXGI: Failed to parse display metadata` from DXVK.
- `status` listing the same install twice, once as `C:\Program Files\...` and once as `Z:\...\drive_c\Program Files\...`:
  that's the prefix's own `drive_c` seen through Wine's `Z:` drive.
- Discord status doesn't show: Wine can't usually reach the Discord app's pipe.

## Reporting

Send the script's output, `data/mod.log`, and the game's `Game.log`. Say which distro, GPU, install tool
(LUG Helper, Lutris, Steam) and Wine runner you used.
