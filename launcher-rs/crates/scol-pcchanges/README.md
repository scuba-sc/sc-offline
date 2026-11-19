<!-- SPDX-License-Identifier: GPL-3.0-only -->
# scol-pcchanges

The PC changes of the sc-offline launcher, ported from `launcher/launcher.cpp` to Rust. It is copied
exactly and never widened. `scol-play` calls it, and only `scol-cli` builds its real system. It is tested with a fake system.

None of this has been run in game; see [docs/status.md](../../../docs/status.md).

## What it changes on the PC

While you play, the launcher's helper makes these changes and undoes them when the game closes.
Each is a switch in `sc-offline.ini` and each is recorded as it is made.

| Change | Switch | Record key | Undone by |
| --- | --- | --- | --- |
| Windows Firewall rules that block `StarCitizen.exe`, `RSI Launcher.exe` and `CrashHandler.exe`, in and out (`netsh advfirewall`) | `block_network` | `firewall`, `fw_launcher`, `fw_crash` | `netsh ... delete rule` |
| One firewall rule that lets the LAN (and `multiplayer_allow`) reach `StarCitizen.exe` over UDP; the game block rule then leaves those addresses out | `multiplayer` | `fw_mp` | `netsh ... delete rule` |
| The hosts line `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` plus a tag comment | `eac_hosts` | `hosts` | removing only lines that carry the tag, then `ipconfig /flushdns` |
| `EasyAntiCheat_EOS.exe` renamed to `EasyAntiCheat_EOS.exe.bak` | `eac_rename` | `eac` | renaming it back, only if the `.bak` exists and the exe does not |

The record is `%ProgramData%\sc-offline\pc-changes.txt`. Only recorded changes are undone: a hosts
line or a `.bak` you made yourself is left alone. The record is deleted only when every undo step
worked; otherwise it stays, and any later run (play, status, uninstall) finds it.

## Security notes

This crate manipulates the anti-cheat setup on purpose. It turns Easy Anti-Cheat off by renaming its
executable and by stopping the RSI Launcher from downloading it again (the hosts line). It also
blocks the network for the game, the RSI Launcher and CIG's crash uploader. Do not describe these
changes as anything else, and do not say the launcher leaves anti-cheat alone.

- It runs elevated. The helper that calls it runs as administrator whenever PC changes are wanted
  or left over; the game does not.
- It edits `C:\Windows\System32\drivers\etc\hosts` and runs `netsh.exe` and `ipconfig.exe` from
  `%SystemRoot%\System32`. It runs no other program. The folder comes from the `SystemRoot`
  environment variable of the elevated process (fallback `C:\Windows`), exactly as in the C++
  code; it is not looked up with `GetSystemWindowsDirectoryW`. The hosts path uses the same
  variable. Changing that is a flagged change, not a silent one.
- The maintainer reviews this crate before a release that ships it. The C++ launcher keeps the same
  code in `sc-offline-classic.exe` and can undo the changes from the same record.

## Design

Every effect on the machine goes through the `System` trait (`src/system.rs`): run a command line,
read and write a file, rename, delete, query elevation, read the clock, find the RSI Launcher in the
registry. There are two implementations:

- `real::RealSystem` (Windows only, and only with the non-default cargo feature `real-system`,
  which only the CLI binary enables). It runs the programs above. No test runs it. Its only unsafe
  code is the registry, token, module and clock calls, each with a comment.
- `fake::FakeSystem`. It keeps files in memory, answers `netsh show`, `add` and `delete` from a set
  of rule names, logs every call, and can be told to fail a command, read, write or rename.

All tests use the fake and compare exact command lines, file contents and messages. The expected
strings come from the C++ source and from the golden record in `tests/golden/pc-changes`.

## API

- `apply_pc_changes`, `undo_pc_changes`, `describe_pc_changes`, `pc_changes_left`, `record_add`:
  the C++ `ApplyPcChanges`, `UndoPcChanges`, `DescribePcChanges`, `PcChangesLeft` and the record
  lambda. `dry` prints the `[dry-run] would ...` steps and changes nothing.
- `run(args, &mut Io)`: the `--helper` mode. `args` are the arguments after the exe name:
  `--helper <play|install|uninstall> <Bin64> <launcher pid> <event name> <pc flags> [<multiplayer_allow>]`.
  Flags: `f` firewall, `h` hosts, `e` EAC rename, `l` LAN stays open, `-` none (always `-` under
  Wine). Exit codes: 0 ok, 2 mod copy failed, 3 mod removal failed, 4 bad arguments, 5 PC change
  failed. `is_helper_invocation` is the test `wmain` uses to dispatch to it.
- `Io` holds the `System`, the `HelperHooks` and the output. The hooks are what the helper needs from
  the rest of the launcher and this crate does not own: put the mod in, take it out, signal the ready
  event, wait for the game to close. `scol-play` supplies them.
- `plan_helper_start`: the argument line the launcher passes the helper and whether it must run
  elevated (`StartHelper`). Starting the process (`runas` or hidden) is left to the caller.
- `System::is_elevated` is part of the trait for the callers; the PC changes themselves do not
  branch on it, as in the C++ code.

## Differences from the C++ launcher

- Output goes to the `Write` the caller gives, not to the console and `data\launcher.log`.
- `multiplayer_allow` is checked with `scol_config::config::parse_allow_list`, and the ranges are read
  back from its rewritten text.
- A hosts file is edited as bytes, so a non-UTF-8 byte in it survives.
- `write_file` uses the standard library's default share mode; the C++ `WriteAll` opens with share
  mode 0.
- Step text is not cut at 1024 bytes as the C++ `vsnprintf` buffer does.
- `run` starts the program with `std::process::Command`, which lets the child inherit handles
  (`bInheritHandles` TRUE); the C++ `RunTool` passes FALSE. Stdio is set to null. Both ends are
  netsh and ipconfig, started by the elevated helper.
- `run` waits up to 60 seconds and leaves a timed-out child running, as the C++ does.
- `ParentDir` follows `ParentForSuffix`: the path is resolved first (`System::resolve_dirs`, the
  real one is `GetFullPathNameW` / `PathCchCanonicalizeEx`), then the plain split is the fallback.
  The fake resolves with plain text rules (separators, `.`, `..`, trailing backslash) and covers
  the drive-letter paths only; the real one has not been run.

## What is not tested

The real `System` (netsh, ipconfig, hosts edit, rename, registry read, token query, path resolve) is
compiled and linted but no test runs it. The helper's mod copy and process waits are behind
`HelperHooks`, and the process start (runas or hidden) is not in this crate. `scol-play` supplies those. Do not build `RealSystem` anywhere but the program entry point, and never in a test. Two guards
back this up: the `real-system` feature is off by default, and `tests/no_real_system.rs` fails if
any test file in the workspace names `RealSystem`, `real-system` or `scol_pcchanges::real`.
Check the real one by hand on a throwaway machine.

Hand checks (throwaway machine only):
- A Bin64 given as a UNC path (`\\server\share\...`) and as a `\\?\` path: the RSI Launcher
  and CrashHandler.exe rules must name the right folders. The fake covers drive-letter paths only.
- Play, then close the game: every rule, the hosts line and the `.bak` are gone, and the record
  is deleted.

## Check

```
cargo test -p scol-pcchanges
cargo clippy -p scol-pcchanges --lib --features real-system -- -D warnings
cargo clippy -p scol-pcchanges --all-targets -- -D warnings
cargo deny check
```
