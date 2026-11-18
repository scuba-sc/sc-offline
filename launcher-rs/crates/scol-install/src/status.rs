// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline.exe status`: `RunStatus`, `SelfChecks` and `CollectStatus` of the C++ launcher.
//!
//! Read-only. It writes no file (not `data\launcher.log`, not `data\game-path.txt`), migrates no
//! setting, makes no network request and opens no folder picker. The text and the exit codes are the
//! C++ launcher's.

use crate::checks::{
    count_plugins, describe_pc_changes, eac_state, field, game_build, game_may_be_running_in,
    hosts_blocks_eac, identify, pc_changes_left, Eac, PREBUILT_SHA256,
};
use crate::discovery::detect_bin64;
use crate::identity::Found;
use crate::paths::{to_bin64, GamePaths, GAME_EXE};
use crate::version::SCO_VERSION;
use crate::win::{is_file, on_wine, read_all};
use scol_config::{read_config, Config};
use scol_contract::exit_code;
use std::io::Write;

/// `PauseIfOwnConsole`: when this exe owns its console window (double-clicked), wait for Enter so the
/// message can be read. From a terminal, or with `SC_OFFLINE_GUI` set, it returns at once.
pub fn pause_if_own_console(out: &mut dyn Write) {
    use windows_sys::Win32::System::Console::GetConsoleProcessList;
    if std::env::var_os(scol_contract::env::GUI).is_some() {
        return;
    }
    let mut ids = [0u32; 2];
    // SAFETY: ids is valid for the 2 entries passed.
    if unsafe { GetConsoleProcessList(ids.as_mut_ptr(), 2) } > 1 {
        return;
    }
    let _ = write!(out, "\nPress Enter to close this window.");
    let _ = out.flush();
    let mut byte = [0u8; 1];
    let _ = std::io::Read::read(&mut std::io::stdin(), &mut byte);
}

/// `FailCode`: prints `[!] <message>` and returns the exit code.
pub fn fail_code(out: &mut dyn Write, code: i32, message: &str) -> i32 {
    let _ = writeln!(out, "[!] {message}");
    pause_if_own_console(out);
    code
}

/// `Fail`: exit code 1.
pub fn fail(out: &mut dyn Write, message: &str) -> i32 {
    fail_code(out, exit_code::ERROR, message)
}

/// `SelfChecks`: prints the checks. Returns whether Easy Anti-Cheat is active and whether a
/// previous run's PC changes are still in place (what `play` acts on; `status` only reports).
pub struct CheckResult {
    pub eac_active: bool,
    pub pc_leftover: bool,
    pub game_build: String,
}

