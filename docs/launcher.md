# The launcher: `sc-offline.exe`

The launcher starts the game with the mod and takes the mod out again when the game closes. From 0.8.0 `sc-offline.exe` is the Rust launcher (`launcher-rs/`, crate `scol-cli`). The C++ launcher it replaces ships beside it as `sc-offline-classic.exe`, the [fallback](#the-classic-launcher). Both read the same `sc-offline.ini`, the same `data` folder and the same `%ProgramData%\sc-offline\pc-changes.txt` record.

Not run in game. That covers the Rust launcher's `play` path and its elevated helper. [Status](status.md#080-what-was-and-was-not-run-in-game) says what they were checked against instead. The owner's in-game sessions used the classic launcher.

## Commands

```text
sc-offline.exe [play|install|uninstall|status|mcp|update|stop|plugins|help] [--game <folder>] [--dry-run] [--skip-eac-check] [--game-log] [--end]
```

| Command | What it does |
| --- | --- |
| `play` | The default from a terminal. A double-click opens the [window](#the-window). Copies the mod in, starts the game, takes the mod out when it closes. |
| `install` | Copies the mod in and leaves it there, for starting the game some other way. Run `uninstall` before going online. The window has no Install button. |
| `uninstall` | Takes the mod out, puts back what it replaced, and undoes leftover [PC changes](#pc-changes). Refuses while the game runs. The window's **Repair: undo PC changes** runs it. |
| `status` | Runs the [checks below](#checks-it-runs-every-time). Read-only: it writes no file (not `data\launcher.log`, not `data\game-path.txt`), migrates nothing, settles no interrupted update, opens no folder picker and makes no network request. If it cannot find the game folder it says so. The window's Status panel runs the same checks. |
| `mcp` | A read-only [MCP server for AI agents](ai-agents.md) on stdin/stdout: status, installed plugins, log tails and `sc-offline.ini`. It writes nothing, makes no network request and runs no command. `Game.log` is served only with `mcp --game-log`, because it can hold account names. |
| `update` | Checks GitHub for a newer sc-offline and offers to install it ([Updates](#updates)). `--check` only checks, `--yes` does not ask, `--prerelease` also offers pre-releases. |
| `stop` | Asks the running game of this install to close, like the window's Stop ([below](#stop)). Exit `0` when it closed or nothing was running, `1` when the game folder is unknown or the game is still running. `--end` ends the process if it is still open a minute later. |
| `plugins` | Manages `data\plugins` ([Plugin commands](#plugin-commands)). |
| `help` | Prints the usage. |

| Option | Meaning |
| --- | --- |
| `--game <folder>` | Your `Roberts Space Industries`, `StarCitizen`, channel or `Bin64` folder. Overrides `game =` in the ini. |
| `--dry-run` | Prints every step it would take and stops. Nothing is copied, deleted or started. |
| `--skip-eac-check` | Don't stop when Easy Anti-Cheat looks active. |
| `--window` | Open the window from a terminal, or under Wine/Proton. |
| `--console` | Double-clicked: run `play` in the console instead of the window. |
| `--end` | With `stop` only. |
| `--game-log` | With `mcp` only. |

Exit codes: `0` ok, `1` error, `2` Easy Anti-Cheat is active, `3` the game is running. The helper's own code, in `launcher.log`: `5`, a PC change failed.

Everything the launcher prints also goes to `data\launcher.log`, except for `status`. Each run starts a new file and keeps the previous one as `launcher.log.1`. The helper appends to the file.

### Plugin commands

`sc-offline.exe plugins` works on `data\plugins` the way the host reads it. Changes are journaled, each refuses while the game runs, and each applies at the next game start.

```text
plugins [--root <launcher folder>] list [--json]
plugins enable <id>
plugins disable <id>
plugins install <folder|zip> [--replace]
plugins remove <id> [--purge]
plugins settings <id> [name=value ...] [--reset]
plugins catalogue list [--source <folder>]
plugins catalogue install <id> [--source <folder>] [--replace] [--allow-downgrade]
plugins catalogue update [<id>] [--source <folder>] [--allow-downgrade]
plugins update [<id>] [--source <folder>] [--allow-downgrade]
```

- `list` shows each plugin folder, its `plugin.ini` fields and whether it is on. `--json` prints the same as JSON.
- `enable` and `disable` delete or create the empty `disabled` file in the plugin's folder, as the window's switch does.
- `install` takes a folder or a `.zip` that holds a `plugin.ini`. The plugin lands **disabled**; turn it on with `enable`. A native plugin (a DLL) runs its own code inside the game, so install only plugins you trust. `--replace` replaces a plugin with the same id. `remove` deletes one, and `--purge` also deletes its saved settings in `data\storage`.
- `settings` shows a plugin's `[settings]` and their kept values. With `name=value` words it saves them to `data\storage\<id>.db`, read at the next game start. `--reset` deletes the kept values. A value that does not fit its declaration writes nothing.
- The `catalogue` commands read a **signed index in a local folder** (`--source`). No production signing key ships with 0.8.0, so the built-in trust root is empty: an index is accepted only when it is signed by a key the launcher was built with. There is no network catalogue yet, and without `--source` the commands say "no catalogue is published for this build yet". The window's Catalogue tab is the same code. The catalogue is meant to carry first-party and reviewed plugins only; each lands disabled with a native-code warning. `--allow-downgrade` lets an older version replace a newer one.

## The window

A double-click (no arguments) opens the window. It is drawn with egui and speaks English and Russian (**Language** on the Settings page; Auto follows the Windows UI language). If it cannot open, run `sc-offline.exe play` from a terminal. Under Wine the console run stays the default, and `--window` opens the window anyway.

The sidebar has five pages.

**Home** has the *Status* panel on the left and the actions on the right.

- **Play** and **Update** run `sc-offline.exe <command>` as a hidden child with `SC_OFFLINE_GUI=1`, so the window and the command line share one code path. The child's `[y/N]` questions become **Yes** / **No** buttons (or press Y / N). The session-log question has four answers ([Session logs](#session-logs)). Both buttons are disabled while a command or the game runs.
- **Repair banner.** When `pc-changes.txt` holds a record and no command or game is running, a banner lists what is left and offers **Repair: undo PC changes**, which runs `uninstall`.
- **Status panel.** The same read-only checks as `status`: game folder and build, whether the mod is installed, PC changes left over, whether the network is blocked while you play, Easy Anti-Cheat, how many plugins are enabled, and the SDK version. It refreshes when the window opens, when the game or a command starts or ends, every 5 seconds while either runs, and when you press the refresh icon. While you play, the Mod row reads "Installed (this session)". The line above the rows says **Safe to go online**, **Not safe to go online** (or **yet**, with what is left), or **Game folder not known yet**.

**Output** shows what the running command prints, with a **Copy** button. **Update** and **Repair** switch to it.

**Plugins**: [below](#the-plugins-page). **Capabilities**: [below](#capability-switches).

**Settings**: **Repair: undo PC changes** (it says "Nothing to repair" when there is nothing); **Game logs after a session** (`clean_logs`); **Language**; **In-game menu** (**Classic**, or **New (not tested in game yet)**: the `menu` key, applied at the next Play); **Plugins** on or off; **Check for updates**; **Edit sc-offline.ini**; **Open folder**. Settings are read-only while the game or a command runs.

Browse (the folder picker, also shown when no game is found) and the restart after an update are built but have not run in game.

### Stop

While the game runs, **Play** becomes **Stop**, which closes the game in steps:

1. If the mod is loaded in the game, the launcher signals its per-process event `Local\sc-offline-stop-<pid>` (same user only) and waits up to 10 seconds for `Local\sc-offline-stopped-<pid>`. The button shows **Saving…**. On the game thread the mod then does what a normal quit does: saves the wallet, closes the private session, sends `game.exit` to plugins and closes admission. It logs `[app] stop requested by the launcher: exit work done` in `data\mod.log`. It unloads nothing and does not quit the game.
2. If the mod is installed but does not answer, the window says "The mod didn't answer; closing without its save." and goes on. It goes straight on when the mod is not loaded, the game is online, or the game exits meanwhile.
3. The launcher posts WM_CLOSE to the visible windows of this install's `StarCitizen.exe`. The game folder is resolved as Play does, and Stop refuses when it is unknown. It never touches another install.
4. The `play` that started the game keeps waiting, so the usual cleanup runs when the game exits: the mod comes out and the PC changes are undone.
5. If the game is still running after 60 seconds, the card asks **"End the game process?"**. **Yes** ends that process (the one that was asked to close, checked by its creation time). **No** leaves it, and **Stop** can be pressed again. Nothing ends the game by itself.

After Stop the mod stays closed until the game exits: its menu, hotkeys and timers stop and nothing is saved again. Closing the game window with WM_CLOSE alone skips the mod's exit work, so the wallet is not saved. Each step is written to `data\launcher.log` with a time.

### The Plugins page

The page manages `data\plugins\<id>\` the way the host reads it.

- **List.** Every folder with a `plugin.ini` (others are skipped), showing `name`, `version` and `author`; `description` is the tooltip. A native plugin shows a native-code warning. Folders are rescanned every 3 seconds.
- **On / off.** The switch creates or deletes the empty `disabled` file. The host reads it at game start.
- **Add a plugin.** Pick a folder or a `.zip`. It lands disabled, like `plugins install`.
- **Catalogue tab.** The plugins of a signed index; see [Plugin commands](#plugin-commands).
- **Plugins are off.** The host loads no plugin unless `plugins = on` (the default). When it is off the page says so, and **Turn on** writes the key.
- **Settings** (the gear, for a plugin whose `plugin.ini` has `[settings]`). Edits the values the plugin declared (`bool`, `int`, `float`, `string`, `enum(...)`), checked against the declared range and choices. **Save** writes them to the plugin's database `data\storage\<id>.db`, table `sco_kv`, key `sco.settings.<name>`, value `<type>:<value>`. **Reset** deletes the kept values, so the defaults apply. The fields are read-only while the game or a command runs, because the host reads its settings once, at start.
- **Reload** only shows a note. Hot reload runs inside the game through `sco.reload`, and the launcher has no channel into the running game.
- The `creative` plugin (noclip, god mode, infinite ammo) ships **on**. Switch it off here if you do not want it.

The page can show a **Last startup snapshot (may be stale)** from the optional `data\plugin-startup.json`. Nothing writes that file yet, so the page normally shows **No completed startup snapshot**. The reader in `launcher-rs/crates/scol-plugins` specifies the format.

### Capability switches

The **Capabilities** page lists the `exp_*` keys of `sc-offline.ini` as switches. Each card has a short name, a sentence, an On/Off toggle and an evidence line; the ini key and the Core capability name are in its details. A toggle writes its key, and the game reads it at the next Play. Capabilities that have no switch are read-only rows under "Works by itself, no switch" and "Not available yet". The keys are described under [`sc-offline.ini`](#sc-offlineini).

### The classic launcher

`sc-offline-classic.exe` is the C++ launcher of 0.7.x, kept as the fallback. Use it if the Rust launcher does not start, the window does not open, or a step fails on your PC.

- Run it from the same folder with `play`, `install`, `uninstall`, `status` or `mcp`. It has no `stop` or `plugins` command (its window has a Stop button) and shows the older ImGui window when double-clicked.
- It uses the same ini, `data` folder and PC-change record, so you can switch between the two launchers at any time.
- **Undo PC changes with it:** `sc-offline-classic.exe uninstall` takes the mod out of the game folder and undoes every change in `%ProgramData%\sc-offline\pc-changes.txt`. It refuses while the game runs. Then `sc-offline-classic.exe status` must show nothing left over. If neither launcher runs, [PC changes](#pc-changes) lists each change so you can undo it by hand.
- It always installs `dinput8.dll`, the mod with the classic menu. With `menu = rust` you still get the classic menu, and the mod writes one `mod.log` line saying the new menu needs `sc-offline.exe`.
- Do not update with it. Run `sc-offline.exe update`; the classic launcher's own update would swap the Rust `sc-offline.exe` back out under its name.

### Updating from 0.7.x

Your `sc-offline.ini` and the player files in `data\` (`wallet.txt`, `spawn.txt`, `bookmarks.txt`, `locations_found.txt`, `game-path.txt`, `game-build.txt`, the logs and `data\storage\`) are kept. The 0.7.x updater replaces `sc-offline.exe` with the Rust launcher like any other file in the release, runs `sc-offline.exe --self-test` from the new files, and starts the new launcher. If the self-test fails, the old files are put back. Settings that are new in the release's ini (the `exp_*` switches, `menu`) are appended to yours, commented out, so their defaults apply. Afterwards `sc-offline-classic.exe`, `sc_offline_menu.dll`, `dinput8-rust-menu.dll` and the new plugin folders are in place.

## Checks it runs every time

1. **Which DLL.** The SHA-256 of the `dinput8.dll` next to the exe, and what it is: an sc-offline build (with its version), an older ChrisWareOffline build, or unknown. With the original author's prebuilt DLL it warns unless `boot_map = PU`.
2. **Game updated?** The game version from `<channel>\build_manifest.id` (or the size and date of `StarCitizen.exe`), compared with `data\game-build.txt` from your last play. A change means a game update may have broken the mod.
3. **Leftovers.** The mod is still in the game folder while the game is not running (after a crash, or `install`). It tells you to run `uninstall`.
4. **PC changes left over.** A previous run's changes are still recorded in `pc-changes.txt`. `play` asks whether to undo them and stop, or to play and undo everything afterwards.
5. **Easy Anti-Cheat.** Whether `C:\Program Files (x86)\EasyAntiCheat_EOS\EasyAntiCheat_EOS.exe` exists (active), exists only as `.exe.bak` (disabled) or is absent, and whether your hosts file blocks `modules-cdn.eac-prod.on.epicgames.com`. If EAC is active and `eac_rename = off`, `play` and `install` stop and print the fix.

## What `play` does

1. **Finds the game's `Bin64` folder.** It takes the first of these that has `<channel>\Bin64\StarCitizen.exe`:
   1. `--game`, then `game =` in the ini;
   2. the folder it found last time (`data\game-path.txt`);
   3. where the RSI Launcher says the game is: its Windows install entry, and paths in its settings and logs under `%APPDATA%\rsilauncher`;
   4. the usual folders on every fixed drive (`Program Files\Roberts Space Industries`, `Games\...`, `Game\Star Citizen\...` and similar);
   5. a search of every fixed drive, four folders deep (a few seconds; skips system folders);
   6. if you double-clicked, a folder picker.

   With more than one install it uses the first and lists the others. It remembers its find in `data\game-path.txt`; `game =` and `--game` always win over that file.
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

The package holds two game DLLs built from the same sources:

- `dinput8.dll` is built with the CMake option `SCO_HOST_MENU_CLIENT=OFF`, the configuration that has been run in game. It has the classic menu.
- `dinput8-rust-menu.dll` is built with `SCO_HOST_MENU_CLIENT=ON` and has the hook for the new menu (`sc_offline_menu.dll`). It has never run in game.

`sc-offline.exe` installs `dinput8-rust-menu.dll`, under the name `dinput8.dll`, only when the ini says `menu = rust` and that file is beside the launcher. In every other case it installs `dinput8.dll`. `sc-offline-classic.exe` always installs `dinput8.dll`. Change the `menu` key and the next Play replaces the installed DLL when its bytes differ from the chosen source, as it does for a new version.

## PC changes

While you play, the helper makes three changes outside the game folder and undoes them when every `StarCitizen.exe` has exited. Each is an ini switch, on by default.

| Key | Going in | Coming out |
| --- | --- | --- |
| `block_network` | Windows Firewall rules, inbound and outbound: `sc-offline: block StarCitizen.exe` (this install's exe only), `sc-offline: block RSI Launcher.exe` (found from its install entry, beside the game library, or the default folder) and `sc-offline: block CrashHandler.exe` (`<channel>\Tools\Public\CrashHandler.exe`, CIG's crash reporter). A rule is added only if its exe exists. `sc-offline.exe` itself stays online for updates. With `multiplayer = on` (the default), the `StarCitizen.exe` rule blocks every address except loopback, the private ranges (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`), link-local (`169.254.0.0/16`) and `multiplayer_allow`; all IPv6 stays blocked. One more rule, `sc-offline: allow StarCitizen.exe multiplayer (LAN)`, lets PCs on your local subnet (and `multiplayer_allow`) reach the game over UDP. If Windows refuses the narrower rule, the launcher blocks every address and says so. | Deleted. |
| `eac_hosts` | `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com # added by sc-offline...` is appended to the hosts file, then `ipconfig /flushdns`. Skipped if the hosts file already blocks it. | Only the tagged line is removed; DNS flushed. |
| `eac_rename` | `EasyAntiCheat_EOS.exe` is renamed to `EasyAntiCheat_EOS.exe.bak`. Skipped if it is not there. | Renamed back. |

- Each change is written to `%ProgramData%\sc-offline\pc-changes.txt` the moment it is made, and only recorded changes are undone. A hosts line or `.bak` you made yourself is never touched.
- If a step fails, the helper undoes what it already did and the game does not start.
- After a crash the record stays. `status` lists it, `uninstall` undoes it (**Repair** in the window), and `play` offers to.
- The firewall rules cut the game's network for the session. They hide nothing already on disk, such as logs or the renamed EAC file.

### Administrator rights

The launcher never asks to run as administrator, and the game never runs as administrator. Windows asks once, only for a step that needs it:

- the helper, when the firewall, EAC or a protected game folder is involved;
- deleting session logs that a protected game folder will not let you delete (the elevated run works out this session's files again by itself);
- updating a mod folder only administrators can write, such as one under Program Files (the elevated run asks GitHub again and applies only the version you were shown, then the new launcher starts with normal rights).

If the mod folder's `data` cannot be written, the mod cannot save your wallet or places, and the launcher tells you to move the folder (for example to your Desktop).

Start `sc-offline.exe` with normal rights. An elevated launcher tries to use the desktop token for the game and warns if it falls back. To launch the game with normal rights, remove *Run this program as an administrator* from the `StarCitizen.exe` properties. On NVIDIA GPUs, turn off **Smooth Motion** for Star Citizen in the NVIDIA App first; with it on the game can crash during startup. Crash diagnostics name the fault module and offset and raw-stack executable-address candidates, not an unwound call chain.

## Updates

On `play` the launcher asks GitHub for the newest release of `scuba-sc/sc-offline` (3-second timeout; no network just skips it). With `update_channel = stable` (the default) only full releases are offered; `prerelease` also offers test builds. Drafts, older versions and tags that are not plain numbers are never offered. If a newer one exists, `play` asks **"Update now?"**. `status` makes no network request. Run `sc-offline.exe update` to check on demand. Nothing updates while `StarCitizen.exe` runs.

An update:

1. checks free disk space, then downloads `sc-offline-<tag>.zip`. The download times out only when it stalls, and is tried once more. The zip's SHA-256 must match the `digest` GitHub publishes for that asset. A mismatch or a missing digest stops it with nothing changed.
2. unpacks it with Windows' own `tar.exe` and checks it against its `manifest.json` ([below](#what-an-update-is-checked-against)). Any problem stops it with nothing changed.
3. stages the listed files in `data\update\staged\` with normal rights. When the install folder is not writable (Program Files), it stages in `%LOCALAPPDATA%\sc-offline\update\` instead, and only the next step asks Windows for administrator rights. That step uses no network and checks every staged file against the manifest again.
4. replaces every listed file except `sc-offline.ini` and the player files in `data\` (`wallet.txt`, `spawn.txt`, `bookmarks.txt`, `locations_found.txt`, `game-path.txt`, `game-build.txt`, the logs). `data\storage\` (your saves) is never in a release. Each file is written as `<name>.update-new` and flushed, the old one is renamed to `<name>.update-old`, and the new one is moved into place and checked again. Each step is written to `data\update\applied.txt` first. A file that antivirus is scanning is retried for a few seconds.
5. appends settings that are new in the release's ini to yours, commented out, so their defaults apply.
6. runs `sc-offline.exe --self-test` from the new files, then starts the new launcher with the same arguments.

If a step fails, or the new launcher fails its self-test, the old files are put back at once. If the PC dies mid-update, the next run of `sc-offline.exe` reads `applied.txt` and puts them back. The `.update-old` files are deleted on the next good start. `check_updates = off` turns the automatic check off.

### What an update is checked against

Two things vouch for an update. Neither is a code signature; releases are not code-signed.

1. **GitHub's digest.** GitHub publishes a SHA-256 `digest` for every release asset, served over HTTPS from `api.github.com`. The downloaded zip must match it.
2. **`manifest.json` inside the zip.** CI writes it when it builds the release: the `version`, the `tag`, the `commit` it was built from, and every other file in the zip with its `sha256` and `size`. Its version must equal the release tag. Only files it lists are copied, and only when their SHA-256 matches. A listed path with `..`, a leading `/`, a `\` or a `:` fails the whole update.

So the update is exactly what CI built from that commit, as long as the GitHub release itself is trustworthy. Anyone who can publish a release on `scuba-sc/sc-offline` can publish an update.

### Checking a release by hand

On Windows (PowerShell), in the folder you downloaded the zip to:

```powershell
# 1. The zip against GitHub's digest (shown on the release page next to the asset, or:)
#    gh release view v0.8.0 -R scuba-sc/sc-offline --json assets --jq '.assets[] | [.name, .digest]'
(Get-FileHash .\sc-offline-v0.8.0.zip -Algorithm SHA256).Hash.ToLower()

# 2. Every unpacked file against manifest.json
Expand-Archive .\sc-offline-v0.8.0.zip -DestinationPath .
$dir = '.\sc-offline-v0.8.0'
$man = Get-Content "$dir\manifest.json" -Raw | ConvertFrom-Json
"$($man.tag) from commit $($man.commit), $($man.files.Count) files"
foreach ($f in $man.files) {
  $p = Join-Path $dir $f.path
  if (-not (Test-Path $p)) { "MISSING  $($f.path)"; continue }
  if ((Get-FileHash $p -Algorithm SHA256).Hash.ToLower() -ne $f.sha256) { "CHANGED  $($f.path)" }
}
```

No output after the summary line means every listed file matches. A file in the folder that the manifest does not list (other than `manifest.json`) is not part of the release. On any OS with Python, `python3 tools/release-manifest.py check sc-offline-v0.8.0` from a clone of this repository does step 2, also flags unlisted files, and exits non-zero on any problem. The `commit` field points to the exact source at `https://github.com/scuba-sc/sc-offline/tree/<commit>`.

## Discord status

The Discord plugin (`data\plugins\discord`, source `plugins/discord/`) is **off**, pending an owner decision on its transport and the fields it would send. The folder ships with a `disabled` marker. If you enable it by hand, only local diagnostics load: `discord.status` returns "unavailable" and the reason. It reads no game location, ship, host version, process ID or session time, opens no pipe or network connection and sends nothing. Its three bool preferences are stored but inactive and default to false.

The old implementation sent activity over Discord's local named pipe, outside the SDK's host-mediated transport. Discord may publish presence through its own service, so local delivery did not mean the information stayed local. SDK 2.4.0's `sco.ipc` and `sco.net` offer no approved transport for it. See the [transport and privacy review](../plugins/discord/README.md).

The switch on the Plugins page creates or removes `data\plugins\discord\disabled` and applies at the next **Play**. Removing the marker enables diagnostics only, not Rich Presence. A legacy `discord_presence = off` ini line is still migrated.

## Crash reports

If the game wrote crash files under `<channel>\Crashes` during the session, the launcher offers a report before the log cleanup. **y** zips `mod.log`, `launcher.log`, `Game.log` and the crash folder's text files into `data\crash-reports\sc-offline-crash-<time>.zip`. Only the copies are changed, never the originals:

- your RSI handle, nickname, character name, GEID and account numbers become `<redacted>` wherever they appear (the launcher learns them from every bundled file first);
- e-mail addresses become `<redacted>`;
- your Windows user name in paths and text becomes `<user>`.

Free text can still hold a name no log line announced, so read the zip before you share it. The report is stricter than the MCP `read_log`: a handle from a login line is hidden inside other words too, even when it is short or an ordinary word, and every `Handle[..]` and `Nickname[..]` value in any bundled file is hidden everywhere. Memory dumps (`.dmp`) are left out because they cannot be redacted. The launcher can then open the bug form with the version, game build and platform filled in, and an Explorer window on the zip. **Nothing is uploaded**: you attach the zip. `crash_reports = off` turns this off.

## Session logs

When the game closes, the launcher lists the logs the game wrote this session: `<channel>\Game.log`, new files in `<channel>\logbackups` and new files under `<channel>\Crashes`. It asks **"Delete these files?"**:

| Answer | Window button | Effect |
| --- | --- | --- |
| `y` | **Delete** | Deletes them after one more **"Are you sure?"**. |
| `n` or Enter | **Keep** | Keeps them. |
| `always` | **Always delete** | Deletes now and after every session without asking. |
| `never` | **Never delete** | Keeps them and never asks again. |

Anything unclear keeps the files. **Always** and **Never** write `clean_logs = always` or `clean_logs = never` to the ini and leave every other line and comment alone. Only this session's files are deleted, through the same administrator prompt when the game folder refuses a plain delete. Older files (from online play, say) are never touched, and neither are `data\mod.log` and `data\launcher.log`. Keep `Game.log` if you want to report a bug. Set `clean_logs = never` (or use the Settings page) to skip the question; `ask` brings it back.

Under Wine none of the PC changes run; `sc-offline.sh` handles the hosts file there, and the firewall rules do not apply ([Linux](linux.md)).

## `sc-offline.ini`

Each line is `key = value`. A line that starts with `#` is a comment. The launcher reports an unknown key at start. The ini has no wallet setting: your aUEC balance is in `data\wallet.txt` ([Features](features.md#player)).

`start` and `start_ship` are still read and passed to the host, but the current [spawn plugin](../plugins/spawn/README.md) refuses destination startup. `start = Daymar` does not bring back the old timed arrival or boarding.

| Key | Default | Meaning |
| --- | --- | --- |
| `game` | found automatically | Your Star Citizen folder: `Roberts Space Industries`, `StarCitizen`, the channel folder, or the folder that holds `StarCitizen`. |
| `channel` | `LIVE` | Which install to use when `game` points above it: `LIVE`, `PTU`, `EPTU` and so on. |
| `boot_map` | `PU_All` | `PU_All` loads every star system. Older ChrisWareOffline DLLs understand only `PU`. |
| `start_ship` | `DRAK_Cutlass_Black` | Legacy startup ship request. It does not choose an ordinary menu spawn or enable boarding. |
| `start` | empty | Legacy destination request. Empty leaves the game's own spawn. |
| `plugins` | `on` | Load plugins from `data\plugins\<id>\` ([Plugins](features.md#plugins)). |
| `relocation` | `on` | Host policy for movement through sco-core's `game.relocation`: F7/F8 teleport, Travel, saved spots. `off` disables movement for every plugin. Movement also needs a confirmed offline session, a live player and a known star system, and each plugin has its own setting. |
| `menu` | `imgui` | The in-game menu. `imgui` is the classic menu. `rust` is the new menu and works only with `sc-offline.exe` and `dinput8-rust-menu.dll` beside it ([Which game DLL](#which-game-dll)). It opens on **M**. The launcher passes the choice to the game as `SC_OFFLINE_MENU`. The Settings page writes this key. |
| `asop` | `on` | `off` leaves the ship terminals, personal hangars, the hangar lift and ATC hails as the game has them offline ([Features](features.md#ship-terminals-hangars-and-atc)). |
| `asop_fleet_list` | `ships` | The ship list the terminals show with `asop = on`: `ships` (every ship in `data\ships.txt`) or `game` (the game's own; offline its query fails, so the list is empty). |
| `multiplayer` | `on` | The [Multiplayer tab](features.md#multiplayer-lan-or-vpn). Nothing connects until you press Host or Join. With `block_network` on, the firewall leaves your LAN open for it. `off` removes the tab, and the firewall blocks every address. |
| `multiplayer_allow` | empty | Firewall exceptions besides your LAN, for a VPN: up to 8 IPv4 ranges separated by commas (Tailscale: `100.64.0.0/10`). Each whole range must lie within `10/8`, `127/8`, `169.254/16`, `172.16/12`, `192.168/16` or `100.64/10`; allow only your VPN's range. These exceptions do not authorize a session: the host's own bind and peer policy still applies. The launcher clears values that do not parse and warns. |
| `block_network` | `on` | Block `StarCitizen.exe`, the RSI Launcher and `CrashHandler.exe` in Windows Firewall while you play ([PC changes](#pc-changes)). |
| `eac_hosts` | `on` | Add the EAC hosts line while you play. |
| `eac_rename` | `on` | Rename `EasyAntiCheat_EOS.exe` while you play. |
| `clean_logs` | `ask` | After the game closes: `ask` lists this session's logs and asks before deleting; `always` deletes without asking; `never` keeps them. The older `on` (= `ask`) and `off` (= `never`) still work. |
| `crash_reports` | `on` | After a crash, offer a redacted log bundle and the bug form ([Crash reports](#crash-reports)). |
| `check_updates` | `on` | Check GitHub on `play` and offer to install a newer release. |
| `update_channel` | `stable` | `stable` offers full releases only; `prerelease` offers test builds too. |
| `language` | `auto` | Launcher language: `auto`, `en` or `ru` ([Localization](localization.md)). |
| `registry_probe` | off | Test only. `1` writes `data/registry-dump.txt` 15 s after you spawn: one `name<TAB>vehicleSize` line per entity class the probe walked and the game's own name lookup resolved back to that class. It stops at the first fault and writes nothing, so a partial list never looks complete. |
| `mining`, `mining_debug` | ignored | Legacy keys. The mining plugin has its own settings, off by default; the launcher warns and imports nothing ([migration](../plugins/mining/README.md)). |

### Capability switches in `sc-offline.ini`

sco-core starts some capabilities only when a variable is `1` inside the game process. The launcher sets those variables from the `exp_` keys, so you never edit the environment by hand.

| Key | Default | Variable | What it asks for |
| --- | --- | --- | --- |
| `exp_loadout_async` | `on` | `SCO_LOADOUT_ASYNC` | The menu equips gear (`game.loadout.async`). |
| `exp_inventory_read` | `on` | `SCO_INVENTORY_READ` | Plugins read what you wear and carry (`game.inventory.read`). |
| `exp_actor_stats_write` | `on` | `SCO_ACTOR_STATS_WRITE` | Plugins change your survival stats (`game.actor_stats.write`). |
| `exp_cvars_native` | `on` | `SCO_CVARS_NATIVE` | Plugins read four console settings, read only (`game.cvars.read`). |
| `exp_audio_native` | `on` | `SCO_AUDIO_NATIVE` | Native one-shot sounds. This sco-core refuses them and logs why. |
| `exp_entities_diag` | `off` | `SCO_ENTITIES_DIAG` | Log-only entity hooks and one walk written to `mod.log`. Diagnostics for bug reports, no gameplay change. |

- **A switch asks, Core decides.** On means the launcher asks sco-core to start the capability. It does not mean the capability works. [Status](status.md) says which have been seen in game. The full table is [capabilities.md](capabilities.md).
- **Off means unset.** When a key is off, the launcher removes its variable from the game's environment and prints `[i] removed ... from the game's environment` if one was set. A variable left in your shell or in Windows cannot turn a capability on behind your back.
- **One line at start.** The launcher prints `Capability switches on:` with each key that is on and the variable it sets.
- **Values.** `on` or `off` (also `yes`/`no`, `1`/`0`, `true`/`false`). Anything else keeps the default and is reported.
- **Editing.** Use the Capabilities page or **Settings > Edit sc-offline.ini**. When an update adds a key to an existing ini, it is appended commented out with its explanation above it, and the default applies.
