// SPDX-License-Identifier: GPL-3.0-only
//! The command line of the sc-offline Rust launcher: `play` (the default), `install`, `uninstall`,
//! `status`, `mcp [--game-log]`, `update`, `stop [--end]`, `report [--no-game-log]`, `plugins ...`, `help`,
//! and the hidden `--helper`, `--delete-logs`, `--self-test` and `--elevated-update`.
//!
//! The argument rules, the usage text, the messages and the exit codes are those of `sc-offline.exe`
//! (`wmain` in `launcher/launcher.cpp`), with three commands the C++ launcher only has as window buttons or
//! not at all: `stop`, `report` and `plugins`. Their usage lines are the only lines [`USAGE`] has that the
//! C++ usage lacks ([`CPP_USAGE_LINES_NOT_HERE`]); the parity scripts and tests drop exactly those.
//! `report` writes a redacted zip of the status check and the logs into `data\reports` and sends nothing.
//! `status` and `mcp` are read-only and offline. `play`, `install` and `uninstall` run `scol_play`'s flow
//! over a [`Backend`]; the binary uses the real one, the tests a fake with a stand-in game process.
//! `update` runs `scol_update` (offer, install, restart into the new exe); `stop` runs the stop channel and
//! then WM_CLOSE; `plugins ...` runs `scol_plugin_manager` (the catalogue over WinHTTP when a trust key is
//! built in, none is yet); `--window`, or no arguments when double-clicked, opens the `scol_ui` window.
//! Nothing here was run in game.

#![cfg(windows)]

pub mod testing;
pub mod window;

use scol_contract::exit_code;
use scol_install::status::{fail, run_status};
use scol_install::version::{SCO_BASED_ON, SCO_VERSION};
use scol_mcp::{run_in as run_mcp_in, Io as McpIo};
use scol_play::report::ReportArgs;
use scol_play::{Mode, PlayArgs};
use std::io::{Read, Write};

/// `kUsage`, word for word.
pub const USAGE: &str = "usage: sc-offline.exe [command] [--game <folder>] [--dry-run] [--skip-eac-check]\n\
\n\
\x20 play       (default) copy the mod in, start the game, take the mod out when it closes\n\
\x20 install    copy the mod in and leave it there; run `uninstall` before going online\n\
\x20 uninstall  take the mod out and restore anything it replaced\n\
\x20 status     check the setup and report; read-only (no writes, no network)\n\
\x20 mcp        MCP server on stdin/stdout for AI agents; read-only (status, plugins, logs, settings)\n\
\x20            add --game-log to let it read Game.log too (off by default: it can hold account names)\n\
\x20 update     check GitHub for a newer sc-offline and offer to install it (saves and ini kept)\n\
\x20 stop       ask the running game to close (the mod saves first); --end ends it if it is still open a minute later\n\
\x20 report     zip the status check and the logs into data\\reports for a bug report; names replaced, nothing sent\n\
\x20 plugins    list, enable, disable, install, remove and set up plugins, and the catalogue; `plugins` alone shows how\n\
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

/// The usage lines of [`USAGE`] the C++ launcher's usage does not have: the C++ launcher has no `stop`
/// command (its window has a Stop button), no `report` and no `plugins` command. Parity checks drop
/// exactly these.
pub const CPP_USAGE_LINES_NOT_HERE: [&str; 3] = [
    "  stop       ask the running game to close (the mod saves first); --end ends it if it is still open a minute later",
    "  report     zip the status check and the logs into data\\reports for a bug report; names replaced, nothing sent",
    "  plugins    list, enable, disable, install, remove and set up plugins, and the catalogue; `plugins` alone shows how",
];

/// [`USAGE`] as the C++ launcher prints it: the lines above removed.
pub fn usage_without_rust_only_lines(text: &str) -> String {
    text.split_inclusive('\n')
        .filter(|l| !CPP_USAGE_LINES_NOT_HERE.contains(&l.trim_end_matches('\n')))
        .collect()
}

/// The words of the commands (the C++ launcher's, `stop` and `report`; `plugins` is taken before parsing).
const COMMANDS: [&str; 9] = [
    "play",
    "install",
    "uninstall",
    "status",
    "update",
    "stop",
    "report",
    "help",
    "mcp",
];

