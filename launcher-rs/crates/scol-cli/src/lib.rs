// SPDX-License-Identifier: GPL-3.0-only
//! The command line of the sc-offline Rust launcher, first slice: `help` and `status`.
//!
//! The argument rules, the usage text, the messages and the exit codes are those of
//! `sc-offline.exe` (`wmain` in `launcher/launcher.cpp`). `status` is read-only and offline:
//! it writes no file and makes no network request. The commands that change something (play,
//! install, uninstall, update) are not built yet; they say so and exit 1. Nothing here was run in game.

#![cfg(windows)]

use scol_contract::exit_code;
use scol_install::status::{fail, run_status};
use scol_install::version::{SCO_BASED_ON, SCO_VERSION};
use std::io::Write;

/// `kUsage`, word for word.
pub const USAGE: &str = "usage: sc-offline.exe [command] [--game <folder>] [--dry-run] [--skip-eac-check]\n\
\n\
\x20 play       (default) copy the mod in, start the game, take the mod out when it closes\n\
\x20 install    copy the mod in and leave it there; run `uninstall` before going online\n\
\x20 uninstall  take the mod out and restore anything it replaced\n\
\x20 status     check the setup and report; read-only (no writes, no network)\n\
\x20 update     check GitHub for a newer sc-offline and offer to install it (saves and ini kept)\n\
\x20 help       show this\n\
\n\
\x20 --game <folder>   your StarCitizen, LIVE or Bin64 folder (else sc-offline.ini, else searched)\n\
\x20 --dry-run         print every step, change nothing, start nothing\n\
\x20 --skip-eac-check  don't stop when Easy Anti-Cheat looks active\n\
\x20 --console         double-clicked: run `play` in this console instead of opening the window\n\
\x20 --window          open the window (from a terminal, or under Wine/Proton)\n\
\n\
exit codes: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running\n";

/// The words of the commands the C++ launcher knows.
const COMMANDS: [&str; 6] = ["play", "install", "uninstall", "status", "update", "help"];

/// Options the C++ launcher takes only as the first and only argument or as hidden modes. They need
/// the window or the helper process, which this slice does not have.
const NOT_BUILT_OPTIONS: [&str; 5] = [
    "--window",
    "--self-test",
    "--helper",
    "--delete-logs",
    "--elevated-update",
];

fn is(a: &str, word: &str) -> bool {
    a.eq_ignore_ascii_case(word)
}

/// What the command line asked for.
#[derive(Debug, PartialEq, Eq)]
pub enum Parsed {
    /// The command (lower case) and the `--game` value (empty when absent).
    Run { command: String, game: String },
    /// Parsing stopped with this message, after printing the usage when `usage` is set.
    Stop { usage: bool, message: String },
}

/// The argument loop of `wmain`: `--game <folder>`, `--dry-run`, `--console`, `--skip-eac-check`, one
/// command word, `--help`, `-h` and `/?`. Anything else is an unknown argument.
pub fn parse_args(args: &[String]) -> Parsed {
    let mut command = "play".to_string();
    let mut game = String::new();
    let mut saw_command = false;
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        if is(a, "--game") {
            if i + 1 >= args.len() {
                return Parsed::Stop {
                    usage: false,
                    message:
                        "--game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\""
                            .to_string(),
                };
            }
            i += 1;
            game = args[i].clone();
        } else if is(a, "--dry-run") || is(a, "--console") || is(a, "--skip-eac-check") {
            // accepted; none of them changes what status does
        } else if !saw_command && COMMANDS.iter().any(|c| is(a, c)) {
            command = a.to_ascii_lowercase();
            saw_command = true;
        } else if is(a, "--help") || is(a, "-h") || is(a, "/?") {
            command = "help".to_string();
            saw_command = true;
        } else if NOT_BUILT_OPTIONS.iter().any(|o| is(a, o)) {
            return Parsed::Stop {
                usage: false,
                message: format!(
                    "'{a}' is not available in the Rust launcher yet; use sc-offline.exe"
                ),
            };
        } else {
            return Parsed::Stop {
                usage: true,
                message: format!("unknown argument '{a}'"),
            };
        }
        i += 1;
    }
    Parsed::Run { command, game }
}

/// Runs the command line. `here` is the folder of the executable (where `sc-offline.ini`,
/// `dinput8.dll` and `data\` live). Returns the exit code.
pub fn run(args: &[String], here: &str, out: &mut dyn Write) -> i32 {
    let (command, game) = match parse_args(args) {
        Parsed::Run { command, game } => (command, game),
        Parsed::Stop { usage, message } => {
            if usage {
                let _ = write!(out, "{USAGE}");
            }
            return fail(out, &message);
        }
    };
    if command == "help" {
        let _ = write!(
            out,
            "sc-offline launcher {SCO_VERSION} ({SCO_BASED_ON})\n\n{USAGE}"
        );
        return exit_code::OK;
    }
    if command == "status" {
        return run_status(here, &game, out);
    }
    fail(
        out,
        &format!("'{command}' is not available in the Rust launcher yet; use sc-offline.exe (status and help work here)"),
    )
}
