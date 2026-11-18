// SPDX-License-Identifier: GPL-3.0-only
//! The PC changes: make them (`ApplyPcChanges`), record them, undo them (`UndoPcChanges`).
//! Text and order follow `launcher/launcher.cpp` (the block after "what the helper changes on the
//! PC while you play"). Nothing is widened: the same three switches, the same rule names, the
//! same single hosts line, the same one rename.

use crate::allow::{blocked_outside_lan, parse_allow_list};
use crate::system::{
    eac_exe, env_or, hosts_path, parent_dir, pc_changes_path, system32, ErrorCode, System,
};
use scol_contract::pc_changes::{EAC_HOST, FIREWALL_RULES, GAME_EXE, HOSTS_TAG};
use scol_contract::record;
use std::io::Write;

/// What the player asked for (`PcWanted`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PcWanted {
    pub firewall: bool,
    pub eac_hosts: bool,
    pub eac_rename: bool,
    /// multiplayer: the LAN (and `allow`) stay reachable for StarCitizen.exe.
    pub lan: bool,
    /// `multiplayer_allow`, as the ini parser rewrote it.
    pub allow: String,
}

/// Exit code of a failed PC change.
pub const EXIT_PC_FAILED: i32 = 5;

const RSI_RULE_INDEX: usize = 1;
const MP_KEY: &str = "fw_mp";
const MP_RULE: &str = FIREWALL_RULES[3].1;

macro_rules! say {
    ($out:expr, $($arg:tt)*) => {{
        let _ = write!($out, $($arg)*);
    }};
}

/// `Step`: prints the step, returns true when it should really happen (false on `--dry-run`).
pub fn step(out: &mut dyn Write, dry: bool, text: &str) -> bool {
    let _ = writeln!(
        out,
        "{} {text}",
        if dry { "[dry-run] would" } else { "[+]" }
    );
    !dry
}

/// `PcChangesLeft`: what a previous helper recorded and has not undone yet ("" = nothing).
pub fn pc_changes_left(sys: &mut dyn System) -> String {
    let path = pc_changes_path(sys);
    match sys.read_file(&path) {
        Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
        Err(_) => String::new(),
    }
}

/// `record(...)` in `ApplyPcChanges`: adds `key=value` unless the record already holds exactly
/// that, and writes the record out at once.
pub fn record_add(
    sys: &mut dyn System,
    out: &mut dyn Write,
    rec: &mut String,
    key: &str,
    value: &str,
) {
    if !record::add(rec, key, value) {
        return;
    }
    let path = pc_changes_path(sys);
    if let Err(code) = sys.write_file(&path, rec.as_bytes()) {
        say!(out, "[!] couldn't write {path} (error {code})\n");
    }
}

fn netsh(sys: &mut dyn System, args: &str) -> i32 {
    let exe = system32(sys, "netsh.exe");
    sys.run(&exe, args)
}

/// `FirewallRuleExists`.
pub fn firewall_rule_exists(sys: &mut dyn System, rule: &str) -> bool {
    netsh(
        sys,
        &format!("advfirewall firewall show rule name=\"{rule}\""),
    ) == 0
}

/// `AddFirewallRule`: a block rule out and in. `remote` is the addresses to block ("" for all).
pub fn add_firewall_rule(sys: &mut dyn System, rule: &str, exe: &str, remote: &str) -> bool {
    for dir in ["out", "in"] {
        let mut args = format!(
            "advfirewall firewall add rule name=\"{rule}\" dir={dir} action=block enable=yes profile=any program=\"{exe}\""
        );
        if !remote.is_empty() {
            args.push_str(&format!(" remoteip={remote}"));
        }
        if netsh(sys, &args) != 0 {
            return false;
        }
    }
    true
}