fn is(a: &str, word: &str) -> bool {
    a.eq_ignore_ascii_case(word)
}

/// What the command line asked for.
#[derive(Debug, PartialEq, Eq)]
pub enum Parsed {
    /// The command (lower case), the `--game` value (empty when absent) and the flags.
    Run {
        command: String,
        game: String,
        dry: bool,
        skip_eac: bool,
        game_log: bool,
        /// `--end`, with `stop` only.
        end: bool,
    },
    /// Parsing stopped with this message. `usage`: print the usage first (on stderr when `to_stderr`, which
    /// is what the C++ launcher does for `mcp`, whose stdout belongs to the protocol).
    Stop {
        usage: bool,
        to_stderr: bool,
        message: String,
    },
}

/// The argument loop of `wmain`: `--game <folder>`, `--dry-run`, `--console`, `--skip-eac-check`,
/// `--game-log`, one command word, `--help`, `-h` and `/?`. Anything else is an unknown argument.
pub fn parse_args(args: &[String]) -> Parsed {
    let mut command = "play".to_string();
    let mut game = String::new();
    let (mut dry, mut skip_eac, mut game_log, mut saw_command) = (false, false, false, false);
    let mut end = false;
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        if is(a, "--game") {
            if i + 1 >= args.len() {
                return Parsed::Stop {
                    usage: false,
                    to_stderr: false,
                    message:
                        "--game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\""
                            .to_string(),
                };
            }
            i += 1;
            game = args[i].clone();
        } else if is(a, "--dry-run") {
            dry = true;
        } else if is(a, "--console") {
            // accepted: `play` runs in this console, which is what this binary always does
        } else if is(a, "--skip-eac-check") {
            skip_eac = true;
        } else if is(a, "--game-log") {
            game_log = true; // checked below: only `mcp` takes it
        } else if is(a, "--end") {
            end = true; // checked below: only `stop` takes it
        } else if !saw_command && COMMANDS.iter().any(|c| is(a, c)) {
            command = a.to_ascii_lowercase();
            saw_command = true;
        } else if is(a, "--help") || is(a, "-h") || is(a, "/?") {
            command = "help".to_string();
            saw_command = true;
        } else {
            // `mcp` anywhere: stdout belongs to the protocol, so the usage goes to stderr
            let mcp_mode = args.iter().any(|x| is(x, "mcp"));
            return Parsed::Stop {
                usage: true,
                to_stderr: mcp_mode,
                message: format!("unknown argument '{a}'"),
            };
        }
        i += 1;
    }
    if game_log && command != "mcp" {
        return Parsed::Stop {
            usage: true,
            to_stderr: false,
            message:
                "unknown argument '--game-log' (it belongs to `sc-offline.exe mcp --game-log`)"
                    .to_string(),
        };
    }
    if end && command != "stop" {
        return Parsed::Stop {
            usage: true,
            to_stderr: false,
            message: "unknown argument '--end' (it belongs to `sc-offline.exe stop --end`)"
                .to_string(),
        };
    }
    Parsed::Run {
        command,
        game,
        dry,
        skip_eac,
        game_log,
        end,
    }
}

/// What the commands that change things need from the machine. The binary uses `RealBackend`; tests
/// use [`testing::FakeBackend`].
pub trait Backend {
    /// `play`, `install` or `uninstall`.
    fn play(&mut self, args: &PlayArgs, here: &str, out: &mut dyn Write) -> i32;
    /// The hidden `--helper` mode; `args` are the arguments after the exe name.
    fn helper(&mut self, args: &[String], here: &str, out: &mut dyn Write) -> i32;
    /// The hidden `--delete-logs <channel folder> <since>` mode.
    fn delete_logs(&mut self, channel_dir: &str, since: &str) -> i32;
    /// `update [--check] [--yes] [--prerelease]` and the hidden `--elevated-update <tag> <staged>`; `args`
    /// start at the command word or the hidden option.
    fn update(&mut self, args: &[String], here: &str, out: &mut dyn Write) -> i32;
    /// `stop [--game <folder>] [--end]`: the stop channel, then WM_CLOSE, then (after a minute) the End step.
    fn stop(&mut self, args: &StopArgs, here: &str, out: &mut dyn Write) -> i32;
    /// `report [--game <folder>] [--no-game-log]`: the redacted problem report in `data\reports`.
    fn report(&mut self, args: &ReportArgs, here: &str, out: &mut dyn Write) -> i32;
    /// Opens the window; returns when it closes. `free_console`: this exe owns its console (double-click).
    fn window(&mut self, here: &str, free_console: bool) -> i32;
    /// True when no arguments should open the window: double-clicked, not under Wine, not a window child.
    fn wants_window(&self) -> bool {
        false
    }
}

