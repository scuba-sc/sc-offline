// SPDX-License-Identifier: GPL-3.0-only
//! The `mcp` command: argument rules, the stdio loop and the exit code.
//!
//! The C++ launcher redirects file descriptor 1 to stderr for the whole session so that nothing but
//! JSON-RPC reaches stdout. A Rust program has no hidden writer to stdout, so here the caller hands in the
//! three streams, and this code writes to `stdout` only protocol lines (and the help text of `--help`).

use std::io::{Read, Write};

use crate::server::{serve, Platform, Server};

/// `kUsage` of the C++ launcher, word for word.
pub const USAGE: &str = "usage: sc-offline.exe [command] [--game <folder>] [--dry-run] [--skip-eac-check]\n\
\n\
\x20 play       (default) copy the mod in, start the game, take the mod out when it closes\n\
\x20 install    copy the mod in and leave it there; run `uninstall` before going online\n\
\x20 uninstall  take the mod out and restore anything it replaced\n\
\x20 status     check the setup and report; read-only (no writes, no network)\n\
\x20 mcp        MCP server on stdin/stdout for AI agents; read-only (status, plugins, logs, settings)\n\
\x20            add --game-log to let it read Game.log too (off by default: it can hold account names)\n\
\x20 update     check GitHub for a newer sc-offline and offer to install it (saves and ini kept)\n\
\x20 help       show this\n\
\n\
\x20 --game <folder>   your StarCitizen, LIVE or Bin64 folder (else sc-offline.ini, else searched)\n\
\x20 --dry-run         print every step, change nothing, start nothing\n\
\x20 --skip-eac-check  don't stop when Easy Anti-Cheat looks active\n\
\x20 --game-log        with mcp only: allow read_log to serve the game's Game.log\n\
\x20 --console         double-clicked: run `play` in this console instead of opening the window\n\
\x20 --window          open the window (from a terminal, or under Wine/Proton)\n\
\n\
exit codes: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running\n";

/// The three streams of the process.
pub struct Io<'a> {
    pub stdin: &'a mut dyn Read,
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
}

fn is(a: &str, word: &str) -> bool {
    a.eq_ignore_ascii_case(word)
}

/// Runs `sc-offline.exe mcp <args>` over the platform that `make_platform(game_log)` returns. `args` are the
/// arguments after the word `mcp`. Returns the exit code: 0 at the end of input, 1 for a bad argument.
///
/// Argument rules, as in `wmain` of the C++ launcher: `--game-log` lets `read_log` serve Game.log;
/// `--game <folder>` is ignored with a note on stderr (the folder comes from `sc-offline.ini`);
/// `--dry-run`, `--console` and `--skip-eac-check` are accepted and ignored; `--help`, `-h` and `/?` print the
/// usage on stdout; anything else prints the usage and `[!] unknown argument` on stderr and returns 1.
pub fn run_with(
    args: &[String],
    io: &mut Io<'_>,
    make_platform: &mut dyn FnMut(bool) -> Box<dyn Platform>,
) -> i32 {
    let mut game_log = false;
    let mut game_arg = String::new();
    let mut help = false;
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        if is(a, "--game") {
            if i + 1 >= args.len() {
                let _ = writeln!(
                    io.stderr,
                    "[!] --game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\""
                );
                return 1;
            }
            i += 1;
            game_arg = args[i].clone();
        } else if is(a, "--dry-run") || is(a, "--console") || is(a, "--skip-eac-check") {
        } else if is(a, "--game-log") {
            game_log = true;
        } else if is(a, "--help") || is(a, "-h") || is(a, "/?") {
            help = true;
        } else {
            let _ = writeln!(io.stderr, "{USAGE}[!] unknown argument '{a}'");
            return 1;
        }
        i += 1;
    }
    if help {
        if game_log {
            let _ = writeln!(
                io.stderr,
                "{USAGE}[!] unknown argument '--game-log' (it belongs to `sc-offline.exe mcp --game-log`)"
            );
            return 1;
        }
        let _ = write!(io.stdout, "{USAGE}");
        let _ = io.stdout.flush();
        return 0;
    }
    if !game_arg.is_empty() {
        let _ = writeln!(
            io.stderr,
            "sc-offline mcp: --game is ignored; set game = in sc-offline.ini"
        );
    }
    let platform = make_platform(game_log);
    let _ = writeln!(
        io.stderr,
        "sc-offline mcp: read-only server on stdio, {}",
        platform.version()
    );
    let mut server = Server::new(platform);
    let Io { stdin, stdout, .. } = io;
    serve(
        &mut server,
        &mut |buf: &mut [u8]| loop {
            match stdin.read(buf) {
                Ok(n) => return n, // end of input is 0; a read error ends the session below
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => return 0,
            }
        },
        &mut |line: &[u8]| {
            let mut out = Vec::with_capacity(line.len() + 1);
            out.extend_from_slice(line);
            out.push(b'\n');
            stdout.write_all(&out).is_ok() && stdout.flush().is_ok() // the client went away: stop
        },
    );
    0
}

/// `sc-offline.exe mcp <args>` on this PC: the platform reads the install next to the running exe.
#[cfg(windows)]
pub fn run(args: &[String], io: &mut Io<'_>) -> i32 {
    let here = match std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
    {
        Some(d) => d,
        None => {
            let _ = writeln!(
                io.stderr,
                "[!] sc-offline mcp: the folder of this program is not known"
            );
            return 1;
        }
    };
    run_in(&here, args, io)
}

/// Like [`run`] for an install in `here` (the folder that holds `sc-offline.ini` and `data`).
#[cfg(windows)]
pub fn run_in(here: &std::path::Path, args: &[String], io: &mut Io<'_>) -> i32 {
    let here = here.to_path_buf();
    run_with(args, io, &mut |game_log| {
        Box::new(crate::host::HostPlatform::new(&here, game_log))
    })
}
