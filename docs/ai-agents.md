# Connect an AI agent

AI agents such as Claude Code, Claude Desktop, Codex, Cursor or GitHub Copilot in VS Code can
talk to sc-offline over [MCP](https://modelcontextprotocol.io) (Model Context Protocol). Three
servers cover three jobs:

| Server | Use it to | Start it with |
| --- | --- | --- |
| **sc-offline** (this page) | Look at your install: is it set up, which plugins are there, what do the logs say | `sc-offline.exe mcp` |
| **sco-core SDK** | Write a plugin: find APIs, read contracts and examples, generate and check a plugin | [sco-core agent integration](https://github.com/scuba-sc/sco-core/blob/main/docs/agent-integration.md) |
| **scuba-engine** | Test a plugin out of game in a sandbox host with a fake game | [scuba-engine MCP server](https://github.com/scuba-sc/scuba-engine/tree/main/tools/scuba-engine-mcp) |

## What the sc-offline server does

`sc-offline.exe mcp` runs a read-only MCP server on stdin/stdout. Your agent client starts it
when it needs it; nothing listens on a port and nothing goes over the network. From 0.8.0 the
server is part of the Rust launcher (crate `scol-mcp`); `sc-offline-classic.exe mcp`, the C++
launcher that ships beside it, answers the same protocol with the same four tools. Both write
nothing and make no network request. The plugin commands (`sc-offline.exe plugins ...`) are not
exposed: the server cannot change a plugin, a setting or a file.

| Tool | Returns |
| --- | --- |
| `get_status` | The same checks as `sc-offline.exe status`: launcher version, game folder and build, whether the mod is installed, anti-cheat state, the network-block, EAC-rename and plugin settings, plugin counts, PC changes left to undo, Wine, and whether this install's game is running |
| `list_plugins` | Plugin folders under `data\plugins` with a `plugin.ini` (the first 256 by name, flagged when there are more): enabled or not, and its id, name, version, author, API, kind, entry and description, with warnings such as an id that does not match the folder |
| `read_log` | The last lines (up to 500) of `data\mod.log`, `data\launcher.log` or the previous `launcher.log.1`, or, only when the server runs with `--game-log`, the game's `Game.log`; optionally only lines containing some text. Paths, your Windows account name, e-mail addresses, handles, nicknames and account and player identifiers are redacted (details below) |
| `get_settings` | The settings in `sc-offline.ini`, in file order |

It cannot start, stop or install the game, change settings, enable or disable plugins, or run
any command in the game. Those stay with you, through the launcher window or its commands
([Launcher](launcher.md)). The server also does not read game memory: with the game running, it
sees what the logs and files show.

`read_log` removes this from every line of every log it serves (`mod`, `launcher`, `launcher_previous`, and `game` when
you allow it, see [Game.log is off unless you ask for it](#gamelog-is-off-unless-you-ask-for-it)) before it filters and returns it:

- Paths under your Windows profile come back as `%USERPROFILE%`, and other `C:\Users\<name>` and
  `/home/<name>` paths as `...\<user>`. Any `Users` or `home` path component is treated this way, so an
  ordinary URL or folder such as `/home/index.html` or `D:\Games\home\StarCitizen` also loses the part after
  `home` (an accepted over-redaction). `%5C`, `%2F` and `%40` (any letter case) are decoded first when that
  exposes a path or an address (`C%3A%5CUsers%5Cname`, `name%40example.com`); a line where that finds nothing
  keeps its original bytes. Windows account SIDs (`S-1-5-21-...`) become `<redacted>`.
- Your Windows account name (the `USERNAME` variable and the last part of your profile path) becomes
  `<user>`. A name of 3 to 5 characters is removed as a whole word, in any letter case, and a dash or an
  underscore ends a word, so `ALEX-PC`, `DESKTOP-ALEX` and `alex_saves` are hidden while `Alexa` and `xalex` stay. A
  name of 6 or more characters is also removed inside other words (`DESKTOP-<name>`, `<name>2`). Names of 1
  or 2 characters are only removed from paths, because searching for them would hide parts of ordinary
  words. If your account is named like a common word, such as `admin`, that word is hidden in every line
  too. An account named `SYSTEM`, `local`, `none`, `null` or `default` is not searched for in text (paths still
  go), so `filesystem` stays readable.
- The launcher first learns, then redacts. It learns from three places: the part of the file it returns
  (the last 2 MiB) and a streaming scan of every served log from its first byte to its 64th MiB. Both are
  redone on every call, so values from earlier calls cannot fill the login-line list (what earlier calls learned
  from ordinary lines, such as a `nickname="..."` in a returned window, is kept for the session). The first tool of a session, whichever it
  is, runs the scan once before it answers, so a handle in the game folder name is hidden in `get_status` too. The scan reads in chunks and parses only the lines that contain
  `User Login Success` or `AccountLoginCharacterStatus`. Its work is bounded: at most 4096 such lines and 4 MiB of
  them are parsed per log, and only the first 16 KiB of a longer line are read. When a log is larger
  than 64 MiB, has more login lines than that, has a login line over 16 KiB, or cannot be read, the result says so: `identity_scan_complete` is `false` and `notice` names the
  log, because a login line or part of one was not read. Every handle, nickname, character name and id found
  is hidden on every line, in any letter case, in free text too. A handle that one login line shows once
  (`User Login Success - Handle[<name>], Nickname[<name>], Account[<n>]`) is gone from every other line,
  including lines about chat or other players.
- What is learned goes on two lists. Values from the real login shapes go on a list of their own (at most 384 in
  all, and at most 64 from each log, or from the part of a log that is returned; one login line adds at most 32
  entries, a value and each of its words counting as one): it is never evicted, and once it is full a new value is
  dropped, not an old one, and the result then says so: `identity_scan_complete` is `false` and `notice` names the limit. Values from any other line go on a second list of at most 128 (one line adds at most 8
  entries); when it is full it takes no more. So a noisy `mod.log` or a busy `Game.log` full of other players'
  names cannot push your handle out of the first list, and a flood of fake login lines in one log cannot fill it
  ahead of the others. Learned values of 6 or more characters are hidden anywhere, also inside other words.
  Shorter ones are hidden only as a whole word. A handle or nickname from a real login line is learned even when
  it is an ordinary word (`Server`, `Admin`, `Character`), and is then hidden as a whole word only, never inside
  other words (`Administrator` and `characters` stay). Not learned, from any line: values shorter than 3
  characters, numbers shorter than 7 digits (ports, versions, times), hex handles, the machine words `system`,
  `local`, `none`, `null`, `default`, `localsystem`, `localservice`, `networkservice`, `anonymous`, `nobody`
  and `unknown`, and anything that is part of the redaction markers (`red`, `use`). Not learned from a line that is
  not a login line: ordinary words (`true`, `server`, `star`, `admin`). The value after the weak keys `user=`,
  `owner=`, `player=` and `login=` is hidden where it stands but never taught to other lines, so a flag, a service
  account or an ordinary word there cannot make words like `filesystem` or `SystemTime` unreadable. Each word of a
  multi-word name (one with a space in it) is learned on its own, except ordinary words; a value joined by `_` or
  `-` (`PU_Human_Enemy_NPC_5555`) is learned whole only, so the words in it stay readable. The value after
  `Character:` is not a name when it is a key followed by a number (`createdAt 1581606700563`).
- The digits after `geid`, `accountid`, `account_id` and `playerid` (so `playerGEID` too) become `<redacted>` in
  any letter case when three or more digits follow with up to three of space, `=`, `:`, `"` and `[` in between
  (`geid 2000998877`, `accountId="5544332211"`, `geid""1234`, `geid123`). That is the rule the crash report used
  before the shared redactor, kept whole; both modes apply it. On top of it, a run of 9 to 12 digits after any key
  that ends in `id` becomes `<redacted>` too (`ownerId:5544332211`, `citizen_id: 2000998877`, `id=2000998877`,
  `geid(2000998877)`, `geid-2000998877`, `playerGEID<2000998877>`), with up to three of space, `=`, `:`, `#`, `(`,
  `<`, `-`, `"`, `'` and `[` in between. Shorter or longer numbers after such a key (`pid=1234`, a 13-digit
  millisecond time) stay.
- Identifiers become `<redacted>` where a key announces them. A key is matched on its last part, so
  `rsiHandle`, `targetAccountId` and `owner_account_id` count. The keys are `nickname`, `nick`, `displayName`,
  `playerName`, `characterName`, `charName`, `screenName`, `profileName`, `accountName`, `handle`, `uid`,
  and anything ending in `playerId`, `userId`, `accountId`, `rsiId` or `GEID`, after `=` or `:` (also
  written `player_id`, glued to its digits as `geid200099887766`, as an XML element or in JSON, also in a JSON
  array); `user`, `player`, `owner` and `login` after `=` or `:` (not when the value is a flag, a common word or a
  short number; `owner` and `login` only on a line that also names a handle, nickname, account or id, so
  `Owner: TerrainSystem` and `Login: starting` stay readable) and `character` after `=` or `:` on such a line; `Player[Name]` in brackets;
  a quoted name followed by a bracketed 9 to 12 digit id (`'Name' [200099887766]`, as in kill and
  corpse lines), which hides both and teaches the name; `Name[..]` on a line with a handle, account or id;
  the quoted value after `for user`; the value after `username` and `SetLocalPlayerId`; `Handle[...]`,
  `Nickname[...]`, `Account[...]` and `GEID[...]` in brackets; numbers after an id key separated by
  spaces (`geid 12 - accountId 34`, `geid #12`); `name X` on a line that names an id with a value (the
  character in the login status line); and `name=X` or `"name":"X"` on a line that also mentions a login,
  character, player, account, nickname or handle (as whole words: `CreateHandle`, `handler` and `players` do not
  count) or an id with a value. Only the login status line teaches a `name` to other lines; a bare `name=X` is hidden
  where it stands. The key stays, so the line still reads. An unquoted
  nickname or name may hold up to six words (an apostrophe between letters, as in `O'Brien`, stays in a word)
  and stops at a plain word such as `and`, `went` or `to`. A
  `handle=0x...` value is a system handle and is left alone, and a bare `name=shader` on a line with no
  such context is left alone.
- What follows `Player_` in a word (batch names such as `Player_<id>`) becomes `<redacted>`, also when it is only
  an event name. A word followed by `=` or `:` is a key and keeps its name (`Player_Count=5`, `PLAYER_COUNT: 5`);
  `player_name=...` is a name key and loses its value.
- E-mail addresses become `<redacted>`, including names with non-ASCII letters, internationalised domains,
  quoted local parts (`"a b"@example.com`) and the encoded forms `a%40example.com`, `a\u0040example.com`,
  `a\x40example.com`, `a&#64;example.com`, `a&#x40;example.com` and `a&commat;example.com`. When a decoded
  `%5C`, `%2F` or one of those spellings of `@` exposes a path or an address, the line comes back with all of them
  decoded; when none does, the original bytes stay.
- The path in the result, and every other text the server returns, is redacted with what was learned too, so a
  handle in a folder name (`D:\Games\<handle>\Game.log`) is hidden there.
- Lines are shortened to 256 KiB before redaction, to bound the work, and the output is shortened again
  after redaction. A hostile line of more than 256 KiB can therefore cut a name in two and leave its first
  part. Ordinary lines are cut only after redaction. Five hostile 1 MiB lines and a 64 MiB log are read together
  in about a second here; the checks fail above 5 seconds.

The crash-report bundle (`sc-offline.exe` offers it after a crash) uses the same redactor, learning from
every file it bundles before it redacts any of them. It runs in a strict mode: handles and nicknames from login
lines are learned even when they are short or common words (`Bob`, `Admin`), and every learned value is
hidden anywhere, also inside other words (`Bobcat`), also a machine word (`Nobody`) or a piece of a redaction marker
(`red`; the markers themselves are never touched). It also learns every `Handle[..]`, `Nickname[..]`,
`nickname=".."` and `handle=".."` value of any bundled file, as the redactor before it did, even a short number or
an ordinary word, on a list of its own of up to 16384 values (the crash flow tells you when a log had more), and
hides it everywhere in every file, also a handle that starts with `0x`. The bundle can therefore hide more than
`read_log` does, on purpose. `tools/launcher_redact_diff_test.cpp` holds a copy of that earlier redactor and checks,
over a grid of keys, separators, digit lengths and quoting and over realistic lines, that nothing it hid is shown by
the strict mode and that the relaxed mode hides the same id digits.

### Game.log is off unless you ask for it

`Game.log` is the file most likely to hold account names, handles, ids and other players' names. `read_log` with
log `game` therefore returns an error that says how to enable it, unless the server was started with the
`--game-log` argument: `sc-offline.exe mcp --game-log`. Without the flag the file is neither returned, nor
opened, nor scanned for login lines. With the flag it is served with the same redaction as the other logs, and its
login lines also teach the redactor names it hides in `mod.log` and the launcher logs. Any other extra argument next to
`mcp` is refused with a message on stderr only (stdout carries nothing but protocol lines); `--game-log` on any
other command is refused like an unknown argument.

The filter runs on the redacted text, so you cannot search for a name or an identifier.
`read_log` with log `game` reads the `Game.log` of the game folder that `get_status` reports.

What is not caught, so read the lines before you share them. Free text can still contain names:

- a game account name that no key and no scanned login line announced (for example "Welcome back, Bob" in a log whose
  login line is only in a log past its first 64 MiB, in a log the server does not scan, or in `Game.log` while it is off),
  someone else's name in chat, an unquoted `for user bob`, a bare `name=...` on a line without context;
- a name that only a weak key announced (`user=`, `owner=`, `player=`, `login=`): it is hidden on that line only;
- a name taught only by a line that is not a login line after the 128-value list was full, a login-line value
  after the 384-value list or the 64-value share of its log was full (the result says so), a login line past a log's first 4096 login
  lines or 4 MiB of them, a login line whose name sits past its first 16 KiB, and a login line in a UTF-16 log;
- a short learned value (under 6 characters) or an ordinary-word handle inside a longer word (relaxed mode);
- a Windows account name of 1 or 2 characters outside paths, a name of 3 to 5 characters inside a longer word
  (`alexpc`), a machine word as an account name; a handle that is a machine word (`Nobody`, `SYSTEM`) in the
  MCP server (the crash bundle hides it); a name of non-ASCII letters in another letter case;
- e-mail addresses written as `bob[at]example.com`, `bob at example.com` or with a full-width `@`, `user@localhost`
  or `pd@intranet` style addresses with no dotted domain, a user name in the `user:password@host` part of a URL, a
  handle in a profile address such as `/citizens/<handle>` that no login line announced, an address whose local part
  has `/`, `=` or `?` (the part before it stays), an IP-literal domain (`bob@192.168.1.5`), a last label ending in a digit;
- an id after a key that does not end in `id` (`citizen=2000998877`), or one that is not 9 to 12 digits long after
  a key such as `ownerId` (the keys `geid`, `accountid`, `account_id` and `playerid` hide three digits or more),
  an id written with separators (`2,000,998,877`: only the first group goes);
- paths or addresses encoded in any way other than `%5C`, `%2F` and `%40` (`%3A` alone, double encoding such as `%2540`, `[at]`);
- a line longer than 256 KiB, where a name can be cut in two.

Read the lines before pasting them anywhere public or sharing a crash report (see [CONTRIBUTING](../CONTRIBUTING.md)).

## Set it up

Use the full path of your `sc-offline.exe`. The examples use `C:\Games\sc-offline`.

**Claude Code**

```sh
claude mcp add sc-offline -- "C:\Games\sc-offline\sc-offline.exe" mcp
```

To let the agent read `Game.log` too, add `--game-log` after `mcp` (see above).

**Claude Desktop, Cursor** (`claude_desktop_config.json`, `.cursor/mcp.json`) and any client that
reads an `mcpServers` file such as a project `.mcp.json`:

```json
{
  "mcpServers": {
    "sc-offline": {
      "command": "C:\\Games\\sc-offline\\sc-offline.exe",
      "args": ["mcp"]
    }
  }
}
```

**VS Code** (`.vscode/mcp.json`):

```json
{
  "servers": {
    "sc-offline": {
      "type": "stdio",
      "command": "C:\\Games\\sc-offline\\sc-offline.exe",
      "args": ["mcp"]
    }
  }
}
```

For the other clients the flag goes into `args` too: `["mcp", "--game-log"]`.

**Codex** (`~/.codex/config.toml`):

```toml
[mcp_servers.sc-offline]
command = 'C:\Games\sc-offline\sc-offline.exe'
args = ["mcp"]
```

Then ask the agent something like "Is my sc-offline install ready to play?" or "Why didn't the
crew plugin load? Check the mod log." It should call `get_status`, `list_plugins` and `read_log`.

Under Wine or Proton, point the client at a script that runs `wine sc-offline.exe mcp` (add `--game-log` if you
want `Game.log`) in the same prefix you play with; the server itself is the same.

## Check it by hand

The server reads one JSON-RPC message per line. This sends an `initialize` and lists the tools:

```powershell
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"check","version":"1"}}}',
'{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | .\sc-offline.exe mcp
```

Two JSON lines come back: the server's name and version, then the four tools. A one-line note on
stderr says the server started. If you see the launcher's usage text instead, the
`sc-offline.exe` is older than this feature (the `mcp` command came with 0.7.8.x).

## Live in-game data

The server reads files, so it cannot tell which plugins the running game actually loaded, which
services were published or which signature rows resolved. sco-core has a separate, default-off
runtime diagnostics server for that (see
[sco-core runtime MCP](https://github.com/scuba-sc/sco-core/blob/main/tools/sco-core-mcp-runtime/README.md)).
sc-offline does not enable it: it is waiting for its Windows security acceptance, and
[decisions](decisions.md) keeps console and command access away from MCP.

## For developers

The Rust server is `launcher-rs/crates/scol-mcp` (protocol and tools) over `scol-redact` (the
redactor shared with the crash report); `scol-cli` supplies the files and the status checks.
`launcher-rs/tests/mcp-vectors` holds the request and answer vectors both servers must match.
The C++ twin, kept for `sc-offline-classic.exe`, is `launcher/mcp_server.h` (no Windows code, so it is
tested on any platform) with `RunMcp` in `launcher/launcher.cpp`, and `launcher/redact.h`.
`python tools/check_launcher_mcp.py` builds and runs the portable tests (the protocol and redaction checks in
`tools/launcher_mcp_test.cpp`, and the comparison with the earlier crash redactor in
`tools/launcher_redact_diff_test.cpp`);
`python tools/check_launcher_mcp.py --exe <built sc-offline.exe>` (the Rust exe or the C++ one) also runs real stdio sessions
against a scratch install, with and without `--game-log`, checks that no file or folder changed, and times a log
larger than 64 MiB with hostile lines. None of this ran in game.