/// What `stop` was asked.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StopArgs {
    /// `--game <folder>`; empty when absent.
    pub game: String,
    /// `--end`: end the game process without asking when it is still open after the wait.
    pub end: bool,
}

/// The three streams of the process.
pub struct Streams<'a> {
    pub stdin: &'a mut dyn Read,
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
}

/// Runs the command line. `here` is the folder of the executable (where `sc-offline.ini`, `dinput8.dll`
/// and `data\` live). Returns the exit code.
pub fn run_with(
    args: &[String],
    here: &str,
    io: &mut Streams<'_>,
    backend: &mut dyn Backend,
) -> i32 {
    // Hidden modes, matched by exact argument count like the C++ `wmain`.
    if args.len() == 2 && is(&args[0], "--self-test") {
        return scol_play::self_test(&args[1], here, io.stdout);
    }
    if (args.len() == 6 || args.len() == 7) && is(&args[0], "--helper") {
        return backend.helper(args, here, io.stdout);
    }
    if args.len() == 3 && is(&args[0], "--delete-logs") {
        return backend.delete_logs(&args[1], &args[2]);
    }
    if args.len() == 3 && is(&args[0], "--elevated-update") {
        return backend.update(args, here, io.stdout);
    }
    // Double-clicked with no arguments, or `--window`: the window.
    if (args.is_empty() && backend.wants_window()) || (args.len() == 1 && is(&args[0], "--window"))
    {
        return backend.window(here, args.is_empty());
    }
    if !args.is_empty() && is(&args[0], "plugins") {
        return run_plugins(&args[1..], here);
    }
    // `report [--game <folder>] [--no-game-log]`: its own flag, so it is read here. Anything else after
    // `report` goes through the common rules below and ends as an unknown argument.
    if !args.is_empty() && is(&args[0], "report") {
        if let Some(report) = report_args(&args[1..]) {
            return backend.report(&report, here, io.stdout);
        }
    }
    if !args.is_empty()
        && is(&args[0], "update")
        && args[1..].iter().all(|a| {
            ["--check", "--yes", "--prerelease"]
                .iter()
                .any(|f| is(a, f))
        })
    {
        return backend.update(args, here, io.stdout);
    }
    let (command, game, dry, skip_eac, game_log, end) = match parse_args(args) {
        Parsed::Run {
            command,
            game,
            dry,
            skip_eac,
            game_log,
            end,
        } => (command, game, dry, skip_eac, game_log, end),
        Parsed::Stop {
            usage,
            to_stderr,
            message,
        } => {
            if to_stderr {
                // no Fail(): it writes to stdout
                let _ = writeln!(io.stderr, "{USAGE}[!] {message}");
                return exit_code::ERROR;
            }
            if usage {
                let _ = write!(io.stdout, "{USAGE}");
            }
            return fail(io.stdout, &message);
        }
    };
    match command.as_str() {
        "help" => {
            let _ = write!(
                io.stdout,
                "sc-offline launcher {SCO_VERSION} ({SCO_BASED_ON})\n\n{USAGE}"
            );
            exit_code::OK
        }
        "status" => run_status(here, &game, io.stdout), // before anything that writes
        "mcp" => {
            // likewise: nothing may be written, and stdout belongs to the protocol
            let mut mcp_args: Vec<String> = Vec::new();
            if !game.is_empty() {
                mcp_args.push("--game".into());
                mcp_args.push(game);
            }
            if game_log {
                mcp_args.push("--game-log".into());
            }
            run_mcp_in(
                std::path::Path::new(here),
                &mcp_args,
                &mut McpIo {
                    stdin: io.stdin,
                    stdout: io.stdout,
                    stderr: io.stderr,
                },
            )
        }
        "update" => backend.update(&["update".to_string()], here, io.stdout),
        "stop" => backend.stop(&StopArgs { game, end }, here, io.stdout),
        "report" => backend.report(
            &ReportArgs {
                game,
                game_log: true,
            },
            here,
            io.stdout,
        ),
        "play" | "install" | "uninstall" => {
            let play = PlayArgs {
                mode: match command.as_str() {
                    "install" => Mode::Install,
                    "uninstall" => Mode::Uninstall,
                    _ => Mode::Play,
                },
                game_arg: game,
                dry,
                skip_eac,
                command_line: args.to_vec(),
            };
            backend.play(&play, here, io.stdout)
        }
        other => fail(io.stdout, &format!("unknown command '{other}'")),
    }
}