/// `AddMultiplayerRule`: inbound UDP to StarCitizen.exe from the local subnet and the allow list.
pub fn add_multiplayer_rule(sys: &mut dyn System, exe: &str, allow: &str) -> bool {
    let mut args = format!(
        "advfirewall firewall add rule name=\"{MP_RULE}\" dir=in action=allow enable=yes profile=any protocol=UDP program=\"{exe}\" remoteip=LocalSubnet"
    );
    if !allow.is_empty() {
        args.push(',');
        args.push_str(allow);
    }
    netsh(sys, &args) == 0
}

/// `DeleteFirewallRule`.
pub fn delete_firewall_rule(sys: &mut dyn System, rule: &str) -> bool {
    netsh(
        sys,
        &format!("advfirewall firewall delete rule name=\"{rule}\""),
    ) == 0
}

fn flush_dns(sys: &mut dyn System) {
    let exe = system32(sys, "ipconfig.exe");
    sys.run(&exe, "/flushdns");
}

/// `RsiLauncherExe`: its uninstall entry first, then beside the game library, then the default
/// folder. Empty when none exists.
pub fn rsi_launcher_exe(sys: &mut dyn System, bin: &str) -> String {
    if let Some(exe) = sys.registry_rsi_launcher() {
        return exe;
    }
    // <library>\StarCitizen\<channel>\Bin64 -> <library>\RSI Launcher
    let beside = format!("{}\\RSI Launcher\\RSI Launcher.exe", {
        let a = parent_dir(sys, bin);
        let b = parent_dir(sys, &a);
        parent_dir(sys, &b)
    });
    if sys.is_file(&beside) {
        return beside;
    }
    let default = format!(
        "{}\\Roberts Space Industries\\RSI Launcher\\RSI Launcher.exe",
        env_or(sys, "ProgramFiles", "C:\\Program Files")
    );
    if sys.is_file(&default) {
        default
    } else {
        String::new()
    }
}

/// `CrashHandlerExe`: `<channel>\Tools\Public\CrashHandler.exe`, when it exists.
pub fn crash_handler_exe(sys: &mut dyn System, bin: &str) -> String {
    let channel = parent_dir(sys, bin);
    let exe = format!("{channel}\\Tools\\Public\\CrashHandler.exe");
    if sys.is_file(&exe) {
        exe
    } else {
        String::new()
    }
}

fn contains(hay: &[u8], needle: &[u8]) -> bool {
    !needle.is_empty() && hay.windows(needle.len()).any(|w| w == needle)
}

/// `HostsBlocksEac`: a hosts line (comment cut off) that names the EAC download server.
pub fn hosts_blocks_eac(sys: &mut dyn System) -> bool {
    let path = hosts_path(sys);
    let Ok(hosts) = sys.read_file(&path) else {
        return false;
    };
    hosts.split(|&c| c == b'\n').any(|line| {
        let line = match line.iter().position(|&c| c == b'#') {
            Some(h) => &line[..h],
            None => line,
        };
        contains(line, EAC_HOST.as_bytes())
    })
}

/// `AddHostsLine`: appends the EAC line, tagged so it can be found again. Keeps the file's line
/// endings.
pub fn add_hosts_line(sys: &mut dyn System) -> Result<(), ErrorCode> {
    let path = hosts_path(sys);
    let mut hosts = sys.read_file(&path)?;
    let eol: &[u8] = if hosts.is_empty() || contains(&hosts, b"\r\n") {
        b"\r\n"
    } else {
        b"\n"
    };
    if !hosts.is_empty() && hosts.last() != Some(&b'\n') {
        hosts.extend_from_slice(eol);
    }
    hosts.extend_from_slice(format!("127.0.0.1 {EAC_HOST} {HOSTS_TAG}").as_bytes());
    hosts.extend_from_slice(eol);
    sys.write_file(&path, &hosts)
}

/// `RemoveHostsLine`: removes only the lines this launcher added (the ones carrying the tag).
pub fn remove_hosts_line(sys: &mut dyn System) -> Result<(), ErrorCode> {
    let path = hosts_path(sys);
    let hosts = sys.read_file(&path)?;
    let mut kept = Vec::new();
    let mut removed = false;
    for line in hosts.split_inclusive(|&c| c == b'\n') {
        if contains(line, HOSTS_TAG.as_bytes()) {
            removed = true;
        } else {
            kept.extend_from_slice(line);
        }
    }
    if removed {
        sys.write_file(&path, &kept)
    } else {
        Ok(())
    }
}

