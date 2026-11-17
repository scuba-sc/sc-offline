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
when it needs it; nothing listens on a port and nothing goes over the network.

| Tool | Returns |
| --- | --- |
| `get_status` | The same checks as `sc-offline.exe status`: launcher version, game folder and build, whether the mod is installed, anti-cheat state, the network-block, EAC-rename and plugin settings, plugin counts, PC changes left to undo, Wine, and whether this install's game is running |
| `list_plugins` | Every plugin folder under `data\plugins` with a `plugin.ini`: enabled or not, and its id, name, version, author, API, kind, entry and description, with warnings such as an id that does not match the folder |
| `read_log` | The last lines (up to 500) of `data\mod.log`, `data\launcher.log`, the previous `launcher.log.1` or the game's `Game.log`, optionally only lines containing some text |
| `get_settings` | The settings in `sc-offline.ini`, in file order |

It cannot start, stop or install the game, change settings, enable or disable plugins, or run
any command in the game. Those stay with you, through the launcher window or its commands
([Launcher](launcher.md)). The server also does not read game memory: with the game running, it
sees what the logs and files show.

Paths under your Windows profile come back as `%USERPROFILE%`, and other `C:\Users\<name>`
paths as `C:\Users\<user>`. Log lines are otherwise returned as written, so read them before
pasting them anywhere public (see [CONTRIBUTING](../CONTRIBUTING.md)).

## Set it up

Use the full path of your `sc-offline.exe`. The examples use `C:\Games\sc-offline`.

**Claude Code**

```sh
claude mcp add sc-offline -- "C:\Games\sc-offline\sc-offline.exe" mcp
```

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

**Codex** (`~/.codex/config.toml`):

```toml
[mcp_servers.sc-offline]
command = 'C:\Games\sc-offline\sc-offline.exe'
args = ["mcp"]
```

Then ask the agent something like "Is my sc-offline install ready to play?" or "Why didn't the
crew plugin load? Check the mod log." It should call `get_status`, `list_plugins` and `read_log`.

Under Wine or Proton, point the client at a script that runs `wine sc-offline.exe mcp` in the
same prefix you play with; the server itself is the same.

## Check it by hand

The server reads one JSON-RPC message per line. This sends an `initialize` and lists the tools:

```powershell
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"check","version":"1"}}}',
'{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | .\sc-offline.exe mcp
```

Two JSON lines come back: the server's name and version, then the four tools. A one-line note on
stderr says the server started. If you see the launcher's usage text instead, the
`sc-offline.exe` is older than this feature.

## Live in-game data

The server reads files, so it cannot tell which plugins the running game actually loaded, which
services were published or which signature rows resolved. sco-core has a separate, default-off
runtime diagnostics server for that (see
[sco-core runtime MCP](https://github.com/scuba-sc/sco-core/blob/main/tools/sco-core-mcp-runtime/README.md)).
sc-offline does not enable it: it is waiting for its Windows security acceptance, and
[decisions](decisions.md) keeps console and command access away from MCP.

## For developers

The protocol and the tools are in `launcher/mcp_server.h` (no Windows code, so it is tested on
any platform); `launcher/launcher.cpp` (`RunMcp`) supplies the files and the status checks.
`python tools/check_launcher_mcp.py` builds and runs the portable test;
`python tools/check_launcher_mcp.py --exe <built sc-offline.exe>` also runs a real stdio session
against a scratch install and checks that no file or folder changed.