/// The arguments after `report`: `--game <folder>` and `--no-game-log`, in any order. None when
/// something else is there (the common argument rules then name it).
pub fn report_args(rest: &[String]) -> Option<ReportArgs> {
    let mut r = ReportArgs {
        game: String::new(),
        game_log: true,
    };
    let mut i = 0;
    while i < rest.len() {
        if is(&rest[i], "--no-game-log") {
            r.game_log = false;
        } else if is(&rest[i], "--game") && i + 1 < rest.len() {
            i += 1;
            r.game = rest[i].clone();
        } else {
            return None;
        }
        i += 1;
    }
    Some(r)
}

/// `plugins list|enable|disable|install|remove|catalogue ...`: the plugin manager, rooted at the launcher
/// folder unless `--root` is given.
fn run_plugins(rest: &[String], here: &str) -> i32 {
    let mut a: Vec<String> = rest.to_vec();
    if !a.iter().any(|x| x == "--root") {
        a.push("--root".into());
        a.push(here.to_string());
    }
    scol_plugin_manager::run_net(&a, Some(catalogue_net))
}

/// The network client of the plugin catalogue: WinHTTP, like the window's.
fn catalogue_net() -> Box<dyn scol_catalogue::fetch::Fetcher> {
    Box::new(scol_update::http::catalogue_fetcher(SCO_VERSION))
}

/// What `update` and the automatic check before `play` need from the machine: the real one, plus the two
/// things only a command line knows. Whether anybody can answer a question (a console, never the
/// window's hidden child, whose button is the question), and what follows an installed update: a console
/// run starts the new exe with the same arguments and ends with its code, the window's child ends with
/// the restart code so the window restarts itself.
struct CliMachine {
    inner: scol_update::platform::System,
    /// This process is the window's child (`SC_OFFLINE_GUI`).
    window_child: bool,
}

impl CliMachine {
    fn new() -> Self {
        CliMachine {
            inner: scol_update::platform::System,
            window_child: std::env::var_os(scol_contract::env::GUI).is_some(),
        }
    }
}

impl scol_update::platform::Platform for CliMachine {
    fn game_running(&mut self) -> bool {
        self.inner.game_running()
    }
    fn run_self_test(&mut self, here: &std::path::Path, tag: &str) -> Result<(), String> {
        // Normal rights even when this process is elevated: `System` picks the token by elevation.
        self.inner.run_self_test(here, tag)
    }
    fn can_write(&mut self, dir: &std::path::Path) -> bool {
        self.inner.can_write(dir)
    }
    fn run_elevated(
        &mut self,
        here: &std::path::Path,
        tag: &str,
        staged: &std::path::Path,
    ) -> Result<i32, String> {
        self.inner.run_elevated(here, tag, staged)
    }
    fn free_space(&mut self, dir: &std::path::Path) -> Option<u64> {
        self.inner.free_space(dir)
    }
    fn local_app_data(&mut self) -> Option<std::path::PathBuf> {
        self.inner.local_app_data()
    }
    fn sleep_ms(&mut self, ms: u64) {
        self.inner.sleep_ms(ms)
    }
    fn can_ask(&mut self) -> bool {
        use std::io::IsTerminal;
        !self.window_child && std::io::stdin().is_terminal()
    }
    fn after_update(
        &mut self,
        here: &std::path::Path,
        args: &[String],
        out: &mut dyn Write,
    ) -> Option<i32> {
        if self.window_child {
            let _ = writeln!(out, "[i] restarting sc-offline with the new version");
            return Some(exit_code::RESTART);
        }
        let _ = write!(out, "[i] starting the new version\n\n");
        let _ = out.flush();
        match scol_update::platform::relaunch(here, args) {
            Ok(code) => Some(code),
            Err(e) => {
                let _ = writeln!(out, "[!] {e}; run sc-offline.exe again");
                Some(exit_code::ERROR)
            }
        }
    }
}

