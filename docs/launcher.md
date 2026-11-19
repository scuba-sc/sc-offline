# The launcher: `sc-offline.exe`

The launcher starts the game with the mod and takes the mod out again when the game closes. From 0.8.0 `sc-offline.exe` is the Rust launcher (`launcher-rs/`, crate `scol-cli`). The C++ launcher it replaces ships beside it as `sc-offline-classic.exe`, the [fallback](#the-classic-launcher). Both read the same `sc-offline.ini`, the same `data` folder and the same `%ProgramData%\sc-offline\pc-changes.txt` record.

Not run in game: the Rust launcher's `play` path and its elevated helper. They match the C++ launcher in synthetic scenarios (status and dry-run text, exit codes, file trees) and run against a stand-in game process. See [Status](status.md#080-what-was-and-was-not-run-in-game).

## Commands

```text
sc-offline.exe [play|install|uninstall|status|mcp|update|stop|plugins|help] [--game <folder>] [--dry-run] [--skip-eac-check] [--game-log] [--end]
```

| Command | What it does |
| --- | --- |
| `play` | The default from a terminal (a double-click opens the [window](#the-window)). Copies the mod in, starts the game, takes the mod out when it closes. |
| `install` | Copies the mod in and leaves it there, for starting the game some other way. Run `uninstall` before going online. Command line only: the [window](#the-window) has no Install button. |
| `uninstall` | Takes the mod out, puts back anything it replaced, and undoes leftover [PC changes](#pc-changes). Refuses while the game is running. The window's **Repair: undo PC changes** runs it. |
| `status` | Runs the [checks below](#checks-it-runs-every-time) and is read-only: it writes no file (not `data\launcher.log`, not `data\game-path.txt`), doesn't migrate `discord_presence`, doesn't settle an interrupted update, doesn't open the folder picker and makes no network request (no update check). If the game folder can't be found it says so; `play` still remembers what it finds. The window's Status panel runs the same checks. |
| `mcp` | Runs a read-only [MCP server for AI agents](ai-agents.md) on stdin/stdout: status (the same checks as `status`), installed plugins, log tails and `sc-offline.ini`. Like `status` it writes nothing and makes no network request; it runs no command and changes no setting. The game's `Game.log` is served only with `mcp --game-log`; without the flag the file is not opened (it can hold account names). |
| `update` | Checks GitHub for a newer sc-offline and offers to install it; see [Updates](#updates). `update --check` only checks, `update --yes` does not ask, `update --prerelease` also offers pre-releases. |
| `stop` | Asks the running game of this install to close, the same Stop as the [window](#the-window): the mod runs its exit work first (up to 10 seconds), then the game's window is told to close, and `stop` waits up to 60 seconds for the game to be gone. Exit `0` when it closed (or nothing was running), `1` when the game folder is unknown or the game is still running. If it is still running after a minute: in a terminal `stop` asks "End the game process?"; with no terminal it only says so. `stop --end` ends that process without asking (only the one it asked to close, checked by its creation time). The `play` that started the game takes the mod out and undoes the PC changes when the game exits. |
| `plugins` | Manages `data\plugins` from the command line; see [Plugin commands](#plugin-commands). |
| `help` | Prints the usage. |

| Option | Meaning |
| --- | --- |
| `--game <folder>` | Your `Roberts Space Industries`, `StarCitizen`, channel or `Bin64` folder. Overrides `game =` in `sc-offline.ini`. |
| `--dry-run` | Prints every step it would take, then stops. Nothing is copied, deleted or started. |
| `--skip-eac-check` | Don't stop when Easy Anti-Cheat looks active. |
| `--window` | Open the window from a terminal, or under Wine/Proton. |
| `--console` | Double-clicked: run `play` in the console instead of opening the window. |
| `--end` | With `stop` only: end the game process if it is still running a minute after it was asked to close. Any other command refuses it. |
| `--game-log` | With `mcp` only: let the MCP server's `read_log` serve the game's `Game.log`, redacted like the other logs. Off by default. Any other command refuses it. |

Exit codes: `0` ok · `1` error · `2` Easy Anti-Cheat is active · `3` the game is running. The helper's own codes, in `launcher.log`: `5` a PC change failed.

Everything the launcher prints also goes to `data\launcher.log` (a new file each run: the previous one is kept as `launcher.log.1`, replacing any older `.1`, so the last session's Stop and End lines survive one more run; the helper appends to it), except for `status`, which writes nothing.

### Plugin commands

`sc-offline.exe plugins` works on `data\plugins` the way the host reads it. Changes are journaled, and each refuses while the game is running. New and changed plugins apply at the next game start.

```text
plugins [--root <launcher folder>] list [--json]
plugins enable <id>
plugins disable <id>
plugins install <folder|zip> [--replace]
plugins remove <id> [--purge]
plugins settings <id> [name=value ...] [--reset]
plugins catalogue list --source <folder>
plugins catalogue install <id> --source <folder> [--replace] [--allow-downgrade]
plugins catalogue update [<id>] --source <folder> [--allow-downgrade]
plugins update [<id>] --source <folder> [--allow-downgrade]
```

- `list` shows each folder with its `plugin.ini` fields and whether it is on. `--json` prints the same as JSON.
- `enable` and `disable` create or delete the empty `disabled` file in the plugin's folder, as the window's switch does.
- `install` takes a folder or a `.zip` that holds a `plugin.ini`. Everything you install lands **disabled**; turn it on with `enable`. A native plugin (a DLL) runs its own code inside the game, so install only plugins you trust. `--replace` replaces a plugin with the same id; `remove` deletes one (`--purge` also deletes its saved settings in `data\storage`).
- `settings` shows a plugin's `[settings]` with their kept values; with `name=value` words it saves them to the plugin's store (`data\storage\<id>.db`, read at the next game start); `--reset` deletes the kept values. It refuses while the game runs, and a value that does not fit its declaration writes nothing.
- The `catalogue` commands read a **signed index in a local folder** (`--source`). No production signing key ships with 0.8.0, so the built-in trust root is empty: an index is accepted only when it is signed by a key the launcher was built with, and there is no network catalogue yet: without `--source` the commands say "no catalogue is published for this build yet". The window's Catalogue tab is the same code. Adding a key is a release-time step, not something a player does. First-party and reviewed plugins, native ones included, are the only entries the catalogue is meant to carry, and each lands disabled with a native-code warning. `--allow-downgrade` lets an older version replace a newer one.

## The window

A double-click (no arguments, the exe's own console) opens the window instead of the console run. It is drawn with egui (Rust). If it cannot open, run `sc-offline.exe play` from a terminal. Under Wine the console run stays the default; `--window` opens the window anyway. The window speaks English and Russian (**Language** on the Settings page; Auto follows the Windows UI language).

The sidebar has five pages:

- **Home**: the *Status* panel on the left and the *Actions* on the right. **Play** and **Update** run `sc-offline.exe <command>` as a hidden child with `SC_OFFLINE_GUI=1`, so the window and the CLI share one code path. The child's `[y/N]` questions turn the buttons into **Yes** / **No** (or press Y / N); the session-log question has four answers, **Delete**, **Keep**, **Always delete** and **Never delete** (see [Session logs](#session-logs)). **Play** and **Update** are disabled while a command runs and while the game runs.
  - **Stop**: while the game runs, **Play** becomes **Stop**. First it asks the mod to run its exit work: if the mod is loaded in the game process it has made a per-process event, `Local\sc-offline-stop-<pid>` (session-local, default security descriptor, so only the same user can open it). Stop signals it and waits up to 10 seconds for `Local\sc-offline-stopped-<pid>`; on the game thread the mod then runs what a normal quit runs (the wallet is saved, the private session closed, `game.exit` sent to plugins, admission closed) once, logs `[app] stop requested by the launcher: exit work done` to `data\mod.log`, and answers. It unloads nothing and does not quit the game itself. **After Stop the mod stays closed until the game exits**: its menu, hotkeys and timers (the 5 second wallet poll among them) stop, and nothing is saved again. If the game does not close and **End** is declined, the game keeps running without the mod. Closing a game window with WM_CLOSE alone skips that exit work (the wallet was not saved). While the launcher waits for the mod's answer the button shows **Saving…** and the window stays responsive. Without the event (the mod is not loaded, or the game is online) Stop goes on straight to the next step; it does too when the mod has already closed itself (the game was quitting), when the game process exits, or when the mod has not answered after 10 seconds. If the mod is installed in the game folder but did not answer (no event, an event that can't be opened, or a timeout), the window says "The mod didn't answer; closing without its save." and logs it. Then Stop posts WM_CLOSE to the visible top-level window(s) of this install's `StarCitizen.exe`: the game folder is resolved as Play does (`game =`, else the folder the panel found), and Stop refuses when it isn't known; it never touches another install (PTU, another folder). The play command that started the game keeps waiting for it, so the usual cleanup runs when the game exits: the mod out, the PC changes undone. Nothing terminates the game automatically. If it is still running 60 seconds after Stop, the Actions card asks **"End the game process?"**; only **Yes** ends that process (the one that was asked to close, checked by its creation time), **No** leaves it alone and **Stop** can be pressed again. Each step is written to `data\launcher.log` with a time.
  - **Repair banner**: when `%ProgramData%\sc-offline\pc-changes.txt` holds a record and the game is not running (after a crash; not while a command such as Play runs), a banner across the top of the page lists what is left and has the **Repair: undo PC changes** button, which runs `uninstall`.
  - **Status panel**: the same read-only checks as `sc-offline.exe status`: the game folder and build, whether the mod is installed, PC changes left over, whether the network is blocked while you play, Easy Anti-Cheat, how many plugins are enabled and the SDK version. It is checked when the window opens, after the game exits, whenever the game starts or stops or a command starts or ends, every 5 seconds while the game or a command is running, and when you press the refresh icon, on a background thread. While you play, the Mod row reads "Installed (this session)" and PC changes "Applied for this session; undone when the game closes". "The game is running" means a `StarCitizen.exe` of this install's folder, the same rule Stop uses. Nothing is written.
  - The status line above the panel's rows says **Safe to go online** (nothing of the mod is in Bin64 and no PC changes are left), **Not safe to go online** / **yet** (the game is running, or it lists what is left) or **Game folder not known yet**.
- **Output**: what the running command prints, with a **Copy** button. **Update** and **Repair** switch to it.
- **Plugins**: see [below](#the-plugins-page).
- **Capabilities**: see [Game capability switches](#game-capability-switches).
- **Settings**: **Repair: undo PC changes** (enabled whenever no command runs and the game is closed; it says "Nothing to repair" when there is nothing); **Game logs after a session** (**Ask each time**, **Always delete**, **Never delete**: `clean_logs` in `sc-offline.ini`); **Language**; **In-game menu** (**Classic**, the ImGui menu, or **New (not tested in game yet)**, the Rust menu: the `menu` key; it applies the next time the game starts); **Plugins** on or off (`plugins`); **Check for updates** (`check_updates`); and **Edit sc-offline.ini** and **Open folder**. Settings are read-only while the game or a command runs.

The window has a folder picker (Browse, and when no game is found) and starts the new window after an update; neither has been run in game.

### The Plugins page

The page manages `data\plugins\<id>\` the way the host (sco-core) reads it, with no separate list of its own:

- **What is listed**: every folder under `data\plugins` that holds a `plugin.ini` (a folder without one is skipped, as the host does), with its `name`, `version` and `author`; `description`, when a plugin adds one, is the tooltip. A native plugin shows a native-code warning. Folders are rescanned every 3 seconds.
- **On / off**: the switch creates or deletes an empty file named `disabled` in the plugin's folder. The host reads that marker when the game starts, so a change applies at the next start.
- **Add a plugin**: pick a folder or a `.zip`. It lands disabled, like `plugins install`.
- **Catalogue** tab: the plugins of a signed index. See [Plugin commands](#plugin-commands): no production key ships, so with 0.8.0 it works from a local folder and refuses an index with no trusted key.
- **The creative plugin** (`creative`: noclip, god mode, infinite ammo) ships **on**. Switch it off here if you do not want it.
- **Plugins are off**: the host loads no plugin unless `plugins = on` in `sc-offline.ini` (the default is on). When it is off, the page says so and **Turn on** writes the key.
- **Settings** (the gear, for a plugin whose `plugin.ini` has a `[settings]` section): edits the values the plugin declared (`bool`, `int`, `float`, `string`, `enum(...)`), checked against the declared range and choices with the host's rules. **Save** writes them where `sco.settings` keeps them: the plugin's own database `data\storage\<id>.db`, table `sco_kv`, key `sco.settings.<name>`, value `<type>:<value>`. **Reset** deletes the kept values, so the defaults apply. The fields are read-only while the game or a command is running: the host reads its settings once, at start. Close the game, edit, start it again.
- **Reload**: only shows a note. Hot reload runs inside the game, through its `sco.reload` command; the launcher has no channel into the running game to call it.

#### Last startup snapshot

The Plugins page can show a **Last startup snapshot (may be stale)** from the optional `data\plugin-startup.json` file. It never tails `data\mod.log`, contacts the host, or reports live state. The current product has no writer for this file, so it normally shows **No completed startup snapshot**. The current `plugins` value in `sc-offline.ini` and each folder's `disabled` marker remain the desired settings. The v1 file format and its limits (at most 64 KiB and 256 rows, printable ASCII, fixed state and reason-code names, unknown fields rejected) are specified by the reader in `launcher-rs/crates/scol-plugins`; no host writer exists yet.

### Game capability switches

The **Capabilities** page lists the `exp_*` keys of `sc-offline.ini` as switches. Each card has a short name, one sentence, an On/Off toggle and an evidence line (for example "Not run in game yet" or "Seen working in game once"); the ini key and the Core capability name are in its details. A toggle writes its key (the game reads it at the next Play; the page is read-only while the game or a command runs). Capabilities that have no switch are read-only rows in two groups, "Works by itself, no switch" and "Not available yet". The same keys are described [below](#game-capability-switches-in-sc-offlineini).

### The classic launcher

`sc-offline-classic.exe` is the C++ launcher of 0.7.x, unchanged in behaviour, shipped as the fallback. Use it if the Rust launcher does not start, the window does not open, or a step fails on your PC.

- Run it from the same folder, with the same commands: `sc-offline-classic.exe play`, `install`, `uninstall`, `status`, `mcp`. It has no `stop` or `plugins` command (its window has a Stop button) and shows the older ImGui window when double-clicked.
- It uses the same `sc-offline.ini`, `data` folder and PC-change record, so you can switch between the two at any time.
- **Undo PC changes with it:** `sc-offline-classic.exe uninstall` takes the mod out of the game folder and undoes every change recorded in `%ProgramData%\sc-offline\pc-changes.txt` (the firewall rules, the hosts line, the EAC rename). It refuses while the game runs. Then `sc-offline-classic.exe status` must show nothing left over. If you cannot run either launcher, [PC changes](#pc-changes) lists each change so it can be undone by hand.
- Do not update with it: run `sc-offline.exe update`. The classic launcher's own update would swap the Rust `sc-offline.exe` back in under its name.

### Updating from 0.7.x

Your `sc-offline.ini` and the player files in `data\` (`wallet.txt`, `spawn.txt`, `bookmarks.txt`, `locations_found.txt`, `game-path.txt`, `game-build.txt`, the logs and `data\storage\`) are kept. The 0.7.x updater replaces `sc-offline.exe` with the Rust launcher like any other file in the release, runs `sc-offline.exe --self-test` from the new files (the Rust launcher answers it the same way), and starts the new launcher. If the new launcher fails the self-test, the old files are put back. New settings in the release's `sc-offline.ini` (the `exp_*` switches, `menu`) are appended to yours, commented out, so their defaults apply. After the swap, `sc-offline-classic.exe`, `sc_offline_menu.dll` and the new plugin folders are in place. Nothing else changes for you: same folder, same ini, same data.

## Checks it runs every time

1. **Which DLL.** The SHA-256 of the `dinput8.dll` next to the exe, and what it is: an sc-offline build
   (with its version), an older ChrisWareOffline build, or unknown. With the original author's prebuilt
   DLL it warns unless `boot_map = PU`.
2. **Game updated?** The game version from `<channel>\build_manifest.id` (or `StarCitizen.exe`'s size and date),
   compared with `data\game-build.txt` from your last play. If it changed, a game update may have broken the mod.
3. **Leftovers.** If the mod is still in the game folder while the game isn't running (a crash, or `install`), it
   says so and tells you to run `uninstall` (in the window, **Repair**).
4. **PC changes left over.** If a previous run's changes (below) are still recorded in
   `%ProgramData%\sc-offline\pc-changes.txt`, it lists them. `play` asks whether to undo them and stop,
   or play and undo everything afterwards.
5. **Easy Anti-Cheat.** Whether `C:\Program Files (x86)\EasyAntiCheat_EOS\EasyAntiCheat_EOS.exe` exists (active),
   only as `.exe.bak` (disabled), or not at all, and whether your hosts file blocks
   `modules-cdn.eac-prod.on.epicgames.com`. If EAC is active and `eac_rename = off`, `play` and `install`
   stop and print the fix.

## What `play` does

1. **Finds the game's `Bin64` folder.** It tries these in order and takes the first that has
   `<channel>\Bin64\StarCitizen.exe`:
   1. `--game`, then `game =` in `sc-offline.ini`;
   2. the folder it found last time (`data\game-path.txt`);
   3. where the RSI Launcher says the game is: its Windows install entry, and paths in its settings and
      logs under `%APPDATA%\rsilauncher`;
   4. the usual folders on every fixed drive (`Program Files\Roberts Space Industries`, `Games\…`,
      `Game\Star Citizen\…` and similar);
   5. a search of every fixed drive, four folders deep (a few seconds; skips system folders);
   6. if you double-clicked it, a folder picker.

   If it finds more than one install, it uses the first and lists the others. It remembers what it found in
   `data\game-path.txt`; `game =` and `--game` always win over that file.
2. **Runs the checks** above.
3. **Sets the environment variables** the mod reads (see [data-files.md](data-files.md#environment-variables)).
4. **Starts a helper**, a second copy of `sc-offline.exe` with no window, that changes the game folder:

   | Path | Going in | Coming out |
   | --- | --- | --- |
   | `Bin64\dinput8.dll` | the mod's DLL. If another mod's `dinput8.dll` is there, it is first renamed `dinput8.dll.sc-offline-backup` | deleted; the other mod's DLL renamed back |
   | `<channel>\user\client\0\default_1.xml` | the starting loadout from `data\OfflineDB`. Yours is first copied to `default_1.xml.sc-offline-backup` | your copy restored, or ours deleted if you had none |
   | `Bin64\sc-offline.installed` | a note of what was installed and when | deleted |

   Before copying the mod in, `play`'s helper makes the [PC changes](#pc-changes). It runs as administrator when
   any PC change is on (the default) or the game folder needs it, and then Windows asks once. The game itself
   never runs as administrator.
5. **Starts `StarCitizen.exe`** and waits until every `StarCitizen.exe` has exited. The helper then takes the
   mod out, even if you closed the launcher window first. Ctrl+C in the launcher window is ignored; close the
   game instead.

If the PC crashes mid-game, the mod stays in the game folder. The next `play` or `status` notices, and the window
shows its Repair banner; use **Repair** or run `sc-offline.exe uninstall` before going online.

## PC changes

While you play, the helper changes three things outside the game folder and undoes them when every
`StarCitizen.exe` has exited. Each is a switch in `sc-offline.ini`, on by default.

| Key | Going in | Coming out |
| --- | --- | --- |
| `block_network` | Windows Firewall rules, inbound and outbound: `sc-offline: block StarCitizen.exe` (this install's exe only), `sc-offline: block RSI Launcher.exe` (found from its install entry, beside the game library, or the default folder) and `sc-offline: block CrashHandler.exe` (`<channel>\Tools\Public\CrashHandler.exe`, CIG's crash reporter). A rule is only added if its exe exists. `sc-offline.exe` itself stays online for updates. With `multiplayer = on` (the default), the `StarCitizen.exe` rule blocks every address except loopback, the private ranges (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`), link-local (`169.254.0.0/16`) and `multiplayer_allow` (all IPv6 stays blocked), and one more rule, `sc-offline: allow StarCitizen.exe multiplayer (LAN)`, lets PCs on your local subnet (and `multiplayer_allow`) reach the game over UDP, for a session you host from the [Multiplayer tab](features.md#multiplayer-lan-or-vpn). If Windows refuses the narrower rule, the launcher blocks every address as before and says so. | deleted |
| `eac_hosts` | `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com # added by sc-offline…` appended to the hosts file, then `ipconfig /flushdns`. Skipped if the hosts file already blocks it | only the tagged line removed, DNS flushed |
| `eac_rename` | `EasyAntiCheat_EOS.exe` renamed to `EasyAntiCheat_EOS.exe.bak`. Skipped if it isn't there | renamed back |

- Each change is written to `%ProgramData%\sc-offline\pc-changes.txt` the moment it is made, and only recorded
  changes are undone. A hosts line or `.bak` you made yourself is never touched.
- If a step fails, the helper undoes what it already did and the game doesn't start.
- After a crash the record stays. `status` lists it (read-only), `uninstall` undoes it (**Repair** in the window), and `play` offers to.

### Updates

On `play` the launcher asks GitHub for the newest release of `scuba-sc/sc-offline`
(3-second timeout; no network just skips it). With `update_channel = stable` (the default) only full
releases are offered; `update_channel = prerelease` also offers pre-releases (test builds). Drafts, older
versions and tags that aren't plain numbers are never offered. If a newer one exists, `play` asks
**"Update now?"**. `status` makes no network request; run `sc-offline.exe update` to check on demand. It won't update while
`StarCitizen.exe` is running.

An update:
1. checks there's free disk space, then downloads `sc-offline-<tag>.zip`. The download only times out when
   it stalls, and is tried once more. The zip's SHA-256 must match the `digest` GitHub publishes for that
   asset; a mismatch or a missing digest stops it with nothing changed;
2. unpacks it with Windows' own `tar.exe` and checks it against its `manifest.json` (see below). Any
   problem stops it with nothing changed;
3. stages the listed files in `data\update\staged\`. This runs with normal rights. When the install folder
   isn't writable (Program Files), it stages in `%LOCALAPPDATA%\sc-offline\update\` instead, and only the
   next step asks Windows for administrator rights. That step uses no network: it checks every staged
   file against the manifest again before using it;
4. replaces every listed file except `sc-offline.ini` and the player files in `data\` (`wallet.txt`,
   `spawn.txt`, `bookmarks.txt`, `locations_found.txt`, `game-path.txt`, `game-build.txt`, the logs).
   `data\storage\` (your saves) is never in a release, so an update doesn't touch it.
   Each file is written as `<name>.update-new`, flushed to disk, the old one renamed to `<name>.update-old`,
   and the new one moved into place and checked again. Each step is written to `data\update\applied.txt`
   before it happens. A file that antivirus is scanning is retried for a few seconds;
5. appends settings that are new in the release's `sc-offline.ini` to yours, commented out, so their
   defaults apply;
6. runs `sc-offline.exe --self-test` from the new files, then starts the new launcher with the same arguments.

If a step fails, or the new launcher fails its self-test, the old files are put back at once. If the PC dies
mid-update, the next run of `sc-offline.exe` reads `applied.txt` and puts them back. The `.update-old` files
are deleted on the next good start. `check_updates = off` turns the check off; `sc-offline.exe update`
checks on demand.

#### What an update is checked against

Two things vouch for an update. Neither is a signature: releases aren't code-signed.

1. **GitHub's digest.** GitHub publishes a SHA-256 `digest` for every release asset, served over HTTPS
   from `api.github.com`. The downloaded zip must match it.
2. **`manifest.json` inside the zip.** CI writes it when it builds the release: the `version`, the `tag`,
   the `commit` it was built from, and every other file in the zip with its `sha256` and `size`, one per line.
   Its version must equal the release tag. Only files it lists are copied, and only when their SHA-256 matches.
   A listed path with `..`, a leading `/`, a `\` or a `:` makes the whole update fail.

So the update is exactly what CI built from that commit, as long as the GitHub release itself is
trustworthy. Anyone who can publish a release on `scuba-sc/sc-offline` can publish an update.

#### Checking a release by hand

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

No output after the summary line means every listed file matches. A file in the folder that the manifest
doesn't list (other than `manifest.json`) isn't part of the release. With Python (macOS, Linux or Windows),
`python3 tools/release-manifest.py check sc-offline-v0.8.0` from a clone of this repository does step 2,
also flags unlisted files, and exits non-zero on any problem. The `commit` field lets you
look up the exact source at `https://github.com/scuba-sc/sc-offline/tree/<commit>`.

### Discord status

The Discord folder (`data\plugins\discord`, source: `plugins/discord/`) is **off pending owner transport and transmitted-field approval**. The packaged folder metadata includes `disabled`. A manually enabled folder loads only local diagnostics: `discord.status` returns unavailable with the approval blocker. It reads no game location, ship, host version, process ID or session timestamp, opens no custom pipe/network connection and sends nothing. Its three existing bool preferences remain stored but inactive; their defaults are now false and a kept true value cannot enable presence.

The former implementation sent activity over Discord's local named pipe, outside host-mediated SDK transport. Local delivery did not prove information stayed local: Discord may publish presence through its own service. No approved Discord transport exists in SDK 2.4.0's shared-memory `sco.ipc` or private-session `sco.net` contracts; no custom IPC/network exception is introduced. See the [transport/privacy review, standalone proof and pending acceptance](../plugins/discord/README.md).

The top-bar and Plugins-page switch still creates or removes `data\plugins\discord\disabled`, applying on the next **Play**; removing the marker enables diagnostics only, not Rich Presence. Existing legacy `discord_presence = off` migration and launcher behavior are unchanged. Shared host/CI release wiring must stage the marker with the isolated module; local checker success is not proof of final release packaging, host discovery, Windows load or gameplay.

### Crash reports

If the game wrote crash files under `<channel>\Crashes` during the session, the launcher offers a report
before the log cleanup. A **y** zips `mod.log`, `launcher.log`, `Game.log` and the crash folder's text
files into `data\crash-reports\sc-offline-crash-<time>.zip`. In the copies (never the originals), your RSI
handle, nickname and character name, GEID and account numbers become `<redacted>` wherever they appear (the
launcher learns them from every bundled file first), e-mail addresses become `<redacted>`, and your Windows
user name in paths and text becomes `<user>`. Free text can still hold a name that no log line announced, so
read the zip before you share it. The report is stricter than the MCP `read_log`: a handle from a login line is hidden inside other words too, even if it is short or an ordinary word, and every `Handle[..]`/`Nickname[..]` value in any bundled file is hidden everywhere. The same redactor serves the [MCP server](ai-agents.md). Memory dumps (`.dmp`) are left out because they can't be redacted. It can then open the bug
form with the version, game build and platform filled in, and an Explorer window on the zip. **Nothing is
uploaded**: you attach the zip and read it first. `crash_reports = off` turns this off.

### Administrator rights

The launcher never asks to run as administrator, and the game never runs as administrator. Windows asks once,
only for the step that needs it:
- the helper, when the firewall, EAC or a protected game folder is involved (see [PC changes](#pc-changes));
- deleting session logs that a protected game folder won't let you delete. The elevated run works out this
  session's files again by itself;
- updating a mod folder only administrators can write, e.g. under Program Files. The elevated run asks GitHub
  again and applies only the version you were shown, then the new launcher starts with normal rights.

If the mod folder's `data` can't be written, the mod can't save your wallet or places, and the launcher tells
you to move the folder (for example to your Desktop).

Start `sc-offline.exe` with normal rights. An elevated launcher attempts to use the desktop token for the game; if unavailable, it falls back and warns. Remove *Run this program as an administrator* from `StarCitizen.exe` Properties to launch with normal rights. On NVIDIA GPUs, turn off **Smooth Motion** for Star Citizen in the NVIDIA App first; with it on, the game can crash during startup ([README troubleshooting](../README.md#troubleshooting)). Crash diagnostics identify the fault module/offset and raw-stack executable-address candidates, not an unwound call chain.

### Session logs

When the game closes, the launcher lists the logs the game wrote during this session: `<channel>\Game.log`,
new files in `<channel>\logbackups`, and new files under `<channel>\Crashes`. It asks **"Delete these
files?"**. The answers are `y` (delete; one more **"Are you sure?"** follows), `n` or a bare Enter (keep), `always`
(delete now, and after every session without asking) and `never` (keep, and never ask again); in the window they
are the buttons **Delete**, **Keep**, **Always delete** and **Never delete**. Anything unclear keeps the files.
**Always** and **Never** write `clean_logs = always` or `clean_logs = never` into `sc-offline.ini`; every other
line and comment in the file stays as it is. With `always` only this session's files are deleted, still through
the same administrator prompt when the game folder refuses a plain delete. Files older than the session
(for example from online play) are never touched, and neither are `data\mod.log` and `data\launcher.log`.
Keep `Game.log` if you want to report a bug. Set `clean_logs = never` in `sc-offline.ini` (or on the Settings
page) to skip the question; `ask` brings it back.
- Under Wine none of this runs; `sc-offline.sh` handles hosts there, and the firewall rule doesn't apply.
- The firewall rule cuts the game's network for the session. It doesn't hide anything already on disk, such as
  logs or the renamed EAC file while you play.

## `sc-offline.ini`

Each line is `key = value`. Lines starting with `#` are comments. An unknown key is reported when the launcher starts, not silently ignored.

There is no wallet setting: your aUEC balance is kept in `data\wallet.txt`, which you can edit (see [Features](features.md#player)).

Reading a setting does not establish that its requested game feature is available.
The launcher retains `start` and `start_ship` and passes them to the host for
compatibility. The current [external spawn module](../plugins/spawn/README.md)
explicitly refuses destination startup (`spawn.startup`): it does not implement
the G2/G3 player relocation/full-pose route. Setting `start = Daymar` therefore
does not restore the old timed arrival or boarding feature. Legacy helper source
is not a registered feature or a fallback. This is separate from whether the
launcher read the INI file; game acceptance remains pending.

| Key | Default | Meaning |
| --- | --- | --- |
| `game` | (found automatically) | Your Star Citizen folder. You can point at `Roberts Space Industries`, at `StarCitizen`, at the channel folder, or at the folder that holds `StarCitizen`. |
| `check_updates` | `on` | Check GitHub for a newer release on `play` and offer to install it. |
| `update_channel` | `stable` | `stable`: only full releases are offered. `prerelease`: pre-releases (test builds) are offered too. |
| `crash_reports` | `on` | After a crash, offer a redacted log bundle and the bug form. See [Crash reports](#crash-reports). |
| `clean_logs` | `ask` | After the game closes: `ask` lists this session's game logs and asks before deleting them (Delete asks twice; the question also offers Always and Never, which write `always` or `never` here); `always` deletes them without asking; `never` keeps them and doesn't ask. The older `on` (= `ask`) and `off` (= `never`) still work. |
| `channel` | `LIVE` | Which install to use when `game` points above it: `LIVE`, `PTU`, `EPTU`, and so on. |
| `menu` | `imgui` | Which in-game menu the mod shows. `imgui`: the menu it has always had. `rust`: the new menu (`sc_offline_menu.dll`, drawn with egui) shows the same panels. It has never run in a game, so it is opt-in, and it opens on **M**, the ImGui menu's key, which it replaces while it is selected (set `SC_OFFLINE_MENU_KEY` to a letter or F1 to F12 to use another key). Pages that are only `sco.ui` ImGui tabs open through its "Classic pages" entry. The launcher passes the choice to the game as `SC_OFFLINE_MENU`. If the DLL is missing or does not load, the ImGui menu stays, and `mod.log` says why in one `[menu]` line (`rust menu started` when it worked). The Rust hook is compiled into `dinput8.dll` only when the mod is built with `-DSCO_HOST_MENU_CLIENT=ON` against a Core that has `sco/widgets_view.h` ([build.md](build.md#cmake-options-that-change-the-menu)); a DLL built without it ignores this key. The Settings page writes this key too. |
| `boot_map` | `PU_All` | `PU_All` loads every star system, so Travel can reach Pyro and Nyx. |
| `start_ship` | `DRAK_Cutlass_Black` | Retained legacy startup ship request, parsed and passed to the host. The current external spawn module does not implement destination startup; this key does not select an ordinary menu spawn or enable boarding. |
| `start` | (empty) | Empty leaves the game's own spawn policy. The legacy `Daymar` request is retained in configuration, but destination startup is unavailable in the current external spawn module. No timed arrival or pilot-seat guarantee. |
| `plugins` | `on` | `on`: load plugins from `data\plugins\<id>\`. See [Plugins](features.md#plugins). |
| `asop_fleet_list` | `ships` | Which ship list the terminals show with `asop = on`: `game` (the game's own; offline its entitlement query fails, so it's empty) or `ships` (every ship in `data\ships.txt`). |
| `mining` / `mining_debug` (legacy) | ignored | Moved to the mining plugin enabled/debug settings, default false. Launcher warns; no automatic import. See [deliberate migration](../plugins/mining/README.md). |
| `asop` | `on` | `off`: leave the ship terminals, personal hangars, the hangar lift and ATC hails as the game has them offline. See [Ship terminals, hangars and ATC](features.md#ship-terminals-hangars-and-atc). |
| `block_network` | `on` | Block `StarCitizen.exe`, the RSI Launcher and the game's `CrashHandler.exe` in Windows Firewall while you play. See [PC changes](#pc-changes). |
| `multiplayer` | `on` | The [Multiplayer tab](features.md#multiplayer-lan-or-vpn). Nothing connects until you press Host or Join there; with `block_network` on, the firewall leaves your LAN open for it (see [PC changes](#pc-changes)). `off`: no tab, and the firewall blocks every address. |
| `multiplayer_allow` | (empty) | Firewall exceptions besides your LAN, for a VPN: up to 8 private or shared-space IPv4 ranges separated by commas (Tailscale: `100.64.0.0/10`). Each whole range must be within `10/8`, `127/8`, `169.254/16`, `172.16/12`, `192.168/16`, or `100.64/10`. Allow only your VPN's range. These firewall exceptions do not authorize a G13 session: the host's separate exact bind and peer policy still applies. The launcher clears values that do not parse with a warning; malformed INI does not itself always refuse startup. Admission independently validates the forwarded environment value and refuses malformed input. Canonical environment formatting is not authenticated provenance: another process can supply the same text. Public ranges and ranges crossing those boundaries are rejected by both launcher and admission. A declared range exempts only that IPv4 space from live firewall coverage: both block directions and full IPv6 coverage are still required. This does not change TCP session classification. |
| `eac_hosts` | `on` | Add the EAC hosts line while you play. |
| `eac_rename` | `on` | Rename `EasyAntiCheat_EOS.exe` while you play. |
| `registry_probe` | (off) | Test only. `1` writes `data/registry-dump.txt` 15 s after you spawn: one `name<TAB>vehicleSize` line per entity class the probe walked and the game's own name lookup resolved back to that class. It stops at the first fault and writes nothing, so a partial list never looks complete. |
| `exp_loadout_async` | `on` | Lets the menu equip gear (`game.loadout.async`). Passes `SCO_LOADOUT_ASYNC=1` to the game. Seen working in game by the owner on build 4.10.200.58746: equipping attached the items. See [Game capability switches](#game-capability-switches-in-sc-offlineini). |
| `exp_inventory_read` | `on` | Lets plugins read what you wear and carry (`game.inventory.read`). Passes `SCO_INVENTORY_READ=1`. Not run in game. |
| `exp_actor_stats_write` | `on` | Lets plugins change your survival stats (`game.actor_stats.write`). Passes `SCO_ACTOR_STATS_WRITE=1`. Not run in game. |
| `exp_cvars_native` | `on` | Lets plugins read four console settings, read only (`game.cvars.read`). Passes `SCO_CVARS_NATIVE=1`. Not run in game. |
| `exp_audio_native` | `on` | Asks sco-core for native one-shot sounds (`SCO_AUDIO_NATIVE=1`). The pinned sco-core refuses them and logs why. Not run in game. |
| `exp_entities_diag` | `off` | Installs log-only entity hooks and writes one walk to `mod.log` (`SCO_ENTITIES_DIAG=1`). Diagnostics only, no gameplay change. Not run in game. |

### Game capability switches in `sc-offline.ini`

sco-core starts some capabilities only when a variable is `1` inside the game process. The launcher sets these variables for you from the `exp_` keys above, so you never edit the environment by hand. The keys are in `sc-offline.ini` and you can turn any of them off.

- **On by default.** `exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native` and `exp_audio_native` are on in the release packages; `exp_entities_diag` (log-only diagnostics, not a feature) is off. Each is a visible setting you can turn off, and each says what has and has not been run in game. Turn one off with `exp_<name> = off`. Values are `on`/`off` (also `yes`/`no`, `1`/`0`, `true`/`false`); anything else keeps the default and is reported.
- **Run in game once or twice.** Only `exp_loadout_async` has been seen working in game (the owner's sessions on build 4.10.200.58746: equipping attached the items, `AttachmentReceived` in `Game.log`). The other switches have not been run in game. A switch being on means the launcher asks sco-core to start the capability. It does not mean it works.
- **Off means unset.** When a key is off the launcher removes its variable from the game's environment. A variable left in your shell or in Windows therefore cannot turn a capability on: only the ini can. The launcher prints `[i] removed ... from the game's environment` when it removes one that was set.
- **One line at start.** The launcher prints `Capability switches on:` with each key that is on and the variable it sets, followed by `not run in game`.

Only variables that sco-core reads are listed (`SCO_LOADOUT_ASYNC`, `SCO_INVENTORY_READ`, `SCO_ACTOR_STATS_WRITE`, `SCO_CVARS_NATIVE`, `SCO_AUDIO_NATIVE`, `SCO_ENTITIES_DIAG`). The window's Capabilities page has a toggle for each switch; you can also edit the ini (Settings > Edit sc-offline.ini). When an update adds a key to an existing ini, it is appended commented out with its explanatory line above it, and the default applies.
