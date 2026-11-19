// SPDX-License-Identifier: GPL-3.0-only
//! `play`, `install` and `uninstall`: the part of `wmain` in `launcher/launcher.cpp` after the argument
//! loop, in the same order and with the same text: the log, the settle of a previous update, the
//! config, the game folder, the self checks, the `SC_OFFLINE_*` environment, the helper (PC changes and
//! the copy of the mod, elevated when the C++ launcher would elevate), the game, the wait, the clean-up
//! when the game exits, the session logs and the crash report offer.
//!
//! Every process and system effect goes through [`Host`]; the PC changes through `scol_pcchanges::System`
//! (`Host::system`). Source, build and synthetic evidence only. Not run in game.

use crate::host::{Host, Ready};
use crate::log::{open_log, Tee};
use crate::post::{clean_session_logs, offer_crash_report};
use crate::settle::settle_previous_update;
use scol_config::env::{apply_capabilities, play_env, PlayPaths};
use scol_config::ini_edit::drop_ini_key;
use scol_config::{read_config, Config, LogChoice};
use scol_contract::{exit_code, files};
use scol_install::fsops::{can_write_to, create_dir};
use scol_install::install::put_mod;
use scol_install::paths::{parent_dir, to_bin64, GamePaths, GAME_EXE};
use scol_install::status::{fail, fail_code, find_game, pause_if_own_console, self_checks};
use scol_install::version::SCO_VERSION;
use scol_pcchanges::changes::game_exe_in;
use scol_pcchanges::helper::needs_pc_changes;
use scol_pcchanges::{apply_pc_changes, plan_helper_start, undo_pc_changes, PcWanted};
use std::io::Write;

/// Which command this is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    /// Copy the mod in, start the game, take the mod out when it closes.
    Play,
    /// Copy the mod in and leave it there.
    Install,
    /// Take the mod out and restore what it replaced.
    Uninstall,
}

/// What the command line asked for (`scol-cli` parses it).
#[derive(Clone, Debug)]
pub struct PlayArgs {
    pub mode: Mode,
    /// `--game <folder>`; empty when absent.
    pub game_arg: String,
    pub dry: bool,
    pub skip_eac: bool,
    /// Every argument as typed, for the first line of `data\launcher.log`.
    pub command_line: Vec<String>,
}

/// `ERROR_ELEVATION_REQUIRED`.
const ERROR_ELEVATION_REQUIRED: u32 = 740;
/// Two seconds of clock slack on the start of the session, as `10000000 * 2` ticks.
const CLOCK_SLACK_TICKS: u64 = 2 * 10_000_000;
/// How long the launcher waits for the helper to clean up after the game closes.
const HELPER_CLEANUP_MS: u32 = 120_000;

fn is_file(p: &str) -> bool {
    std::fs::metadata(p).map(|m| m.is_file()).unwrap_or(false)
}

/// `SetPluginDisabled(here, "discord", true)`: the `disabled` marker file of a plugin.
fn disable_plugin(here: &str, folder: &str) -> bool {
    let marker = format!(
        "{here}\\data\\plugins\\{folder}\\{}",
        files::PLUGIN_DISABLED_MARKER
    );
    if std::fs::symlink_metadata(&marker).is_ok() {
        return true;
    }
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&marker)
        .is_ok()
}

fn pc_wanted(cfg: &Config) -> PcWanted {
    PcWanted {
        firewall: cfg.firewall,
        eac_hosts: cfg.eac_hosts,
        eac_rename: cfg.eac_rename,
        lan: cfg.firewall && cfg.multiplayer,
        allow: cfg.multiplayer_allow.clone(),
    }
}

/// Step 1 of `wmain`: [`find_game`], and when `--game` and `game =` say nothing and nothing is found by
/// itself, the folder picker (`PickFolder`) if the host can show one: the folder dialog, for the player to
/// name the StarCitizen folder. The pick is remembered like a discovered game (`discovered` true), unless
/// this is a dry run. A cancelled or wrong pick fails with the "set game =" message.
fn find_game_or_pick(
    here: &str,
    game_arg: &str,
    cfg: &Config,
    host: &mut dyn Host,
    out: &mut dyn Write,
) -> Result<(String, bool), i32> {
    if !game_arg.is_empty() || !cfg.game.is_empty() {
        return find_game(here, game_arg, cfg, out);
    }
    let found = host.discover_game(here, &cfg.channel, out);
    if let Some(first) = found.first() {
        let _ = writeln!(out, "[i] found the game {}", first.how);
        for f in &found[1..] {
            let _ = writeln!(
                out,
                "[i] also found {} (set game = in sc-offline.ini to use it)",
                f.bin
            );
        }
        return Ok((first.bin.clone(), true));
    }
    if !host.can_pick_folder() {
        return Err(fail(
            out,
            &format!(
                "couldn't find Star Citizen ({}). Open sc-offline.ini and set\n    game = <your StarCitizen folder>\nor pass --game <folder>",
                cfg.channel
            ),
        ));
    }
    let _ = writeln!(
        out,
        "[i] couldn't find Star Citizen ({}) by itself. Pick your StarCitizen folder in the window that opens.",
        cfg.channel
    );
    let picked = host.pick_folder();
    let bin = picked
        .as_deref()
        .map(|p| to_bin64(p, &cfg.channel))
        .unwrap_or_default();
    if bin.is_empty() {
        return Err(fail(
            out,
            &format!(
                "no {GAME_EXE} under {} (channel {}). Open sc-offline.ini and set\n    game = <your StarCitizen folder>",
                picked.as_deref().unwrap_or("(nothing picked)"),
                cfg.channel
            ),
        ));
    }
    Ok((bin, true))
}