/// `DescribePcChanges`: one line per change the record lists.
pub fn describe_pc_changes(rec: &str, out: &mut dyn Write) {
    for (key, name) in FIREWALL_RULES {
        if record::field(rec, key) == "added" {
            say!(out, "          - firewall rule \"{name}\"\n");
        }
    }
    if record::field(rec, "hosts") == "added" {
        say!(out, "          - hosts line for {EAC_HOST}\n");
    }
    if record::field(rec, "eac") == "renamed" {
        say!(out, "          - EasyAntiCheat_EOS.exe renamed to .bak\n");
    }
}

/// `UndoPcChanges`: undoes what the record says this launcher did, then deletes the record.
/// 0 ok, 5 something stayed.
pub fn undo_pc_changes(sys: &mut dyn System, out: &mut dyn Write, dry: bool) -> i32 {
    let rec = pc_changes_left(sys);
    if rec.is_empty() {
        return 0;
    }
    let mut rc = 0;
    for (key, name) in FIREWALL_RULES {
        if record::field(&rec, key) == "added"
            && step(out, dry, &format!("remove the firewall rule \"{name}\""))
            && !delete_firewall_rule(sys, name)
            && firewall_rule_exists(sys, name)
        {
            say!(out, "[!] couldn't remove the firewall rule; remove \"{name}\" in Windows Defender Firewall\n");
            rc = EXIT_PC_FAILED;
        }
    }
    if record::field(&rec, "hosts") == "added"
        && step(out, dry, "remove sc-offline's line from the hosts file")
    {
        match remove_hosts_line(sys) {
            Ok(()) => flush_dns(sys),
            Err(code) => {
                let hosts = hosts_path(sys);
                say!(out, "[!] couldn't edit {hosts} (error {code}); delete the {EAC_HOST} line by hand\n");
                rc = EXIT_PC_FAILED;
            }
        }
    }
    if record::field(&rec, "eac") == "renamed"
        && step(out, dry, "rename EasyAntiCheat_EOS.exe.bak back")
    {
        let exe = eac_exe(sys);
        let bak = format!("{exe}.bak");
        if sys.is_file(&bak) && !sys.is_file(&exe) {
            if let Err(code) = sys.rename(&bak, &exe, false) {
                say!(out, "[!] couldn't rename {bak} back (error {code})\n");
                rc = EXIT_PC_FAILED;
            }
        }
    }
    if rc == 0 && !dry {
        let path = pc_changes_path(sys);
        sys.delete_file(&path);
    }
    rc
}

