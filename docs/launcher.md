# The launcher: `sc-offline.exe`

The launcher starts the game with the mod and takes the mod out again when the game closes. Source: `launcher/launcher.cpp`.

## Commands

```text
sc-offline.exe [play|install|uninstall|status|mcp|update|help] [--game <folder>] [--dry-run] [--skip-eac-check] [--game-log]
```

| Command | What it does |
| --- | --- |
| `play` | The default from a terminal (a double-click opens the [window](#the-window)). Copies the mod in, starts the game, takes the mod out when the game closes. |
| `install` | Copies the mod in and leaves it there, for starting the game some other way. Run `uninstall` before going online. Command line only: the [window](#the-window) has no Install button. |
| `uninstall` | Takes the mod out, puts back anything it replaced, and undoes leftover [PC changes](#pc-changes). Refuses while the game is running. The window's **Repair: undo PC changes** runs it. |
| `status` | Runs the [checks below](#checks-it-runs-every-time) and is read-only: it writes no file (not `data\launcher.log`, not `data\game-path.txt`), doesn't migrate `discord_presence`, doesn't settle an interrupted update, doesn't open the folder picker and makes no network request (no update check). If the game folder can't be found it says so; `play` still remembers what it finds. The window's Status panel runs the same checks. |
| `mcp` | Runs a read-only [MCP server for AI agents](ai-agents.md) on stdin/stdout: status (the same checks as `status`), installed plugins, log tails and `sc-offline.ini`. Like `status` it writes nothing and makes no network request; it runs no command and changes no setting. The game's `Game.log` is served only with `mcp --game-log`; without the flag the file is not opened (it can hold account names). |
| `update` | Checks GitHub for a newer sc-offline and offers to install it; see [Updates](#updates). |
| `help` | Prints the usage. |

| Option | Meaning |
| --- | --- |
| `--game <folder>` | Your `Roberts Space Industries`, `StarCitizen`, channel or `Bin64` folder. Overrides `game =` in `sc-offline.ini`. |
| `--dry-run` | Prints every step it would take, then stops. Nothing is copied, deleted or started. |
| `--skip-eac-check` | Don't stop when Easy Anti-Cheat looks active. |
| `--window` | Open the window from a terminal, or under Wine/Proton. |
| `--console` | Double-clicked: run `play` in the console instead of opening the window. |
| `--game-log` | With `mcp` only: let the MCP server's `read_log` serve the game's `Game.log`, redacted like the other logs. Off by default. Any other command refuses it. |

Exit codes: `0` ok · `1` error · `2` Easy Anti-Cheat is active · `3` the game is running. The helper's own codes, in `launcher.log`: `5` a PC change failed.

Everything the launcher prints also goes to `data\launcher.log` (a new file each run: the previous one is kept as `launcher.log.1`, replacing any older `.1`, so the last session's Stop and End lines survive one more run; the helper appends to it), except for `status`, which writes nothing.

## The window

A double-click (no arguments, the exe's own console) opens a 720×376 window instead of the console run. It is
drawn with ImGui on Direct3D 11 (the vendored `src/third_party/imgui`, the copy the mod's menu uses). If
Direct3D 11 isn't available, it says so and tells you to run `sc-offline.exe play` from a terminal. Under Wine the
console run stays the default; `--window` opens the window anyway.

The sidebar has three pages (the fourth, Settings, opens from the top bar):

- **Home**: the *Status* panel on the left and the *Actions* on the right. **Play** and **Update** run
  `sc-offline.exe <command>` as a hidden child with `SC_OFFLINE_GUI=1`, so the window and the CLI share one code
  path. The child's `[y/N]` questions turn the buttons into **Yes** / **No** (or press Y / N); the session-log
  question has four answers, **Delete**, **Keep**, **Always delete** and **Never delete** (see
  [Session logs](#session-logs)). **Play** and **Update** are disabled while a command runs and while the game runs.
  - **Stop**: while the game runs, **Play** becomes **Stop**. First it asks the mod to run its exit work: if the mod
    is loaded in the game process it has made a per-process event, `Local\sc-offline-stop-<pid>` (session-local,
    default security descriptor, so only the same user can open it). Stop signals it and waits up to 10 seconds for
    `Local\sc-offline-stopped-<pid>`; on the game thread the mod then runs what a normal quit runs (the wallet is
    saved, the private session closed, `game.exit` sent to plugins, admission closed) once, logs `[app] stop requested
    by the launcher: exit work done` to `data\mod.log`, and answers. It unloads nothing and does not quit the game
    itself. **After Stop the mod stays closed until the game exits**: its menu, hotkeys and timers (the 5 second
    wallet poll among them) stop, and nothing is saved again. If the game does not close and **End** is declined,
    the game keeps running without the mod. Closing a game window with WM_CLOSE alone skips that exit work (the
    wallet was not saved). While the launcher waits for the mod's answer the button shows **Saving…** and the window
    stays responsive. Without the event (the mod is not loaded, or the game is online) Stop goes on straight to the
    next step; it does too when the mod has already closed itself (the game was quitting), when the game process
    exits, or when the mod has not answered after 10 seconds. If the mod is installed in the game folder but did not
    answer (no event, an event that can't be opened, or a timeout), the window says "The mod didn't answer; closing
    without its save." and logs it. Then Stop posts WM_CLOSE to the visible top-level window(s)
    of this install's `StarCitizen.exe`: the game folder is resolved as Play does (`game =`, else the folder the panel
    found), and Stop refuses when it isn't known; it never touches another install (PTU, another folder). The play
    command that started the game keeps waiting for it, so the usual cleanup runs when the game exits: the mod out,
    the PC changes undone. Nothing terminates the game automatically. If it is still running 60 seconds after Stop,
    the Actions card asks **"End the game process?"**; only **Yes** ends that process (the one that was asked to
    close, checked by its creation time), **No** leaves it alone and **Stop** can be pressed again. Each step is
    written to `data\launcher.log` with a time: Stop pressed (targets and process ids), the stop event signalled or
    not present, the mod's answer (after how many milliseconds) or its timeout, WM_CLOSE posted, the game exited, the
    End confirm shown, the End answer, and each process ended.
  - **Repair banner**: when `%ProgramData%\sc-offline\pc-changes.txt` holds a record and the game is not running
    (after a crash; not while a command such as Play runs), a banner across the top of the page lists what is left and has the **Repair: undo PC changes**
    button, which runs `uninstall`.
  - **Status panel**: the same read-only checks as `sc-offline.exe status`: the game folder and build, whether the
    mod is installed, PC changes left over, whether the network is blocked while you play, Easy Anti-Cheat, and how
    many plugins are enabled. It is checked when the window opens, after the game exits (and after a command or a
    change in what is left over), whenever the game starts or stops or a command starts or ends, every 5 seconds
    while the game or a command is running, and when you press the refresh icon in its header, on a background
    thread. While you play, the Mod row reads "Installed (this session)" and PC changes "Applied for this session;
    undone when the game closes" instead of a leftover warning. "The game is running" (Play becomes Stop, the
    Status dot) means a `StarCitizen.exe` of this install's folder, the same rule Stop uses; a process of that name
    in another folder is not counted (only the update's file swap stays cautious about any of them). The game folder comes from `game =` in `sc-offline.ini`, else the usual places (the same search as
    `status`). Nothing is written.
- **Plugins**: see [below](#the-plugins-page).
- **Output**: what the running command prints, with a **Copy** button. **Update** and **Repair** switch to it.

The top bar has the **Discord** switch (the Discord plugin, `data\plugins\discord`, used from the next Play), **Settings**
(the Settings page), **Logs** (opens `data`), minimize and close. Drag the empty part of the bar to move the window.

The **Settings** page has **Repair: undo PC changes** (enabled whenever no command runs and the game is closed; it
says "Nothing to repair" when there is nothing), the three-way **Game logs after a session** choice (**Ask each time**, **Always delete**, **Never delete**:
`clean_logs` in `sc-offline.ini`), and **Edit sc-offline.ini**, which opens the file in Notepad. The language switch
stays in the top bar.

The status line above the panel's rows uses only cheap checks: the game folder from `game =`, `data\game-path.txt`
or what the panel found, the `sc-offline.installed` marker or sc-offline's `dinput8.dll` in Bin64, and
`%ProgramData%\sc-offline\pc-changes.txt`. It refreshes every 1.5 seconds, including while **Play** is running, and
after every command. Hover it for the detail:

- **Safe to go online**: nothing of the mod is in Bin64 and no PC changes are left;
- **Not safe to go online** / **yet**: the game is running, or it lists what is left;
- **Game folder not known yet**: **Play** looks for it, or press the refresh icon.

After **Update** applies a new version, the window restarts itself. With any argument, the console run is
unchanged.

### The Plugins page

The page manages `data\plugins\<id>\` the way the host (sco-core) reads it, with no separate list of its own:

- **What is listed**: every folder under `data\plugins` that holds a `plugin.ini` (a folder without one is skipped,
  as the host does), with its `name`, `version` and `author`; `description`, when a plugin adds one, is the
  tooltip. Folders are rescanned every 3 seconds.
- **On / off**: the switch creates or deletes an empty file named `disabled` in the plugin's folder. The host
  reads that marker when the game starts, so a change applies at the next start. The old `data\mods\mod.txt` /
  `enabled.txt` split is gone: nothing here reads or writes it.
- **The creative plugin** (`creative`: noclip, god mode, infinite ammo) ships **on**. Switch it off here if you
  do not want it; the choice is the `disabled` marker in its folder.
- **Plugins are off**: the host loads no plugin unless `plugins = on` in `sc-offline.ini` (the default is on).
  When it is off, the page says so and **Turn on** writes the key.
- **Settings** (the gear, for a plugin whose `plugin.ini` has a `[settings]` section): edits the values the
  plugin declared (`bool`, `int`, `float`, `string`, `enum(...)`), checked against the declared range and
  choices with the host's rules. **Save** writes them where `sco.settings` keeps them: the plugin's own database
  `data\storage\<id>.db`, table `sco_kv`, key `sco.settings.<name>`, value `<type>:<value>`. **Reset** deletes the
  kept values, so the defaults apply. The fields are read-only while the game or a command is running: the host reads
  its settings once, at start, so a write during a session would not be seen and no `settings.changed` would fire.
  Close the game, edit, start it again. A plugin whose `[settings]` this page can't read is flagged; run
  `sco-plugin-check` on it.
- **Reload**: only shows a note. Hot reload runs inside the game, through its `sco.reload` command; the launcher
  has no channel into the running game to call it.

#### Last startup snapshot

The Plugins page has a **Last startup snapshot (may be stale)** panel and a per-folder “At last startup” line.
It reads only the optional `data\plugin-startup.json` file. It never tails `data\mod.log`, contacts the host,
or reports live state. The current product has no writer for this file, so it normally shows **No completed
startup snapshot**. The existing [host startup refusal](host-composition.md) is unchanged.

The current `plugins` value in `sc-offline.ini` and each folder's `disabled` marker remain the desired settings.
The panel's “plugins setting then” and row states describe an earlier startup only. Its timestamp makes the
age visible; the launcher does not decide that a snapshot is fresh. A missing or explicitly incomplete record
shows **No completed startup snapshot**. Unreadable, oversized, truncated, malformed, or unsupported input
shows **Unknown**. A folder row shows **Unknown** when no valid-manifest ID in the snapshot matches it.
Invalid-manifest aliases are not matched to folders. No free-form reason text is displayed.

The proposed v1 JSON contract is deliberately closed and bounded: at most 64 KiB and 256 plugin rows; all
strings are printable ASCII; the session ID is 32 lowercase hexadecimal characters; timestamps use UTC
`YYYY-MM-DDTHH:MM:SSZ`. The session ID is an opaque unique ID for one host startup attempt; the launcher
does not use it to infer freshness or authenticate the writer. Top-level fields are `schema_version`,
`record_state`, `session_id`, `started_at_utc`, `completed_at_utc`, `plugins_enabled_at_start`,
`plugin_count`, `loaded_count`, and `plugins`. A complete record has `record_state: "complete"`, a
completion timestamp, matching row/count values, unique IDs, and a `loaded_count` equal to rows whose state
is `loaded`. Each row has `id`,
`manifest_valid`, `version`, `kind`, `state`, `reason_code`, and `restart_required`; kinds and states are
fixed enums (`native`, `lua`, `data`, `builtin`, `unknown`; and `off`, `disabled`, `refused`, `ready`,
`loaded`, `crashed`, `unloaded`, `faulted`). Reason codes are fixed identifiers (`plugins_off`,
`plugin_disabled`, `invalid_manifest`, `plugin_refused`, `plugin_crashed`,
`provider_fault_restart_required`), never host-provided prose. `starting` has no completion timestamp;
`failed` and `incomplete` have a finalization timestamp. None supplies plugin rows or counts, and none is
displayed as a completed snapshot.
Unknown fields, duplicate keys or IDs, inconsistent counts, bad enum values, controls, non-ASCII bytes,
and partial JSON are rejected.

A future host writer should use the existing `data` directory and atomically replace the file only after
flushing a complete bounded temporary file in that same directory. A crash before replacement leaves the
previous snapshot, whose old timestamp stays visible; a published `starting`/`incomplete` record clears that
previous result from the UI. The reader opens read-only, rejects reparse points and unstable/oversized reads,
and parses only the bytes it read. No production writer or host-to-launcher integration is wired yet. In-process
native plugins are trusted code that can modify local files, so this file would not authenticate a report
against them. No new permission or live IPC path is part of this scaffold.

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
#    gh release view v0.7.0 -R scuba-sc/sc-offline --json assets --jq '.assets[] | [.name, .digest]'
(Get-FileHash .\sc-offline-v0.7.0.zip -Algorithm SHA256).Hash.ToLower()

# 2. Every unpacked file against manifest.json
Expand-Archive .\sc-offline-v0.7.0.zip -DestinationPath .
$dir = '.\sc-offline-v0.7.0'
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
`python3 tools/release-manifest.py check sc-offline-v0.7.0` from a clone of this repository does step 2,
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
| `exp_loadout_async` | `on` | Lets the menu equip gear (`game.loadout.async`). Passes `SCO_LOADOUT_ASYNC=1` to the game. Not run in game. See [Game capability switches](#game-capability-switches). |
| `exp_inventory_read` | `on` | Lets plugins read what you wear and carry (`game.inventory.read`). Passes `SCO_INVENTORY_READ=1`. Not run in game. |
| `exp_actor_stats_write` | `on` | Lets plugins change your survival stats (`game.actor_stats.write`). Passes `SCO_ACTOR_STATS_WRITE=1`. Not run in game. |
| `exp_cvars_native` | `on` | Lets plugins read four console settings, read only (`game.cvars.read`). Passes `SCO_CVARS_NATIVE=1`. Not run in game. |
| `exp_audio_native` | `on` | Asks sco-core for native one-shot sounds (`SCO_AUDIO_NATIVE=1`). The pinned sco-core refuses them and logs why. Not run in game. |
| `exp_entities_diag` | `off` | Installs log-only entity hooks and writes one walk to `mod.log` (`SCO_ENTITIES_DIAG=1`). Diagnostics only, no gameplay change. Not run in game. |

### Game capability switches

sco-core starts some capabilities only when a variable is `1` inside the game process. The launcher sets these variables for you from the `exp_` keys above, so you never edit the environment by hand. The keys are in `sc-offline.ini` and you can turn any of them off.

- **On by default.** Every capability switch is on in the release packages. Each is a visible setting you can turn off, and each says it has not been run in game. Turn one off with `exp_<name> = off`. Values are `on`/`off` (also `yes`/`no`, `1`/`0`, `true`/`false`); anything else keeps the default and is reported.
- **Not run in game.** None of these capabilities has been run in game. A switch being on means the launcher asks sco-core to start the capability. It does not mean it works.
- **Off means unset.** When a key is off the launcher removes its variable from the game's environment. A variable left in your shell or in Windows therefore cannot turn a capability on: only the ini can. The launcher prints `[i] removed ... from the game's environment` when it removes one that was set.
- **One line at start.** The launcher prints `Capability switches on:` with each key that is on and the variable it sets, followed by `not run in game`.

Only variables that sco-core reads are listed (`SCO_LOADOUT_ASYNC`, `SCO_INVENTORY_READ`, `SCO_ACTOR_STATS_WRITE`, `SCO_CVARS_NATIVE`, `SCO_AUDIO_NATIVE`, `SCO_ENTITIES_DIAG`). The launcher window's Settings page does not have toggles for these yet: edit the ini (Settings > Edit sc-offline.ini). When an update adds a key to an existing ini, it is appended commented out with its explanatory line above it, and the default applies.