pub fn self_checks(here: &str, cfg: &Config, g: &GamePaths, out: &mut dyn Write) -> CheckResult {
    let mut r = CheckResult {
        eac_active: false,
        pc_leftover: false,
        game_build: String::new(),
    };
    let wine = on_wine();
    // 1. The DLL this launcher would copy in.
    let module = identify(&format!("{here}\\{}", scol_contract::files::MOD_DLL));
    if !module.present {
        let _ = writeln!(
            out,
            "Mod DLL:  missing (dinput8.dll next to sc-offline.exe)"
        );
    } else {
        let _ = writeln!(
            out,
            "Mod DLL:  {}\n          sha256 {}",
            module.what, module.sha
        );
        if module.sha == PREBUILT_SHA256 && !cfg.boot_map.eq_ignore_ascii_case("PU") {
            let _ = writeln!(
                out,
                "[!] the prebuilt DLL only knows boot_map = PU; set that in sc-offline.ini (now {})",
                cfg.boot_map
            );
        }
        if !module.version.is_empty() && module.version != SCO_VERSION {
            let _ = writeln!(
                out,
                "[i] this launcher is {SCO_VERSION}; the DLL is {}",
                module.version
            );
        }
    }

    // 2. The game build, against the one recorded after the last play.
    r.game_build = game_build(&g.bin);
    let mut last = read_all(&format!("{here}\\data\\game-build.txt")).unwrap_or_default();
    while matches!(last.last(), Some(b'\n') | Some(b'\r')) {
        last.pop();
    }
    let last = String::from_utf8_lossy(&last).into_owned();
    let _ = writeln!(
        out,
        "Game:     {}",
        if r.game_build.is_empty() {
            "(version unknown)"
        } else {
            &r.game_build
        }
    );
    if !last.is_empty() && !r.game_build.is_empty() && last != r.game_build {
        let _ = writeln!(
            out,
            "[!] the game updated since you last played (was {last}).\n    If the mod misbehaves, check for a newer sc-offline release."
        );
    }

    // 3. What is in the game folder now.
    let marker = read_all(&g.marker);
    let have_marker = marker.is_some();
    let in_game = identify(&g.dll);
    if !in_game.present {
        let _ = writeln!(out, "Installed: no (Bin64 has no dinput8.dll)");
    } else if have_marker || in_game.ours {
        let _ = writeln!(
            out,
            "Installed: yes, {}{}{}",
            in_game.what,
            if have_marker { ", since " } else { "" },
            match &marker {
                Some(m) => field(m, "time"),
                None => String::new(),
            }
        );
    } else {
        let _ = writeln!(
            out,
            "Installed: no; Bin64 has another mod's dinput8.dll (it is set aside while you play)"
        );
    }
    if (have_marker || in_game.ours) && in_game.present && !game_may_be_running_in(&g.bin, out) {
        let _ = writeln!(
            out,
            "[!] the mod is still in the game folder (a crash, or `install`).\n    Run `sc-offline.exe uninstall` before going online."
        );
    }

    // 4. What sc-offline changed on the PC, if a previous run didn't get to undo it (a crash).
    let pc_left = pc_changes_left();
    if !pc_left.is_empty() {
        r.pc_leftover = true;
        let _ = writeln!(
            out,
            "[!] PC changes from a previous run ({}) are still in place:",
            field(&pc_left, "time")
        );
        describe_pc_changes(&pc_left, out);
        if !game_may_be_running_in(&g.bin, out) {
            let _ = writeln!(
                out,
                "    Run `sc-offline.exe uninstall` to undo them before going online."
            );
        }
    }
    if !wine {
        let _ = writeln!(
            out,
            "Network:  {}{}",
            if cfg.firewall {
                "blocked for StarCitizen.exe, RSI Launcher.exe and CrashHandler.exe while you play (block_network)"
            } else {
                "NOT blocked (block_network = off)"
            },
            if cfg.firewall && cfg.multiplayer {
                "; your LAN stays reachable for the Multiplayer tab (multiplayer)"
            } else {
                ""
            }
        );
    }

    // 5. Easy Anti-Cheat.
    let eac = eac_state();
    let _ = writeln!(
        out,
        "EAC:      {}",
        match eac {
            Eac::Active => "ACTIVE",
            Eac::Disabled => "disabled (EasyAntiCheat_EOS.exe.bak)",
            Eac::NotInstalled => "not installed",
        }
    );
    if wine {
        let _ = writeln!(out, "Hosts:    checked by sc-offline.sh against /etc/hosts");
    } else {
        let _ = writeln!(
            out,
            "Hosts:    {}",
            if hosts_blocks_eac() {
                "EAC download server blocked"
            } else {
                "EAC download server NOT blocked"
            }
        );
    }
    let auto_eac = !wine && cfg.eac_rename;
    r.eac_active = eac == Eac::Active;
    if eac == Eac::Active && auto_eac {
        let _ = writeln!(
            out,
            "[i] Easy Anti-Cheat is on; `play` turns it off while you play and back on afterwards (eac_rename)."
        );
    } else if eac == Eac::Active {
        let _ = writeln!(
            out,
            "[!] Easy Anti-Cheat is active; the mod can't run with it. In PowerShell as administrator:\n      ren \"C:\\Program Files (x86)\\EasyAntiCheat_EOS\\EasyAntiCheat_EOS.exe\" EasyAntiCheat_EOS.exe.bak\n    and block modules-cdn.eac-prod.on.epicgames.com in your hosts file (README, Setup)."
        );
    } else if !wine && !cfg.eac_hosts && !hosts_blocks_eac() {
        let _ = writeln!(
            out,
            "[i] add `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to your hosts file so the RSI Launcher\n    doesn't download Easy Anti-Cheat again (README, Setup)."
        );
    }
    if wine {
        let ov = std::env::var_os("WINEDLLOVERRIDES")
            .map(|v| v.to_string_lossy().into_owned())
            .filter(|v| !v.is_empty() && v.encode_utf16().count() < 512);
        let _ = writeln!(
            out,
            "Wine:     yes, WINEDLLOVERRIDES={}",
            ov.as_deref()
                .unwrap_or("(not set - dinput8 needs n,b; use sc-offline.sh)")
        );
    }
    r
}

/// `RunStatus`: finds the game and runs the self-checks. `game_arg` is `--game`, empty when absent.
/// Read-only: nothing is written, no update check goes out, no folder picker opens and a leftover
/// update is not settled. Returns the exit code.
pub fn run_status(here: &str, game_arg: &str, out: &mut dyn Write) -> i32 {
    let _ = write!(out, "sc-offline launcher {SCO_VERSION}\n\n");
    let cfg = match read_config(std::path::Path::new(&format!(
        "{here}\\{}",
        scol_contract::files::INI
    ))) {
        Some(parsed) => {
            for m in &parsed.messages {
                let _ = writeln!(out, "{m}");
            }
            parsed.config
        }
        None => {
            let _ = writeln!(
                out,
                "[i] no sc-offline.ini next to this exe; using defaults"
            );
            Config::default()
        }
    };
    let bin = match find_game(here, game_arg, &cfg, out) {
        Ok((bin, _discovered)) => bin,
        Err(code) => return code,
    };
    let _ = writeln!(out, "Folder:   {bin}");
    self_checks(here, &cfg, &GamePaths::new(&bin), out);
    let _ = writeln!(out);
    exit_code::OK
}