/// The question on the console: `AskYes`. No console, no answer.
fn ask_on_console(q: &str) -> bool {
    use std::io::IsTerminal;
    if !std::io::stdin().is_terminal() {
        return false;
    }
    print!("{q} [y/N] ");
    let _ = std::io::stdout().flush();
    let mut l = String::new();
    std::io::stdin().read_line(&mut l).is_ok() && l.trim().to_ascii_lowercase().starts_with('y')
}

/// `sc-offline.ini` next to the exe: the settings, or the defaults when it is missing. Messages about it
/// are not printed here (`status` and `play` print them).
fn ini_config(here: &str) -> scol_config::Config {
    scol_config::read_config(std::path::Path::new(&format!(
        "{here}\\{}",
        scol_contract::files::INI
    )))
    .map(|p| p.config)
    .unwrap_or_default()
}

/// How long `stop` waits, and what it does with a game that stays open.
#[derive(Debug, Clone, Copy)]
pub struct StopTiming {
    pub ack_wait_ms: u32,
    pub wait_seconds: u32,
    pub end: scol_play::stop::EndPolicy,
}

impl StopTiming {
    /// The C++ window's: ten seconds for the mod, a minute for the game.
    pub fn real(end: scol_play::stop::EndPolicy) -> Self {
        StopTiming {
            ack_wait_ms: scol_play::stop::STOP_ACK_WAIT_MS,
            wait_seconds: scol_play::stop::STOP_WAIT_SECONDS,
            end,
        }
    }
}

/// `stop`: the game folder (`--game`, `game =`, discovery; nothing is searched for with a picker), then the
/// stop channel and WM_CLOSE over `port`, the wait, and the End step, as [`scol_play::stop`] does. Lines of
/// the stop go to `data\launcher.log` as `[stop hh:mm:ss.mmm] ...`.
pub fn run_stop(
    args: &StopArgs,
    here: &str,
    port: &mut dyn scol_play::stop::StopPort,
    timing: StopTiming,
    ask: &mut dyn FnMut(&str) -> bool,
    out: &mut dyn Write,
) -> i32 {
    let cfg = ini_config(here);
    let bin = match scol_install::status::find_game(here, &args.game, &cfg, out) {
        Ok((bin, _)) => bin,
        Err(code) => return code,
    };
    scol_play::stop::run_stop_command(
        port,
        &scol_play::stop::StopCommand {
            bin: &bin,
            ack_wait_ms: timing.ack_wait_ms,
            wait_seconds: timing.wait_seconds,
            end: timing.end,
        },
        ask,
        out,
        &mut |l| scol_play::real::stop_log(here, l),
    )
}

/// The backend of the real PC: starts the helper process, asks for administrator rights, starts the game.
pub struct RealBackend;

