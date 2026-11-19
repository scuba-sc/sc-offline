<!-- SPDX-License-Identifier: GPL-3.0-only -->
# MCP and redactor vectors

Shared test vectors for `scol-redact` and `scol-mcp`. The C++ code wrote the expected results. The Rust tests
must give the same bytes. Synthetic data only: made-up names, ids and paths. None of this has been run in game; see [docs/status.md](../../../docs/status.md).

| File | Written by | Holds |
|---|---|---|
| `scenarios.json` | people | The inputs: redaction cases, MCP sessions over a fake platform, stdio framing cases. No expected results. |
| `vectors.json` | `tools/launcher-mcp/dump.cpp` (the C++ code) | Every scenario with the result of `launcher/mcp_server.h` and `launcher/redact.h`, plus every `Redact` and `RedactWith` call that `tools/launcher_mcp_test.cpp` makes. |

Do not edit `vectors.json` by hand.

## Rewrite or check the vectors (C++ side)

```
cmake -S tools/launcher-mcp -B build-mcp
cmake --build build-mcp --config Release --target launcher_mcp_dump
build-mcp/Release/launcher_mcp_dump launcher-rs/tests/mcp-vectors/scenarios.json launcher-rs/tests/mcp-vectors/vectors.json
```

`ctest` in that build folder has a test, `launcher_mcp_vectors`, that regenerates the vectors in memory and fails when
they differ from the committed file. The dump also runs the whole C++ check suite first (its `main` is renamed in a
copy that CMake writes into the build folder), so a failing C++ check stops it.

## Replay (Rust side)

```
cd launcher-rs
cargo test -p scol-redact -p scol-mcp
```

- `crates/scol-redact/tests/vectors.rs` redacts every input and compares the output and the learned lists
  (`priority`, `learned`, `legacy`, and the two dropped counters).
- `crates/scol-mcp/tests/vectors.rs` sends every request line to the Rust server and compares the reply line, byte for
  byte, then runs the stdio framing cases through `serve`.

## File format

`vectors.json` has `redact`, `sessions` and `serve` arrays, one entry per line.

A *text* value is a string, `{"hex":"..."}` (bytes that are not valid UTF-8), `{"repeat":{"unit":T,"count":N}}`,
`{"lines":{"template":"... {n} ...","from":A,"to":B}}` (one line per `n`) or an array of those (joined).

- `redact` entry: `strict`, `roots`, `names`, optional `lists` (identity lists set directly), optional `learn` (texts
  given to `Learn` in order), `inputs`, and `expected` with `outputs` (and, when `learn` is present, the learned lists
  and `priority_dropped`, `legacy_dropped`). `key_pass: true` means the input goes through the identity-key pass alone.
- `sessions` entry: `platform` (the fake: `version`, `profile_roots`, `account_names`, `game_log`, `status`, `plugins`,
  `logs`, `heads`, `head_size`, `scan_error`, `scan_chunk`, `settings`) and `steps`. A step is `{"request": T, "reply": ...}`
  (`reply` is `null` when the server gives none) or `{"update": {...}}`, which replaces those platform keys that the
  platform functions read at call time (`status`, `plugins`, `logs`, `heads`, `head_size`, `scan_error`, `scan_chunk`,
  `settings`) before the next request. A platform key that is left out reads as "nothing there".
- `serve` entry: `platform`, `input`, `chunk` (bytes per read), optional `stop_after` (the writer fails after that many
  replies) and `output` (the reply lines).

## What the vectors cover

The C++ suite's own redaction checks; learned identities from login lines in relaxed and strict mode, list limits and
dropped counters; path, e-mail, SID, account name and id shapes; NUL bytes, invalid UTF-8 and non-ASCII names;
deterministic fuzz text and byte soup; protocol errors (parse errors with byte offsets, bad ids, batch, depth, value and
member limits, oversize lines); tool argument validation; `get_status`, `list_plugins` (sorting, the 256 cap, field
clipping, warnings), `get_settings` (the 1000 cap, clipping, errors), `read_log` (skipped older bytes, the 64 KiB cap,
filters, session learning, every notice, Game.log with and without `--game-log`); stdio framing across chunk sizes.

## Intentional differences from the C++ launcher

Everything the C++ server sends on stdout is identical. These are the places where the Rust code is not a line-for-line
copy, with the reason.

1. **JSON.** `scol-mcp` has its own parser and serializer (`src/json.rs`), not `serde_json`. The replies depend on a number
   keeping its token, an object keeping order, duplicate keys replacing in place, and the parser's own error sentences and
   byte offsets. `serde_json` is used only in the tests, to read the vector files.
2. **Bytes.** Text is `Vec<u8>` all through, as `std::string` is in C++, so invalid UTF-8 takes the same path (it becomes
   U+FFFD only when a string is serialized, or where the C++ calls `SanitizeUtf8`). `strchr(set, 0)` finds the terminator in C,
   so a NUL byte counts as "in the set" in a few places; the port keeps that.
3. **plugin.ini.** `list_plugins` reports the raw top-level `key = value` fields (keys lower-cased, the last duplicate wins,
   no inline comments, `api` and `entry` included). `scol_config::plugin_ini::parse_plugin_ini` is a different reader (it
   defaults `name` to the folder, strips inline comments, keeps typed `[settings]`, has no `api` or `entry`), so it cannot give
   these results and is not used. The plugin counts in `get_status` come from `scol-install` (`collect_status`).
4. **`game_running`.** The C++ server calls `GameRunningIn`, which counts a `StarCitizen.exe` it cannot open as not
   running. `scol-install` has only the conservative `game_may_be_running_in` (a process that cannot be identified counts as
   running), and `HostPlatform` uses that. The two answers differ only for a game process this program may not inspect.
   With no game folder known the answer is "not running" in both.
5. **Command line messages.** `run` takes the arguments after the word `mcp`. A bad argument prints the usage and
   `[!] unknown argument '...'` on stderr and returns 1 (as the C++ does in `mcp` mode). `--game` with no folder also writes to
   stderr (the C++ `Fail()` writes to stdout before the protocol starts, where no client reads it). `--help`, `-h` and `/?`
   print the usage on stdout and return 0; the C++ launcher also prints a banner line with the version first.
6. **No stdout redirection.** The C++ `RunMcp` points file descriptor 1 at stderr for the whole session, because
   `ReadConfig` and the game search print to stdout. The Rust server gets its three streams from the caller and nothing else
   writes to stdout: `collect_status` is given a sink. The C++ status check also prints `sc-offline.ini` diagnostics
   (`[!] sc-offline.ini line N: ...`) on stderr; the Rust server prints only its banner line there. Stdout is the same.
   The CLI must pass the real stdout to `run` and print nothing else on it during a session.
7. **Names of the platform folders.** `HostPlatform` lists `data\plugins` with `std::fs::read_dir`, the C++ with
   `FindFirstFileW`. Both give directory order before the sort; the first 4096 names are looked at, as in C++. Junctions and directory symlinks count as plugin folders (and a `disabled` marker is followed), as in C++. A folder name
   that is not valid Unicode is converted lossily (U+FFFD), where `Narrow` makes UTF-8 from UTF-16 with the Windows rules.
8. **Unset platform functions.** In C++ an unset `std::function` reads as "nothing there"; the Rust trait gives each
   function a default that returns the empty value, with the same effect.

Not covered: any Windows firewall, registry or process check beyond what `scol-install` already does.