/// `ApplyPcChanges`: makes the wanted changes and records each one as soon as it is made.
/// 0 ok, 5 failed (anything already changed stays recorded, and the caller undoes it).
pub fn apply_pc_changes(
    sys: &mut dyn System,
    out: &mut dyn Write,
    w: &PcWanted,
    game_exe: &str,
    dry: bool,
) -> i32 {
    // A leftover record after a crash: keep it, add to it.
    let mut rec = pc_changes_left(sys);
    let pcp = pc_changes_path(sys);
    let dir = parent_dir(sys, &pcp);
    if !dry {
        sys.create_dir(&dir);
    }
    if !dry && !rec.contains("time=") {
        let t = sys.local_time();
        let when = record::pc_time(t.year, t.month, t.day, t.hour, t.minute);
        record_add(sys, out, &mut rec, "time", &when);
    }

    if w.firewall {
        let bin = parent_dir(sys, game_exe);
        let exes = [
            game_exe.to_string(),
            rsi_launcher_exe(sys, &bin),
            crash_handler_exe(sys, &bin),
        ];
        let (ranges, allow_text) = if w.lan {
            match parse_allow_list(&w.allow) {
                Some(parsed) => parsed,
                None => {
                    say!(
                        out,
                        "[!] multiplayer_allow ignored (not a list of IPv4 ranges)\n"
                    );
                    (Vec::new(), String::new())
                }
            }
        } else {
            (Vec::new(), String::new())
        };
        // FIREWALL_RULES' first three: the block rules.
        for (i, exe) in exes.iter().enumerate() {
            let (key, name) = FIREWALL_RULES[i];
            if exe.is_empty() {
                if i == RSI_RULE_INDEX {
                    say!(out, "[i] RSI Launcher.exe not found; not blocking it\n");
                }
                continue;
            }
            if record::field(&rec, key) == "added" && !dry && firewall_rule_exists(sys, name) {
                say!(out, "[i] firewall rule \"{name}\" already in place\n");
            } else if step(out, dry, &format!("add a firewall rule blocking {exe}")) {
                let remote = if i == 0 && w.lan {
                    blocked_outside_lan(&ranges)
                } else {
                    String::new()
                };
                let mut added = add_firewall_rule(sys, name, exe, &remote);
                if !added && !remote.is_empty() {
                    // Fail closed: block every address, as without multiplayer.
                    delete_firewall_rule(sys, name);
                    say!(out, "[!] couldn't leave your LAN open in the firewall rule; blocking every address (multiplayer can't reach other PCs)\n");
                    added = add_firewall_rule(sys, name, exe, "");
                }
                if !added {
                    say!(out, "[!] couldn't add the firewall rule (netsh failed)\n");
                    delete_firewall_rule(sys, name);
                    return EXIT_PC_FAILED;
                }
                record_add(sys, out, &mut rec, key, "added");
            }
        }
        if w.lan {
            if record::field(&rec, MP_KEY) == "added" && !dry && firewall_rule_exists(sys, MP_RULE)
            {
                say!(out, "[i] firewall rule \"{MP_RULE}\" already in place\n");
            } else if step(
                out,
                dry,
                &format!(
                    "add a firewall rule letting your LAN{} reach {game_exe} over UDP (multiplayer)",
                    if allow_text.is_empty() { "" } else { " and multiplayer_allow" }
                ),
            ) {
                if add_multiplayer_rule(sys, game_exe, &allow_text) {
                    record_add(sys, out, &mut rec, MP_KEY, "added");
                } else {
                    delete_firewall_rule(sys, MP_RULE);
                    say!(out, "[!] couldn't add the multiplayer firewall rule; a session you host may not be reachable\n");
                }
            }
        }
    }
    if w.eac_hosts {
        if hosts_blocks_eac(sys) {
            say!(out, "[i] hosts file already blocks {EAC_HOST}\n");
        } else if step(
            out,
            dry,
            &format!("add `127.0.0.1 {EAC_HOST}` to the hosts file"),
        ) {
            if let Err(code) = add_hosts_line(sys) {
                let hosts = hosts_path(sys);
                say!(
                    out,
                    "[!] couldn't edit {hosts} (error {code}); antivirus may protect it\n"
                );
                return EXIT_PC_FAILED;
            }
            record_add(sys, out, &mut rec, "hosts", "added");
            flush_dns(sys);
        }
    }
    if w.eac_rename {
        let exe = eac_exe(sys);
        let bak = format!("{exe}.bak");
        if !sys.is_file(&exe) {
            say!(out, "[i] Easy Anti-Cheat is already off or not installed\n");
        } else if step(out, dry, "rename EasyAntiCheat_EOS.exe to .bak") {
            if let Err(code) = sys.rename(&exe, &bak, true) {
                say!(out, "[!] couldn't rename {exe} (error {code})\n");
                return EXIT_PC_FAILED;
            }
            record_add(sys, out, &mut rec, "eac", "renamed");
        }
    }
    0
}

/// `GameExe` for a `Bin64` folder.
pub fn game_exe_in(bin: &str) -> String {
    format!("{bin}\\{GAME_EXE}")
}