fn on_off(b: bool) -> &'static str {
    if b {
        "on"
    } else {
        "off"
    }
}

/// Runs the command. `here` is the folder of the launcher (`dinput8.dll`, `sc-offline.ini` and `data\`
/// live there). Returns the exit code: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running.
pub fn run_play(args: &PlayArgs, host: &mut dyn Host, here: &str, out: &mut dyn Write) -> i32 {
    let data = format!("{here}\\data");
    create_dir(&data); // a fresh unzip may not have it yet
    let mut header = String::from("sc-offline.exe");
    for a in &args.command_line {
        header.push(' ');
        header.push_str(a);
    }
    let log = open_log(&data, false, &header);
    let mut out = Tee { out, log };
    let out = &mut out;
    let dry = args.dry;
    let mut mode = args.mode;
    let _ = write!(
        out,
        "sc-offline launcher {SCO_VERSION}{}\n\n",
        if dry {
            " - dry run, nothing is changed"
        } else {
            ""
        }
    );

    if !can_write_to(&data) {
        let _ = writeln!(
            out,
            "[!] this folder ({here}) can't be written without administrator rights, so the mod can't save\n    your wallet or places. Move the sc-offline folder out of Program Files, e.g. to your Desktop."
        );
    }
    if !dry {
        settle_previous_update(here, out);
    }
    let ini = format!("{here}\\{}", files::INI);
    let cfg = match read_config(std::path::Path::new(&ini)) {
        Some(p) => {
            for m in &p.messages {
                let _ = writeln!(out, "{m}");
            }
            p.config
        }
        None => {
            let _ = writeln!(
                out,
                "[i] no sc-offline.ini next to this exe; using defaults"
            );
            Config::default()
        }
    };
    // 0.7.5: the Discord status is the discord plugin's. An old `discord_presence = off` turns that
    // plugin off once and the line goes, so the launcher's Discord switch owns it from then on.
    if cfg.discord_off
        && !dry
        && disable_plugin(here, "discord")
        && drop_ini_key(std::path::Path::new(&ini), "discord_presence")
    {
        let _ = writeln!(
            out,
            "[i] discord_presence = off: the Discord plugin is turned off (data\\plugins\\discord\\disabled)"
        );
    }

    // 0. Updates. Never blocks play: the host's hook skips whatever goes wrong.
    if cfg.check_updates && !dry && mode == Mode::Play {
        if let Some(code) = host.check_update(&cfg, out) {
            return code;
        }
    }

    // 1. Find the game.
    let (bin, discovered) = match find_game_or_pick(here, &args.game_arg, &cfg, host, out) {
        Ok(found) => found,
        Err(code) => return code,
    };
    // Remember it, so the next run skips the search. game = and --game always win over this.
    if discovered && !dry {
        let _ = std::fs::write(format!("{data}\\game-path.txt"), format!("{bin}\n"));
    }
    let _ = writeln!(out, "Folder:   {bin}");
    let g = GamePaths::new(&bin);
    let pc = pc_wanted(&cfg);

    // 2. Self-checks.
    let checks = self_checks(here, &cfg, &g, out);
    let _ = writeln!(out);

    let running = host.game_may_be_running(&g.bin, out);
    if mode == Mode::Uninstall {
        if running {
            return fail_code(
                out,
                exit_code::GAME_RUNNING,
                &format!("{GAME_EXE} is running. Close it first."),
            );
        }
        if dry {
            let a = scol_install::install::take_mod(&g, true, out);
            let b = undo_pc_changes(host.system(), out, true);
            return if (a | b) != 0 {
                exit_code::ERROR
            } else {
                exit_code::OK
            };
        }
    } else {
        if !is_file(&format!("{here}\\{}", files::MOD_DLL)) {
            return fail(
                out,
                "dinput8.dll is missing next to sc-offline.exe.\n    Extract the whole zip into one folder and run this from there.",
            );
        }
        if running {
            return fail_code(
                out,
                exit_code::GAME_RUNNING,
                &format!("{GAME_EXE} is already running. Close it first."),
            );
        }
        if checks.pc_leftover && !dry && mode == Mode::Play {
            if host.ask_yes(out, "Undo them now and stop, instead of playing?") {
                let _ = writeln!(out, "[i] undoing them (same as `sc-offline.exe uninstall`)");
                mode = Mode::Uninstall;
            } else {
                let _ = writeln!(
                    out,
                    "[i] playing; they are undone together with this session's changes when the game closes."
                );
            }
        }
        let wine = host.system().on_wine();
        let play_handles_eac = mode == Mode::Play && cfg.eac_rename && !wine;
        if checks.eac_active && !args.skip_eac && !play_handles_eac {
            return fail_code(
                out,
                exit_code::EAC_ACTIVE,
                "stopped: Easy Anti-Cheat is active (see above). --skip-eac-check overrides this.",
            );
        }
    }

    // 3. What the mod reads (the SC_OFFLINE_* variables, see docs/data-files.md).
    let play = mode == Mode::Play;
    if play {
        let block = play_env(
            &cfg,
            PlayPaths {
                data: &data,
                user_dir: &g.user_dir,
            },
        );
        for v in &block {
            host.set_env(v.name, &v.value);
        }
        // sco-core's own opt-in variables come from the exp_ keys only.
        let apply = {
            let h: &dyn Host = host;
            apply_capabilities(&cfg.capabilities, &|n| h.env(n).is_some())
        };
        for v in &apply.env {
            host.set_env(v.name, &v.value);
        }
        let on = if apply.on_summary.is_empty() {
            "none".to_string()
        } else {
            format!("{}; not run in game", apply.on_summary)
        };
        let _ = writeln!(out, "Capability switches on: {on}");
        if !apply.removed.is_empty() {
            let _ = writeln!(
                out,
                "[i] removed {} from the game's environment: only the exp_ keys in sc-offline.ini set these",
                apply.removed
            );
        }
        if dry {
            let _ = write!(
                out,
                "[dry-run] would set SC_OFFLINE_BOOT_MAP={} SC_OFFLINE_START_SHIP={} SC_OFFLINE_START={} SC_OFFLINE_PLUGINS={}\n          SC_OFFLINE_RELOCATION={} SC_OFFLINE_ASOP={}; SC_OFFLINE_MOD_LOG, _SPAWN_FILE, _SHIPS_FILE under {}; SC_USER={}\n",
                cfg.boot_map,
                cfg.start_ship,
                cfg.start,
                on_off(cfg.plugins),
                on_off(cfg.relocation),
                on_off(cfg.asop),
                data,
                g.user_dir
            );
        }
    }
    let wine = host.system().on_wine();
    let needs_pc = needs_pc_changes(&pc, wine);

    // 4. Change the game folder, through the helper.
    if dry {
        if play && needs_pc {
            apply_pc_changes(host.system(), out, &pc, &game_exe_in(&bin), true);
        }
        if mode == Mode::Uninstall {
            undo_pc_changes(host.system(), out, true);
        }
        if put_mod(here, &g, if play { "play" } else { "install" }, true, out) != 0 {
            return exit_code::ERROR;
        }
        if play {
            let _ = writeln!(out, "[dry-run] would start {bin}\\{GAME_EXE}");
            let _ = writeln!(
                out,
                "[dry-run] after the game closes: take the mod out, put back what was set aside, undo the PC changes"
            );
            if cfg.clean_logs == LogChoice::Ask {
                let _ = writeln!(
                    out,
                    "[dry-run] then list this session's Game.log, logbackups and crash files and ask before deleting them"
                );
            } else if cfg.clean_logs == LogChoice::Always {
                let _ = writeln!(
                    out,
                    "[dry-run] then delete this session's Game.log, logbackups and crash files without asking (clean_logs = always)"
                );
            }
        }
        return exit_code::OK;
    }

    let op = match mode {
        Mode::Play => "play",
        Mode::Install => "install",
        Mode::Uninstall => "uninstall",
    };
    let event = format!("Local\\sc-offline-ready-{}", host.pid());
    let started = host.create_session(&event, play).and_then(|()| {
        let pid = host.pid();
        let start = plan_helper_start(host.system(), op, &bin, pid, &event, &pc);
        if let Some(n) = start.notice {
            let _ = write!(out, "{n}");
        }
        host.start_helper(&start)
    });
    if let Err(code) = started {
        host.end_session();
        return fail(
            out,
            &format!("couldn't start the mod helper (error {code}); administrator rights refused?"),
        );
    }

    if !play {
        // install / uninstall: the helper does the whole job
        let code = host.wait_helper(None).unwrap_or(1);
        host.end_session();
        if code != 0 {
            return fail(
                out,
                &format!("the helper failed (exit {code}); see data\\launcher.log"),
            );
        }
        if mode == Mode::Install {
            let _ = writeln!(
                out,
                "Mod:      installed. Start the game your usual way; run `sc-offline.exe uninstall` before going online."
            );
        } else {
            let _ = writeln!(out, "Mod:      removed.");
        }
        return exit_code::OK;
    }

    // No timeout: a UAC prompt can sit there as long as the player likes. The helper either signals
    // (copied) or exits (copy failed; it undid nothing because nothing was left half-done).
    if let Ready::HelperGone(code) = host.wait_ready() {
        host.end_session();
        return fail(
            out,
            &format!(
                "couldn't copy the mod into {bin} (helper exit {code}); see data\\launcher.log"
            ),
        );
    }
    let _ = writeln!(out, "Mod:      copied into Bin64");
    if needs_pc {
        let _ = writeln!(
            out,
            "PC:       {}{}{}(undone when the game closes)",
            if pc.firewall {
                "game, RSI Launcher and CrashHandler network blocked, "
            } else {
                ""
            },
            if pc.eac_hosts { "EAC hosts line, " } else { "" },
            if pc.eac_rename { "EAC renamed " } else { "" }
        );
    }
    host.ignore_ctrl_c();

    // 5. Play. The game runs with normal rights, never the helper's, and never administrator's even
    //    when this window was started with "Run as administrator".
    let exe = game_exe_in(&bin);
    let mut rc = exit_code::OK;
    let session_start = host.now_ticks().saturating_sub(CLOCK_SLACK_TICKS);
    match host.start_game(&exe, &bin, out) {
        Err(err) => {
            let _ = writeln!(out, "[!] couldn't start {exe} (error {err})");
            if err == ERROR_ELEVATION_REQUIRED {
                let _ = write!(
                    out,
                    "    StarCitizen.exe is set to \"Run this program as an administrator\" (its Properties >\n    Compatibility tab). Untick it: as administrator, overlay layers such as OBS load into the\n    game and can crash it while it loads.\n"
                );
            }
            rc = exit_code::ERROR;
            // Nothing is playing: the helper may clean up now instead of when this process exits.
            host.signal_closed();
            if host.wait_helper(Some(HELPER_CLEANUP_MS)) == Some(0) {
                let _ = writeln!(
                    out,
                    "Mod:      removed from Bin64; PC changes undone. Safe to go online."
                );
            }
        }
        Ok(()) => {
            if !checks.game_build.is_empty() {
                let _ = std::fs::write(
                    format!("{data}\\game-build.txt"),
                    format!("{}\n", checks.game_build),
                );
            }
            let _ = write!(
                out,
                "\nPlaying. When the game closes, the mod is removed from Bin64.\n"
            );
            host.wait_game();
            // the game can hand over to a second StarCitizen.exe
            let poll = host.game_poll_ms();
            while host.game_may_be_running(&g.bin, out) {
                host.sleep(poll);
            }
            // Take the mod out and undo the PC changes now, before any question below: a question
            // left unanswered must not keep the game folder modded or the firewall rules in place.
            host.signal_closed();
            if let Some(code) = host.wait_helper(Some(HELPER_CLEANUP_MS)) {
                if code == 0 {
                    let _ = writeln!(
                        out,
                        "Mod:      removed from Bin64; PC changes undone. Safe to go online."
                    );
                } else {
                    let _ = writeln!(
                        out,
                        "[!] the helper couldn't undo everything (exit {code}); use Repair in Settings or run `sc-offline.exe uninstall`. See data\\launcher.log"
                    );
                }
            }
            let channel = parent_dir(&bin);
            let can_ask = host.owns_console_input();
            if cfg.crash_reports && can_ask {
                offer_crash_report(host, out, here, &channel, session_start, &checks.game_build);
            }
            clean_session_logs(
                host,
                out,
                here,
                &channel,
                session_start,
                cfg.clean_logs,
                can_ask,
            );
        }
    }
    if !host.helper_finished() {
        let _ = writeln!(
            out,
            "Mod:      the helper removes it from Bin64 and undoes the PC changes as this window closes."
        );
    }
    host.end_session();
    if rc != 0 {
        pause_if_own_console(out);
    }
    rc
}