impl Backend for RealBackend {
    fn play(&mut self, args: &PlayArgs, here: &str, out: &mut dyn Write) -> i32 {
        // The automatic check before `play` (`check_updates = on`): a short request. When one is on offer
        // the player is asked, like in the C++ launcher; yes installs it and the new exe takes over with
        // this command line, no (or nobody to ask) goes on playing. The window's child only reports it:
        // the window has its own Update button.
        let line = args.command_line.clone();
        let here_owned = here.to_string();
        let hook: scol_play::real::UpdateHook = Box::new(move |cfg, out| {
            let mut net = scol_update::http::WinHttp::new(SCO_VERSION, 3_000);
            let mut machine = CliMachine::new();
            let code = scol_update::flow::check_and_offer_for_play(
                SCO_VERSION,
                cfg.prereleases,
                std::path::Path::new(&here_owned),
                &line,
                &mut net,
                &mut machine,
                out,
                &mut ask_on_console,
            );
            if code.is_none() {
                let _ = writeln!(out);
            }
            code
        });
        scol_play::real::run_command_with(args, here, out, Some(hook))
    }
    fn helper(&mut self, args: &[String], here: &str, out: &mut dyn Write) -> i32 {
        scol_play::real::run_helper(args, here, out)
    }
    fn delete_logs(&mut self, channel_dir: &str, since: &str) -> i32 {
        scol_play::real::run_delete_logs(channel_dir, since)
    }
    fn update(&mut self, args: &[String], here: &str, out: &mut dyn Write) -> i32 {
        // `update` asks the network whatever `check_updates` says (that key gates the automatic check
        // before `play`); the channel comes from `update_channel`.
        let cfg = ini_config(here);
        let ctx = scol_update::Context {
            here: here.into(),
            version: SCO_VERSION.to_string(),
            prerelease: cfg.prereleases,
            check_updates: cfg.check_updates,
        };
        let mut net = scol_update::http::WinHttp::new(SCO_VERSION, 15_000);
        scol_update::run_with(
            args,
            &ctx,
            &mut net,
            &mut CliMachine::new(),
            out,
            &mut ask_on_console,
        )
    }
    fn stop(&mut self, args: &StopArgs, here: &str, out: &mut dyn Write) -> i32 {
        use std::io::IsTerminal;
        let policy = if args.end {
            scol_play::stop::EndPolicy::Yes
        } else if std::io::stdin().is_terminal() {
            scol_play::stop::EndPolicy::Ask
        } else {
            scol_play::stop::EndPolicy::Never
        };
        let mut port = scol_play::real::RealStop::default();
        run_stop(
            args,
            here,
            &mut port,
            StopTiming::real(policy),
            &mut ask_on_console,
            out,
        )
    }
    fn report(&mut self, args: &ReportArgs, here: &str, out: &mut dyn Write) -> i32 {
        scol_play::report::make_report(&mut scol_play::real::RealHost::new(), out, here, args)
    }
    fn window(&mut self, here: &str, free_console: bool) -> i32 {
        window::open_window(here, free_console)
    }
    fn wants_window(&self) -> bool {
        std::env::var_os(scol_contract::env::GUI).is_none()
            && window::owns_console()
            && !scol_install::on_wine()
    }
}

/// [`run_with`] on the real PC with the process's own streams.
pub fn run_process(args: &[String], here: &str) -> i32 {
    let stdin = std::io::stdin();
    let mut stdin = stdin.lock();
    let stdout = std::io::stdout();
    let mut stdout = stdout.lock();
    let mut stderr = std::io::stderr();
    let code = run_with(
        args,
        here,
        &mut Streams {
            stdin: &mut stdin,
            stdout: &mut stdout,
            stderr: &mut stderr,
        },
        &mut RealBackend,
    );
    let _ = stdout.flush();
    code
}

/// Runs the command line with the real backend, writing to `out` (stdin is the process's own, stderr too).
/// For `help`, `status` and argument errors, which never reach the backend.
pub fn run(args: &[String], here: &str, out: &mut dyn Write) -> i32 {
    let mut stdin = std::io::stdin().lock();
    let mut stderr = std::io::stderr();
    run_with(
        args,
        here,
        &mut Streams {
            stdin: &mut stdin,
            stdout: out,
            stderr: &mut stderr,
        },
        &mut RealBackend,
    )
}

/// The entry point of the `sc-offline` binary (and its `scol` alias): never returns.
pub fn main_entry() -> ! {
    let args: Vec<String> = std::env::args_os()
        .skip(1)
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    // `ExeDir`: the folder of this executable; "." when it cannot be told.
    let here = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_string_lossy().into_owned()))
        .unwrap_or_else(|| ".".to_string());
    std::process::exit(run_process(&args, &here));
}
