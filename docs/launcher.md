# The launcher: `sc-offline.exe`

The launcher starts the game with the mod and takes the mod out again when the game closes. From 0.8.0, `sc-offline.exe` is a new launcher. The older one ships beside it as `sc-offline-classic.exe`, the [fallback](#the-classic-launcher). Both read the same `sc-offline.ini`, the same `data` folder and the same `%ProgramData%\sc-offline\pc-changes.txt` record.

The new launcher's `play` command and the steps it takes with administrator rights have not been run in the real game. [Status](status.md#ships-in-080-but-not-run-in-game) says what was run. The maintainer's game sessions used the classic launcher.

## Commands

```text
sc-offline.exe [play|install|uninstall|status|mcp|update|stop|report|plugins|help] [--game <folder>] [--dry-run] [--skip-eac-check] [--game-log] [--end] [--no-game-log]
```

| Command | What it does |
| --- | --- |
| `play` | The default from a terminal. A double-click opens the [window](#the-window). Copies the mod in, starts the game, takes the mod out when it closes. |
| `install` | Copies the mod in and leaves it there, for starting the game some other way. Run `uninstall` before going online. The window has no Install button. |
| `uninstall` | Takes the mod out, puts back what it replaced, and undoes leftover [PC changes](#pc-changes). Refuses while the game runs. The window's **Repair** button runs it. |
| `status` | Runs the [checks below](#checks-it-runs-every-time). It only reads: it writes no file, changes nothing, opens no folder picker and makes no network request. If it cannot find the game folder it says so. |
| `mcp` | A read-only server that lets an [AI agent](ai-agents.md) see status, plugins, log tails and `sc-offline.ini`. `Game.log` is served only with `--game-log`, because it can hold account names. |
| `update` | Checks GitHub for a newer sc-offline and offers to install it ([Updates](#updates)). `--check` only checks, `--yes` does not ask, `--prerelease` also offers test builds. The binary accepts these three flags, but `sc-offline.exe help` does not list them. |
| `stop` | Asks the running game of this install to close, like the window's Stop ([below](#stop)). Exit `0` when it closed or nothing was running, `1` when the game folder is unknown or the game is still running. `--end` ends the process if it is still open a minute later. |
| `report` | Writes a [problem report](#problem-report-on-request) into `data\reports`: the status check and the logs in one zip, with your names replaced. It only reads the game folder and sends nothing. `--no-game-log` leaves `Game.log` out. The window's **Create report** button runs it. |
| `plugins` | Manages the plugins in `data\plugins` ([Plugin commands](#plugin-commands)). |

| Option | Meaning |
| --- | --- |
| `--game <folder>` | Your `Roberts Space Industries`, `StarCitizen`, channel or `Bin64` folder. Overrides `game =` in the ini. |
| `--dry-run` | Prints every step it would take and stops. Nothing is copied, deleted or started. |
| `--skip-eac-check` | Don't stop when Easy Anti-Cheat looks active. |

`help` prints the usage. Exit codes: `0` ok, `1` error, `2` Easy Anti-Cheat is active, `3` the game is running. A PC change that fails is exit `1` too: the launcher prints "the helper failed" with the elevated helper's own code (`5` for a PC change) and `data\launcher.log` has the details. Everything the launcher prints also goes to `data\launcher.log`, except for `status`. Each run starts a new file and keeps the previous one as `launcher.log.1`.

### Plugin commands

`sc-offline.exe plugins` works on `data\plugins` the way the game reads it. Each command refuses while the game runs and takes effect at the next game start.

- `plugins list [--json]` shows each plugin, its details and whether it is on. `enable <id>` and `disable <id>` do what the window's switch does.
- `install <folder|zip>` takes a folder or a `.zip` with a `plugin.ini` inside. The plugin lands **off**; turn it on with `enable`. A native plugin (a DLL) runs its own code inside the game, so install only plugins you trust. `--replace` replaces a plugin with the same id. `remove <id>` deletes one, and `--purge` also deletes its saved settings.
- `settings <id> [name=value ...]` shows a plugin's settings and their saved values. With `name=value` words it saves them. `--reset` goes back to the defaults. A value that does not fit writes nothing.
- **The `plugins catalogue` commands do nothing yet, because no plugin catalogue is published.** The launcher accepts only a list signed with a key it was built with, and this release has none. The commands say "no catalogue is published for this build yet", and so does the window's Catalogue tab. Add plugins with `install` instead.

## The window

A double-click opens the window. It speaks English and Russian (**Language** on the Settings page; Auto follows Windows). If it cannot open, run `sc-offline.exe play` from a terminal. Under Wine the console run is the default, and `--window` opens the window anyway. After a double-click, `--console` runs `play` in the console instead. The sidebar has five pages.

**Home** shows the verdict with **Play** beside it, the smaller actions under them, and your status below. The window is dark whatever theme Windows uses.

- **Play** and **Update** run the same commands as the terminal. Their questions appear on the **Output** page with a button for each answer (or press Y or N), and the taskbar button flashes when one is waiting. A question nobody answers takes its default (no, keep) when the command ends. Both buttons are disabled while a command or the game runs.
- **Repair banner.** When a crash left PC changes behind, a banner lists them and offers **Repair: undo PC changes**, which runs `uninstall`.
- **Build check.** Under **Game**, the window says how your game build stands to the build this release was checked on: the same build, an earlier one from Core's list, or one nobody checked (a newer patch, a hotfix, a test channel). Nothing is refused on the answer. On a build nobody checked the mod usually still starts, and each feature whose code moved turns itself off; `data\mod.log` has a `[core] signatures:` line with the count and a `[core] MISSING` line for each one. "Checked" means a static check of the game's executable, not an in-game run ([Status](status.md)). The terminal's `status` does not print this line yet.
- **Status panel.** The same checks as `status`: game folder and build, whether the mod is installed, PC changes left over, whether the network is blocked while you play, Easy Anti-Cheat, how many plugins are on, and the SDK version. It refreshes by itself. The line above the rows says **Safe to go online**, **Not safe to go online** (with what is left), or **Game folder not known yet**.

**Output** shows what the running command prints, with a **Copy** button. **Plugins** is described [below](#the-plugins-page). **Capabilities** lists the [capability switches](#capability-switches-in-sc-offlineini) as toggles; a change applies at the next Play.

**Settings** has **Repair: undo PC changes**, **Game logs after a session**, **Language**, **In-game menu** (**Classic**, or **New**, which has not been run in game), **Plugins** on or off, **Check for updates**, **Create report** (with **Open the reports folder**), **Edit sc-offline.ini** and **Open folder**. They are read-only while the game or a command runs, except **Create report**, which only reads and also works while the game runs. Browse (the folder picker, also shown when no game is found) and the restart after an update have not been run in game.

### Stop

While the game runs, **Play** becomes **Stop**, which closes the game in steps:

1. If the mod is loaded, the launcher asks it to do what a normal quit does: save your wallet, close the private session and tell the plugins the game is exiting. The button shows **Saving...** for up to 10 seconds. If the mod does not answer, the window says so and goes on without the save.
2. The launcher asks this install's `StarCitizen.exe` windows to close. It never touches another install.
3. The `play` that started the game keeps waiting, so the usual cleanup runs when the game exits: the mod comes out and the PC changes are undone.
4. If the game is still running after 60 seconds, the window asks **End the game process?** **Yes** ends it. **No** leaves it, and you can press **Stop** again. Nothing ends the game by itself.

Closing the game window yourself skips the mod's exit work, so the wallet is not saved.

### The Plugins page

- **List.** Every plugin folder, with its name, version and author. A native plugin shows a warning that it runs its own code. The list refreshes every few seconds.
- **On and off.** The switch takes effect at the next game start. **Add a plugin** takes a folder or a `.zip`; it lands off, like `plugins install`. The Catalogue tab stays empty until a catalogue is published.
- **Plugins are off.** The game loads no plugin unless `plugins = on` (the default). When it is off the page says so, and **Turn on** fixes it.
- **Settings** (the gear, for a plugin that has settings). Edit the values the plugin declares. **Save** keeps them for the next game start. **Reset** goes back to the defaults. The fields are read-only while the game or a command runs.
- **Reload** only shows a note. Reloading a plugin happens inside the game with `sco.reload`.
- The `creative` plugin (noclip, god mode, infinite ammo) is **on** by default. Switch it off here if you do not want it.

### The classic launcher

`sc-offline-classic.exe` is the launcher of 0.7.x, kept as the fallback. Use it if the new launcher does not start, the window does not open, or a step fails on your PC.

- Run it from the same folder with `play`, `install`, `uninstall`, `status` or `mcp`. It has no `stop` or `plugins` command, and it shows its older window when double-clicked.
- It uses the same ini, `data` folder and PC-change record, so you can switch between the two launchers at any time.
- **Undo PC changes with it:** `sc-offline-classic.exe uninstall` takes the mod out of the game folder and undoes every change in `%ProgramData%\sc-offline\pc-changes.txt`. It refuses while the game runs. Then `sc-offline-classic.exe status` must show nothing left over. If neither launcher runs, [PC changes](#pc-changes) lists each change so you can undo it by hand.
- It always installs `dinput8.dll`, the mod with the classic menu. With `menu = rust` you still get the classic menu, and the mod logs one line saying the new menu needs `sc-offline.exe`.
- **Update only with `sc-offline.exe update`.** Never run the classic launcher's `update`: it would put the old launcher back in place of the new `sc-offline.exe`.

### Updating from 0.7.x

To go from 0.7.x to 0.8.0 once, do one of two things: extract the 0.8.0 zip into the old folder, or run `update` in the 0.7.x launcher you have now. From then on, update only with `sc-offline.exe update`. Your `sc-offline.ini` and your files in `data\` (wallet, saved places, logs, saves) are kept. The update replaces `sc-offline.exe` with the new launcher, checks that it starts, and runs it. If that check fails, the old files are put back. Settings that are new in this release are added to your ini, commented out, so their defaults apply. Afterwards `sc-offline-classic.exe`, `sc_offline_menu.dll`, `dinput8-rust-menu.dll` and the new plugin folders are in place. No real update from 0.7.x has been run yet ([Status](status.md#ships-in-080-but-not-run-in-game)).

### Release candidates

A release candidate has a tag such as `v0.8.0-rc.1` and is published as a GitHub **pre-release**, never as the latest release. Both launchers order versions the same way: `0.7.8` < `0.8.0-rc.1` < `0.8.0-rc.2` < `0.8.0`. A candidate is below its own final release and above the release before it.

- **Offered or not.** On the default `stable` channel a launcher is offered full releases only, so a 0.7.x launcher is not offered a candidate, and a launcher at `0.8.0-rc.1` is not offered `0.8.0-rc.2`. It is offered `0.8.0` once the maintainer has published 0.8.0 as a full release.
- **Getting a candidate.** Download `sc-offline-v0.8.0-rc.1.zip` from the [pre-release page](https://github.com/scuba-sc/sc-offline/releases/tag/v0.8.0-rc.1) and extract it into its own folder. To update an install you have, use the pre-release channel. From 0.8.0-rc.1 on, run `sc-offline.exe update --prerelease`, or set `update_channel = prerelease` in `sc-offline.ini`. The 0.7.7 launcher has no `--prerelease` option: set `update_channel = prerelease` in its `sc-offline.ini`, then run `sc-offline.exe update`. Both read the same release list and check the zip against GitHub's digest and `manifest.json`, as for any update. The 0.7.7 path was checked by reading its source and by tests of the shared version rules, not by an update from a real 0.7.7 install.
- **Test first**, and stop at the first failure: `sc-offline.exe status`; play with the default menu (`menu = imgui`) and press **M**; the classic launcher `sc-offline-classic.exe play`; then the new menu (`menu = rust`). Report problems as an [issue](https://github.com/scuba-sc/sc-offline/issues/new/choose) with the lines from `data\mod.log` and `Game.log`.
- **Going back.** Run `sc-offline-classic.exe uninstall` (or `sc-offline.exe uninstall`), then `status` must show nothing left over. Extract the 0.7.7 zip from the [releases page](https://github.com/scuba-sc/sc-offline/releases) into a folder. Your `sc-offline.ini` and `data\` can be kept. Set `update_channel = stable` again if you changed it, so a launcher does not offer candidates.

## Checks it runs every time

1. **Which DLL.** What the `dinput8.dll` next to the exe is: an sc-offline build (with its version), an older ChrisWareOffline build, or unknown. With the original author's prebuilt DLL it warns unless `boot_map = PU`.
2. **Game updated?** The game version, compared with the one from your last play. A change means a game update may have broken the mod.
3. **Leftovers.** The mod is still in the game folder while the game is not running (after a crash, or `install`). It tells you to run `uninstall`.
4. **PC changes left over.** A previous run's changes are still recorded. `play` asks whether to undo them and stop, or to play and undo everything afterwards.
5. **Easy Anti-Cheat.** Whether `C:\Program Files (x86)\EasyAntiCheat_EOS\EasyAntiCheat_EOS.exe` exists (active), exists only as `.exe.bak` (disabled) or is absent, and whether your hosts file blocks `modules-cdn.eac-prod.on.epicgames.com`. If it is active and `eac_rename = off`, `play` and `install` stop and print the fix.

## What `play` does

1. **Finds the game's `Bin64` folder.** It takes the first of these that has `<channel>\Bin64\StarCitizen.exe`: `--game`, then `game =` in the ini; the folder it found last time (`data\game-path.txt`); where the RSI Launcher says the game is; the usual folders on every fixed drive; a search of every fixed drive, four folders deep; and, after a double-click, a folder picker. With more than one install it uses the first and lists the others.
2. **Runs the checks** above.
3. **Sets the environment variables** the mod reads ([data-files.md](data-files.md#environment-variables)).
4. **Starts a helper**, a second copy of `sc-offline.exe` with no window, which changes the game folder:

   | Path | Going in | Coming out |
   | --- | --- | --- |
   | `Bin64\dinput8.dll` | The mod's DLL ([which one](#which-game-dll)). If another mod's `dinput8.dll` is there, it is first renamed `dinput8.dll.sc-offline-backup`. | Deleted; the other mod's DLL renamed back. |
   | `<channel>\user\client\0\default_1.xml` | The starting loadout from `data\OfflineDB`. Yours is first copied to `default_1.xml.sc-offline-backup`. | Your copy restored, or ours deleted if you had none. |
   | `Bin64\sc-offline.installed` | A note of what was installed and when. | Deleted. |

   Before it copies the mod in, the helper makes the [PC changes](#pc-changes). It runs as administrator when any PC change is on (the default) or the game folder needs it, and Windows then asks once. The game never runs as administrator.
5. **Starts `StarCitizen.exe`** and waits until every `StarCitizen.exe` has exited. The helper then takes the mod out, even if you closed the launcher window first. Ctrl+C in the launcher window is ignored; close the game instead.

If the PC crashes mid-game, the mod stays in the game folder. The next `play` or `status` notices, and the window shows its Repair banner. Use **Repair** or `sc-offline.exe uninstall` before going online.

### Which game DLL

The package holds two game DLLs built from the same sources, and you never need to handle them. `dinput8.dll` has the classic menu and is the build that has run in game. `dinput8-rust-menu.dll` has the hook for the new menu (`sc_offline_menu.dll`) and has never run in game. `sc-offline.exe` installs `dinput8-rust-menu.dll`, under the name `dinput8.dll`, only when the ini says `menu = rust` and that file is beside the launcher. In every other case it installs `dinput8.dll`, and `sc-offline-classic.exe` always does. When you change the `menu` key, the next Play swaps the installed DLL.

## PC changes

While you play, the helper makes three changes outside the game folder and undoes them when every `StarCitizen.exe` has exited. Each is an ini switch, on by default.

| Key | Going in | Coming out |
| --- | --- | --- |
| `block_network` | Windows Firewall rules in both directions: `sc-offline: block StarCitizen.exe` (this install's exe only), `sc-offline: block RSI Launcher.exe` and `sc-offline: block CrashHandler.exe` (CIG's crash reporter). A rule is added only if its exe exists. `sc-offline.exe` itself stays online for updates. With `multiplayer = on` (the code default; the shipped `sc-offline.ini` sets `off`), the game's rule leaves your local network open (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `169.254.0.0/16`, loopback and `multiplayer_allow`) and one more rule, `sc-offline: allow StarCitizen.exe multiplayer (LAN)`, lets PCs on your subnet reach the game over UDP. All IPv6 stays blocked. If Windows refuses the narrower rule, every address is blocked and the launcher says so. | Deleted. |
| `eac_hosts` | `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com # added by sc-offline...` is added to the hosts file, then DNS is flushed. Skipped if the hosts file already blocks it. | Only the tagged line is removed; DNS flushed. |
| `eac_rename` | `EasyAntiCheat_EOS.exe` is renamed to `EasyAntiCheat_EOS.exe.bak`. Skipped if it is not there. | Renamed back. |

- Each change is written to `%ProgramData%\sc-offline\pc-changes.txt` the moment it is made, and only recorded changes are undone. A hosts line or `.bak` you made yourself is never touched.
- If a step fails, the helper undoes what it already did and the game does not start.
- After a crash the record stays. `status` lists it, `uninstall` undoes it (**Repair** in the window), and `play` offers to.
- The firewall rules cut the game's network for the session. They hide nothing already on disk, such as logs or the renamed EAC file.

### Administrator rights

The launcher never asks to run as administrator, and the game never runs as administrator. Windows asks once, only for a step that needs it: the helper (when the firewall, EAC or a protected game folder is involved), deleting session logs from a protected game folder, and updating a folder only administrators can write, such as one under Program Files.

If the mod folder's `data` cannot be written, the mod cannot save your wallet or places, and the launcher tells you to move the folder (for example to your Desktop). Start `sc-offline.exe` with normal rights. To launch the game with normal rights, remove *Run this program as an administrator* from the `StarCitizen.exe` properties.

## Updates

On `play` the launcher asks GitHub for the newest release of `scuba-sc/sc-offline` (3-second timeout; no network just skips it). With `update_channel = stable` (the default) only full releases are offered; `prerelease` also offers test builds, such as a [release candidate](#release-candidates). If a newer one exists, `play` asks **"Update now?"**. `status` makes no network request. Run `sc-offline.exe update` to check on demand. Nothing updates while the game runs.

An update:

1. checks free disk space, then downloads `sc-offline-<tag>.zip`. Its SHA-256 must match the digest GitHub publishes for that file. A mismatch or a missing digest stops it with nothing changed.
2. unpacks it and checks every file against the `manifest.json` inside (see "What an update is checked against" below). Any problem stops it with nothing changed.
3. stages the files, then replaces every listed file except `sc-offline.ini` and your files in `data\`. When the folder is not writable (Program Files), only this last step asks Windows for administrator rights.
4. adds settings that are new in the release to your ini, commented out.
5. checks that the new launcher starts, then starts it with the same arguments.

If a step fails, or the new launcher does not start, the old files are put back at once. If the PC dies mid-update, the next run of `sc-offline.exe` puts them back. `check_updates = off` turns the automatic check off.

**What an update is checked against.** Two things vouch for an update, and neither is a code signature: releases are not code-signed. The zip must match **GitHub's digest**, which is served over HTTPS. And every file in it must match **`manifest.json`**, which CI writes when it builds the release: the version, the tag, the commit, and each file with its SHA-256 and size. Only listed files are copied, and a listed path that tries to leave the folder fails the whole update. So the update is exactly what CI built from that commit, as long as the GitHub release is trustworthy. Anyone who can publish a release on `scuba-sc/sc-offline` can publish an update.

**Checking a release by hand.** In PowerShell, in the folder you downloaded the zip to, compare the zip's hash with the digest on the release page (or `gh release view v0.8.0 -R scuba-sc/sc-offline --json assets --jq '.assets[] | [.name, .digest]'`):

```powershell
(Get-FileHash .\sc-offline-v0.8.0.zip -Algorithm SHA256).Hash.ToLower()
```

With Python, `python3 tools/release-manifest.py check sc-offline-v0.8.0` from a clone of this repository checks every unpacked file against `manifest.json`, flags unlisted files, and exits non-zero on any problem. The `commit` field in the manifest points to the exact source at `https://github.com/scuba-sc/sc-offline/tree/<commit>`.

## Discord status

The Discord plugin (`data\plugins\discord`) is **off** until a decision is made on how it would send presence and what it would send. If you turn it on by hand, only local diagnostics load: `discord.status` says "unavailable" and why. It sends nothing and opens no pipe or network connection. See the [transport and privacy review](../plugins/discord/README.md). A legacy `discord_presence = off` ini line is still migrated.

## Crash reports and session logs

If the game wrote crash files during the session, the launcher offers a report before the log cleanup. **y** zips `mod.log`, `launcher.log`, `Game.log` and the crash folder's text files into `data\crash-reports\sc-offline-crash-<time>.zip`. In the copies, your RSI handle, nickname, character name, account numbers and e-mail addresses become `<redacted>`, and your Windows user name becomes `<user>`. Free text can still hold a name no log line announced, so read the zip before you share it. Memory dumps (`.dmp`) are left out because they cannot be redacted. **Nothing is uploaded**: you attach the zip. `crash_reports = off` turns this off.

### Problem report on request

For a bug that is not a crash, `sc-offline.exe report` (or **Settings > Create report** in the window) makes the same kind of bundle whenever you ask: `data\reports\sc-offline-report-<time>.zip`. It holds:

- `status.txt`, the output of `status`;
- `sc-offline.ini` and `plugins.txt` (each plugin folder, on or off, with the id, version and kind from its `plugin.ini`);
- `data\mod.log` and `data\launcher.log`;
- the game's `Game.log`, unless you pass `--no-game-log`;
- the text files of crashes from the last 7 days (at most 32, newest first). Memory dumps are left out.

Every file goes through the same replacement as the crash report, and the command lists what it put in with each file's size. It writes only into `data\reports` and reads the game folder, so it also works while the game runs. **Nothing is sent.** Read the zip, then attach it to your bug report. From a terminal the command offers to open the bug form; the window opens the reports folder. Of a very large log only the last 16 MiB are kept. Not run in game.

When the game closes, the launcher lists the logs the game wrote this session (`Game.log`, new files in `logbackups` and `Crashes`) and asks **"Delete these files?"** The answers are **Delete** (after one more **"Are you sure?"**), **Keep** (also Enter), **Always delete** and **Never delete**. The last two are saved as `clean_logs = always` or `never`. Anything unclear keeps the files. Only this session's files are deleted. Older files, `data\mod.log` and `data\launcher.log` are never touched. Keep `Game.log` if you want to report a bug.

Under Wine none of the PC changes run; `sc-offline.sh` handles the hosts file there, and the firewall rules do not apply ([Linux](linux.md)).

## Troubleshooting

- **The game crashes during startup on an NVIDIA GPU.** Turn off **Smooth Motion** for Star Citizen in the NVIDIA App, then start the game again.
- **The new launcher does not start or a step fails.** Use `sc-offline-classic.exe` ([how](#the-classic-launcher)).
- **A PC change is left over after a crash.** Run **Repair** in the window, or `sc-offline.exe uninstall` ([PC changes](#pc-changes)).

## `sc-offline.ini`

Each line is `key = value`. A line that starts with `#` is a comment. The launcher reports an unknown key at start. Your aUEC balance is in `data\wallet.txt` ([Features](features.md#player)), not in the ini.

`start` and `start_ship` are old keys. The [spawn plugin](../plugins/spawn/README.md) refuses destination startup, so `start = Daymar` does not bring back the old timed arrival or boarding.

| Key | Default | Meaning |
| --- | --- | --- |
| `game` | found automatically | Your Star Citizen folder: `Roberts Space Industries`, `StarCitizen`, the channel folder, or the folder that holds `StarCitizen`. |
| `channel` | `LIVE` | Which install to use when `game` points above it: `LIVE`, `PTU`, `EPTU` and so on. |
| `boot_map` | `PU_All` | `PU_All` loads every star system. Older ChrisWareOffline DLLs understand only `PU`. |
| `plugins` | `on` | Load plugins from `data\plugins\<id>\` ([Plugins](features.md#plugins)). |
| `relocation` | `on` | `off` stops every plugin from moving you: F7/F8, Travel, saved spots. Movement also needs a confirmed offline session, a live player and a known star system. |
| `menu` | `imgui` | The in-game menu. `imgui` is the classic menu. `rust` is the new menu and works only with `sc-offline.exe` ([Which game DLL](#which-game-dll)). It opens on **M**. The Settings page writes this key. |
| `asop` | `on` | `off` leaves the ship terminals, personal hangars, the hangar lift and ATC hails as the game has them offline ([Features](features.md#ship-terminals-hangars-and-atc)). |
| `asop_fleet_list` | `ships` | The ship list the terminals show: `ships` (every ship in `data\ships.txt`) or `game` (the game's own; offline it is empty). |
| `multiplayer` | `off` (shipped ini; `on` if the key is missing) | Only for the separate co-presence plugin, [sc-mp](features.md#multiplayer), which is not in this package. `on` changes two firewall rules (see `block_network` under [PC changes](#pc-changes)): the game's block rule leaves your local network open, and an inbound UDP allow rule is added for it. `off` adds neither, and the block rule covers every address. An `sc-offline.ini` you already have keeps its own value. |
| `multiplayer_allow` | empty | Firewall exceptions besides your LAN, for a VPN: up to 8 IPv4 ranges separated by commas (Tailscale: `100.64.0.0/10`). Each whole range must lie within `10/8`, `127/8`, `169.254/16`, `172.16/12`, `192.168/16` or `100.64/10`; allow only your VPN's range. The launcher clears values that do not parse and warns. |
| `block_network` | `on` | Block `StarCitizen.exe`, the RSI Launcher and `CrashHandler.exe` in Windows Firewall while you play ([PC changes](#pc-changes)). |
| `eac_hosts`, `eac_rename` | `on` | Add the EAC hosts line, and rename `EasyAntiCheat_EOS.exe`, while you play. |
| `clean_logs` | `ask` | After the game closes: `ask` lists this session's logs and asks before deleting; `always` deletes without asking; `never` keeps them. |
| `crash_reports` | `on` | After a crash, offer a redacted log bundle and the bug form ([Crash reports](#crash-reports-and-session-logs)). |
| `check_updates` | `on` | Check GitHub on `play` and offer to install a newer release. |
| `update_channel` | `stable` | `stable` offers full releases only; `prerelease` offers test builds too. |
| `language` | `auto` | Launcher language: `auto`, `en` or `ru` ([Localization](localization.md)). |

### Capability switches in `sc-offline.ini`

Some parts of the mod start only when you ask for them. Each is a `key = on` or `off` line in the ini, and the Capabilities page in the launcher window sets the same keys. You never edit environment variables by hand.

| Key | Default | What it asks for |
| --- | --- | --- |
| `exp_loadout_async` | `on` | The menu equips gear. This is the one that was run in game and worked. |
| `exp_inventory_read` | `on` | Plugins read what you wear and carry. |
| `exp_actor_stats_write` | `on` | Plugins change your survival stats. |
| `exp_cvars_native` | `on` | Plugins read four console settings, read only. |
| `exp_audio_native` | `on` | Native one-shot sounds. This build refuses them and logs why. |
| `exp_entities_diag` | `off` | Extra log lines about game entities, for bug reports. No gameplay change. |

- **A switch asks, the game decides.** On means the launcher asks the mod to start the capability. It does not mean the capability works. [Status](status.md#ships-in-080-but-not-run-in-game) says which have been run in game. [Capabilities](capabilities.md) has the full table.
- **Off means off.** A variable left in your shell or in Windows cannot turn a capability on behind your back: the launcher removes it from the game's environment.
- **Values.** `on` or `off` (also `yes`/`no`, `1`/`0`, `true`/`false`). Anything else keeps the default and is reported.
- **At start** the launcher prints one line listing the switches that are on. When an update adds a key to your ini, it is appended commented out, and the default applies.