/// Step 1 of the C++ `wmain`, shared with `RunStatus`: the game folder from `--game`, then `game =`
/// in sc-offline.ini, then discovery. Prints what the launcher prints. Returns the folder and
/// whether discovery found it (`install` then remembers it in `data\game-path.txt`), or the exit
/// code of the failure. The folder picker of the C++ launcher is not ported: with nothing found the
/// "set game =" message is the answer, as it is in the C++ launcher without a console.
pub fn find_game(
    here: &str,
    game_arg: &str,
    cfg: &Config,
    out: &mut dyn Write,
) -> Result<(String, bool), i32> {
    if !game_arg.is_empty() {
        let bin = to_bin64(game_arg, &cfg.channel);
        if bin.is_empty() {
            return Err(fail(
                out,
                &format!(
                    "no {GAME_EXE} under --game {game_arg} (channel {})",
                    cfg.channel
                ),
            ));
        }
        Ok((bin, false))
    } else if !cfg.game.is_empty() {
        let bin = to_bin64(&cfg.game, &cfg.channel);
        if bin.is_empty() {
            return Err(fail(
                out,
                &format!(
                    "no {GAME_EXE} under game = {} in sc-offline.ini (channel {})",
                    cfg.game, cfg.channel
                ),
            ));
        }
        Ok((bin, false))
    } else {
        let found: Vec<Found> = detect_bin64(here, &cfg.channel, out);
        if found.is_empty() {
            return Err(fail(
                out,
                &format!(
                    "couldn't find Star Citizen ({}). Open sc-offline.ini and set\n    game = <your StarCitizen folder>\nor pass --game <folder>",
                    cfg.channel
                ),
            ));
        }
        let _ = writeln!(out, "[i] found the game {}", found[0].how);
        for f in &found[1..] {
            let _ = writeln!(
                out,
                "[i] also found {} (set game = in sc-offline.ini to use it)",
                f.bin
            );
        }
        Ok((found[0].bin.clone(), true))
    }
}

/// What the window's Status panel shows: the same read-only checks, gathered into fields instead of
/// printed (`StatusReport` of the C++ launcher).
#[derive(Debug, Clone)]
pub struct StatusReport {
    pub checked: bool,
    pub folder_known: bool,
    /// The game's Bin64 folder; empty when unknown.
    pub bin: String,
    /// Empty when unknown.
    pub build: String,
    /// sc-offline's dinput8.dll or its marker is in Bin64.
    pub mod_installed: bool,
    /// The leftover PC-change record; empty when none.
    pub pc_left: Vec<u8>,
    pub wine: bool,
    pub network_blocked: bool,
    pub eac_rename: bool,
    pub plugins_on: bool,
    pub eac: Eac,
    pub plugins_enabled: usize,
    pub plugins_total: usize,
}

/// `CollectStatus`: finds the game as `status` does (`game =` in sc-offline.ini, then the usual
/// places) and writes nothing. Messages about the ini are dropped, as the C++ panel does not show
/// them. Anything the discovery would print goes nowhere.
pub fn collect_status(here: &str) -> StatusReport {
    let cfg = read_config(std::path::Path::new(&format!(
        "{here}\\{}",
        scol_contract::files::INI
    )))
    .map(|p| p.config)
    .unwrap_or_default();
    let mut sink = std::io::sink();
    let bin = if !cfg.game.is_empty() {
        to_bin64(&cfg.game, &cfg.channel)
    } else {
        detect_bin64(here, &cfg.channel, &mut sink)
            .into_iter()
            .next()
            .map(|f| f.bin)
            .unwrap_or_default()
    };
    let mut r = StatusReport {
        checked: false,
        folder_known: false,
        bin: String::new(),
        build: String::new(),
        mod_installed: false,
        pc_left: Vec::new(),
        wine: on_wine(),
        network_blocked: cfg.firewall,
        eac_rename: cfg.eac_rename,
        plugins_on: cfg.plugins,
        eac: eac_state(),
        plugins_enabled: 0,
        plugins_total: 0,
    };
    if !bin.is_empty() {
        let p = GamePaths::new(&bin);
        let d = identify(&p.dll);
        r.folder_known = true;
        r.build = game_build(&bin);
        r.mod_installed = is_file(&p.marker) || (d.present && d.ours);
        r.bin = bin;
    }
    r.pc_left = pc_changes_left();
    (r.plugins_enabled, r.plugins_total) = count_plugins(here);
    r.checked = true;
    r
}
