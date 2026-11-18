// SPDX-License-Identifier: GPL-3.0-only
//! Flows through the fake system: the exact command lines, file contents and messages of the PC
//! changes, their undo and the helper. Synthetic only; nothing was run in game, and the real
//! system implementation is never used here.

mod common;
use common::*;
use scol_pcchanges::allow::{blocked_outside_lan, parse_allow_list};
use scol_pcchanges::fake::{FakeHooks, FakeSystem};
use scol_pcchanges::helper::{needs_pc_changes, run as helper_run};
use scol_pcchanges::{
    apply_pc_changes, describe_pc_changes, is_helper_invocation, parse_pc_flags, pc_flags,
    plan_helper_start, undo_pc_changes, Io, PcWanted,
};

fn apply(sys: &mut FakeSystem, w: &PcWanted, dry: bool) -> (i32, String) {
    let mut out = Vec::new();
    let rc = apply_pc_changes(sys, &mut out, w, GAME, dry);
    (rc, text(out))
}

fn undo(sys: &mut FakeSystem, dry: bool) -> (i32, String) {
    let mut out = Vec::new();
    let rc = undo_pc_changes(sys, &mut out, dry);
    (rc, text(out))
}

#[test]
fn everything_on_runs_exactly_these_commands() {
    let mut sys = machine();
    let (rc, out) = apply(&mut sys, &all_wanted(), false);
    assert_eq!(rc, 0);
    let expected = vec![
        block(R_GAME, "out", GAME, REMOTE_DEFAULT),
        block(R_GAME, "in", GAME, REMOTE_DEFAULT),
        block(R_RSI, "out", RSI, ""),
        block(R_RSI, "in", RSI, ""),
        block(R_CRASH, "out", CRASH, ""),
        block(R_CRASH, "in", CRASH, ""),
        netsh(&format!(
            "advfirewall firewall add rule name=\"{R_MP}\" dir=in action=allow enable=yes profile=any protocol=UDP program=\"{GAME}\" remoteip=LocalSubnet"
        )),
        flush(),
    ];
    assert_eq!(sys.commands(), expected);
    assert!(sys.log.contains(&format!("mkdir {RECORD_DIR}")));
    assert_eq!(
        sys.text(HOSTS).unwrap(),
        format!("127.0.0.1 localhost\r\n127.0.0.1 {EAC_HOST} {TAG}\r\n")
    );
    assert!(sys
        .log
        .contains(&format!("rename {EAC} -> {EAC_BAK} replace=true")));
    assert!(sys.text(EAC).is_none() && sys.text(EAC_BAK).is_some());
    assert_eq!(
        out,
        format!(
            "[+] add a firewall rule blocking {GAME}\n[+] add a firewall rule blocking {RSI}\n[+] add a firewall rule blocking {CRASH}\n[+] add a firewall rule letting your LAN reach {GAME} over UDP (multiplayer)\n[+] add `127.0.0.1 {EAC_HOST}` to the hosts file\n[+] rename EasyAntiCheat_EOS.exe to .bak\n"
        )
    );
}

#[test]
fn the_record_is_written_after_each_change() {
    let mut sys = machine();
    apply(&mut sys, &all_wanted(), false);
    let writes: Vec<&String> = sys.log.iter().filter(|l| l.starts_with("write ")).collect();
    // time, firewall, fw_launcher, fw_crash, fw_mp, hosts file, hosts record, eac
    let record_writes = writes
        .iter()
        .filter(|l| l.ends_with("pc-changes.txt"))
        .count();
    assert_eq!(record_writes, 7);
}

#[test]
fn without_multiplayer_the_game_rule_blocks_every_address() {
    let mut sys = machine();
    let w = PcWanted {
        firewall: true,
        ..PcWanted::default()
    };
    let (rc, _) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert_eq!(sys.commands()[0], block(R_GAME, "out", GAME, ""));
    assert_eq!(sys.commands().len(), 6);
    assert!(sys.text(HOSTS).unwrap() == "127.0.0.1 localhost\r\n");
}

#[test]
fn an_allow_list_widens_the_lan_rule_text_only() {
    let mut sys = machine();
    let mut w = all_wanted();
    w.allow = "100.64.0.0/10".to_string();
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    let remote = "0.0.0.0-9.255.255.255,11.0.0.0-100.63.255.255,100.128.0.0-126.255.255.255,128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff";
    assert_eq!(sys.commands()[0], block(R_GAME, "out", GAME, remote));
    assert_eq!(
        sys.commands()[6],
        netsh(&format!(
            "advfirewall firewall add rule name=\"{R_MP}\" dir=in action=allow enable=yes profile=any protocol=UDP program=\"{GAME}\" remoteip=LocalSubnet,100.64.0.0/10"
        ))
    );
    assert!(out.contains(&format!(
        "[+] add a firewall rule letting your LAN and multiplayer_allow reach {GAME} over UDP (multiplayer)\n"
    )));
}

#[test]
fn a_bad_allow_list_is_ignored_with_a_message() {
    let mut sys = machine();
    let mut w = all_wanted();
    w.allow = "8.8.8.0/24".to_string();
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert!(out.starts_with("[!] multiplayer_allow ignored (not a list of IPv4 ranges)\n"));
    assert_eq!(
        sys.commands()[0],
        block(R_GAME, "out", GAME, REMOTE_DEFAULT)
    );
}

#[test]
fn allow_list_parsing_and_ranges() {
    assert_eq!(parse_allow_list("").unwrap().1, "");
    let (ranges, text) = parse_allow_list(" 192.168.1.0/24 , 10.1.0.0/16").unwrap();
    assert_eq!(text, "192.168.1.0/24,10.1.0.0/16");
    assert_eq!(
        ranges,
        vec![(0xC0A8_0100, 0xC0A8_01FF), (0x0A01_0000, 0x0A01_FFFF)]
    );
    assert!(parse_allow_list("8.8.8.8/32").is_none());
    assert!(parse_allow_list("10.0.0.1/8").is_none());
    assert_eq!(blocked_outside_lan(&[]), REMOTE_DEFAULT);
}

#[test]
fn rsi_launcher_missing_is_reported_and_skipped() {
    let mut sys = machine();
    sys.files.remove(RSI);
    let w = PcWanted {
        firewall: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert!(out.contains("[i] RSI Launcher.exe not found; not blocking it\n"));
    assert_eq!(sys.commands().len(), 4);
    assert!(sys.text(RECORD).unwrap().contains("fw_crash=added\n"));
    assert!(!sys.text(RECORD).unwrap().contains("fw_launcher"));
}

#[test]
fn rsi_launcher_is_found_through_the_registry_first_then_the_default_folder() {
    let mut sys = machine();
    sys.registry_rsi = Some(r"D:\Apps\RSI Launcher\RSI Launcher.exe".to_string());
    let w = PcWanted {
        firewall: true,
        ..PcWanted::default()
    };
    apply(&mut sys, &w, false);
    assert_eq!(
        sys.commands()[2],
        block(R_RSI, "out", r"D:\Apps\RSI Launcher\RSI Launcher.exe", "")
    );

    let mut sys = machine();
    sys.files.remove(RSI);
    let default = r"C:\Program Files\Roberts Space Industries\RSI Launcher\RSI Launcher.exe";
    sys.put(default, "x");
    apply(&mut sys, &w, false);
    assert_eq!(sys.commands()[2], block(R_RSI, "out", default, ""));
}

#[test]
fn a_failed_lan_rule_falls_back_to_blocking_every_address() {
    let mut sys = machine();
    sys.fail_run.push("remoteip=0.0.0.0".to_string());
    let (rc, out) = apply(&mut sys, &all_wanted(), false);
    assert_eq!(rc, 0);
    assert!(out.contains("[!] couldn't leave your LAN open in the firewall rule; blocking every address (multiplayer can't reach other PCs)\n"));
    let cmds = sys.commands();
    assert_eq!(cmds[0], block(R_GAME, "out", GAME, REMOTE_DEFAULT));
    assert_eq!(cmds[1], delete_rule(R_GAME));
    assert_eq!(cmds[2], block(R_GAME, "out", GAME, ""));
    assert_eq!(cmds[3], block(R_GAME, "in", GAME, ""));
}

#[test]
fn a_failed_block_rule_stops_with_5_and_removes_the_rule() {
    let mut sys = machine();
    sys.fail_run.push("dir=in action=block".to_string());
    let (rc, out) = apply(&mut sys, &all_wanted(), false);
    assert_eq!(rc, 5);
    assert!(out.contains("[!] couldn't add the firewall rule (netsh failed)\n"));
    assert_eq!(sys.commands().last().unwrap(), &delete_rule(R_GAME));
    assert!(!sys.rules.contains(R_GAME));
    // Nothing after the failure ran: no hosts line, no rename.
    assert_eq!(sys.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
    assert!(sys.text(EAC).is_some());
    assert!(!sys.text(RECORD).unwrap().contains("firewall="));
}

#[test]
fn a_failed_multiplayer_rule_is_reported_and_the_run_goes_on() {
    let mut sys = machine();
    sys.fail_run.push("protocol=UDP".to_string());
    let (rc, out) = apply(&mut sys, &all_wanted(), false);
    assert_eq!(rc, 0);
    assert!(out.contains(
        "[!] couldn't add the multiplayer firewall rule; a session you host may not be reachable\n"
    ));
    assert!(!sys.text(RECORD).unwrap().contains("fw_mp"));
    assert!(sys.text(RECORD).unwrap().contains("hosts=added\n"));
}

#[test]
fn the_hosts_line_keeps_the_line_endings_of_the_file() {
    for (before, after) in [
        ("a\nb\n", format!("a\nb\n127.0.0.1 {EAC_HOST} {TAG}\n")),
        (
            "a\r\nb",
            format!("a\r\nb\r\n127.0.0.1 {EAC_HOST} {TAG}\r\n"),
        ),
        ("a\nb", format!("a\nb\n127.0.0.1 {EAC_HOST} {TAG}\n")),
        ("", format!("127.0.0.1 {EAC_HOST} {TAG}\r\n")),
    ] {
        let mut sys = machine();
        sys.put(HOSTS, before);
        let w = PcWanted {
            eac_hosts: true,
            ..PcWanted::default()
        };
        let (rc, _) = apply(&mut sys, &w, false);
        assert_eq!(rc, 0);
        assert_eq!(sys.text(HOSTS).unwrap(), after);
        assert_eq!(sys.commands(), vec![flush()]);
    }
}

#[test]
fn a_hosts_file_that_already_blocks_eac_is_left_alone() {
    let mut sys = machine();
    let own = format!("127.0.0.1 {EAC_HOST}\n");
    sys.put(HOSTS, &own);
    let w = PcWanted {
        eac_hosts: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert_eq!(out, format!("[i] hosts file already blocks {EAC_HOST}\n"));
    assert_eq!(sys.text(HOSTS).unwrap(), own);
    assert!(!sys.text(RECORD).unwrap().contains("hosts="));
    assert!(sys.commands().is_empty());
    // A commented-out line does not count.
    sys.put(HOSTS, &format!("# 127.0.0.1 {EAC_HOST}\n"));
    let (_, out) = apply(&mut sys, &w, false);
    assert!(out.starts_with("[+] add `127.0.0.1"));
}

#[test]
fn a_hosts_file_that_cannot_be_written_fails_with_5() {
    let mut sys = machine();
    sys.fail_write.push(HOSTS.to_string());
    let w = PcWanted {
        eac_hosts: true,
        eac_rename: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 5);
    assert!(out.ends_with(&format!(
        "[!] couldn't edit {HOSTS} (error 5); antivirus may protect it\n"
    )));
    assert!(
        sys.text(EAC).is_some(),
        "the rename must not run after the failure"
    );
}

#[test]
fn eac_missing_is_reported() {
    let mut sys = machine();
    sys.files.remove(EAC);
    let w = PcWanted {
        eac_rename: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert_eq!(out, "[i] Easy Anti-Cheat is already off or not installed\n");
    assert!(!sys.text(RECORD).unwrap().contains("eac="));
}

#[test]
fn a_failed_rename_fails_with_5() {
    let mut sys = machine();
    sys.fail_rename.push(EAC.to_string());
    let w = PcWanted {
        eac_rename: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 5);
    assert_eq!(
        out,
        format!("[+] rename EasyAntiCheat_EOS.exe to .bak\n[!] couldn't rename {EAC} (error 5)\n")
    );
}

#[test]
fn a_record_write_failure_is_reported_but_does_not_stop() {
    let mut sys = machine();
    sys.fail_write.push(RECORD.to_string());
    let w = PcWanted {
        eac_rename: true,
        ..PcWanted::default()
    };
    let (rc, out) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert!(out.contains(&format!("[!] couldn't write {RECORD} (error 5)\n")));
}

#[test]
fn dry_run_prints_the_steps_and_changes_nothing() {
    let mut sys = machine();
    let before = sys.files.clone();
    let (rc, out) = apply(&mut sys, &all_wanted(), true);
    assert_eq!(rc, 0);
    assert!(sys.log.is_empty());
    assert_eq!(sys.files, before);
    assert_eq!(
        out,
        format!(
            "[dry-run] would add a firewall rule blocking {GAME}\n[dry-run] would add a firewall rule blocking {RSI}\n[dry-run] would add a firewall rule blocking {CRASH}\n[dry-run] would add a firewall rule letting your LAN reach {GAME} over UDP (multiplayer)\n[dry-run] would add `127.0.0.1 {EAC_HOST}` to the hosts file\n[dry-run] would rename EasyAntiCheat_EOS.exe to .bak\n"
        )
    );
}

#[test]
fn nothing_wanted_changes_nothing() {
    let mut sys = machine();
    let (rc, out) = apply(&mut sys, &PcWanted::default(), false);
    assert_eq!(rc, 0);
    assert!(out.is_empty());
    assert!(sys.commands().is_empty());
    // The record gets only its time line.
    assert_eq!(sys.text(RECORD).unwrap(), "time=2026-11-17 09:05\n");
}

#[test]
fn undo_reverses_exactly_what_was_recorded() {
    let mut sys = machine();
    apply(&mut sys, &all_wanted(), false);
    // The player had their own line and their own comment in the hosts file.
    let hosts = sys.text(HOSTS).unwrap();
    sys.put(HOSTS, &format!("# mine\r\n{hosts}10.0.0.9 nas\r\n"));
    sys.log.clear();
    let (rc, out) = undo(&mut sys, false);
    assert_eq!(rc, 0, "{out}");
    assert_eq!(
        sys.commands(),
        vec![
            delete_rule(R_GAME),
            delete_rule(R_RSI),
            delete_rule(R_CRASH),
            delete_rule(R_MP),
            flush(),
        ]
    );
    assert_eq!(
        sys.text(HOSTS).unwrap(),
        "# mine\r\n127.0.0.1 localhost\r\n10.0.0.9 nas\r\n"
    );
    assert!(sys
        .log
        .contains(&format!("rename {EAC_BAK} -> {EAC} replace=false")));
    assert!(sys.text(EAC).is_some() && sys.text(EAC_BAK).is_none());
    assert!(sys.text(RECORD).is_none());
    assert!(sys.log.last().unwrap().starts_with("delete "));
    assert_eq!(
        out,
        format!(
            "[+] remove the firewall rule \"{R_GAME}\"\n[+] remove the firewall rule \"{R_RSI}\"\n[+] remove the firewall rule \"{R_CRASH}\"\n[+] remove the firewall rule \"{R_MP}\"\n[+] remove sc-offline's line from the hosts file\n[+] rename EasyAntiCheat_EOS.exe.bak back\n"
        )
    );
}

#[test]
fn undo_leaves_alone_what_was_not_recorded() {
    let mut sys = machine();
    // The player's own .bak and own hosts line; the record lists only the hosts line.
    sys.files.remove(EAC);
    sys.put(EAC_BAK, "mine");
    sys.put(
        HOSTS,
        &format!("127.0.0.1 {EAC_HOST} {TAG}\n127.0.0.1 {EAC_HOST}\n"),
    );
    sys.put(RECORD, "time=2026-11-16 20:00\nhosts=added\n");
    let (rc, _) = undo(&mut sys, false);
    assert_eq!(rc, 0);
    assert_eq!(sys.text(HOSTS).unwrap(), format!("127.0.0.1 {EAC_HOST}\n"));
    assert_eq!(sys.commands(), vec![flush()]);
    assert_eq!(sys.text(EAC_BAK).unwrap(), "mine");
    assert!(sys.log.iter().all(|l| !l.starts_with("rename")));
}

#[test]
fn undo_does_not_overwrite_an_exe_that_came_back() {
    let mut sys = machine();
    sys.put(EAC_BAK, "old");
    sys.put(RECORD, "eac=renamed\n");
    let (rc, _) = undo(&mut sys, false);
    assert_eq!(rc, 0);
    assert!(sys.log.iter().all(|l| !l.starts_with("rename")));
    assert_eq!(sys.text(EAC).unwrap(), "x");
    assert!(sys.text(RECORD).is_none());
}

#[test]
fn undo_with_no_record_does_nothing() {
    let mut sys = machine();
    let (rc, out) = undo(&mut sys, false);
    assert_eq!((rc, out.as_str()), (0, ""));
    assert!(sys.log.is_empty());
}

#[test]
fn undo_dry_run_changes_nothing_and_keeps_the_record() {
    let mut sys = machine();
    apply(&mut sys, &all_wanted(), false);
    sys.log.clear();
    let before = sys.files.clone();
    let (rc, out) = undo(&mut sys, true);
    assert_eq!(rc, 0);
    assert!(sys.log.is_empty());
    assert_eq!(sys.files, before);
    assert!(out.starts_with("[dry-run] would remove the firewall rule"));
}

#[test]
fn undo_that_cannot_remove_a_rule_keeps_the_record_and_returns_5() {
    let mut sys = machine();
    sys.put(RECORD, "firewall=added\n");
    sys.rules.insert(R_GAME.to_string());
    sys.fail_run.push("delete rule".to_string());
    let (rc, out) = undo(&mut sys, false);
    assert_eq!(rc, 5);
    assert_eq!(
        out,
        format!(
            "[+] remove the firewall rule \"{R_GAME}\"\n[!] couldn't remove the firewall rule; remove \"{R_GAME}\" in Windows Defender Firewall\n"
        )
    );
    assert_eq!(sys.commands(), vec![delete_rule(R_GAME), show_rule(R_GAME)]);
    assert!(sys.text(RECORD).is_some());
}

#[test]
fn undo_counts_a_rule_that_is_already_gone_as_done() {
    let mut sys = machine();
    sys.put(RECORD, "firewall=added\n");
    let (rc, _) = undo(&mut sys, false);
    assert_eq!(rc, 0);
    assert_eq!(sys.commands(), vec![delete_rule(R_GAME), show_rule(R_GAME)]);
    assert!(sys.text(RECORD).is_none());
}

#[test]
fn undo_reports_a_hosts_file_it_cannot_edit() {
    let mut sys = machine();
    sys.put(HOSTS, &format!("127.0.0.1 {EAC_HOST} {TAG}\n"));
    sys.put(RECORD, "hosts=added\n");
    sys.fail_write.push(HOSTS.to_string());
    let (rc, out) = undo(&mut sys, false);
    assert_eq!(rc, 5);
    assert!(out.ends_with(&format!(
        "[!] couldn't edit {HOSTS} (error 5); delete the {EAC_HOST} line by hand\n"
    )));
    assert!(
        sys.commands().is_empty(),
        "no DNS flush after a failed edit"
    );
    assert!(sys.text(RECORD).is_some());
}

#[test]
fn undo_reports_a_failed_rename_back() {
    let mut sys = machine();
    sys.files.remove(EAC);
    sys.put(EAC_BAK, "x");
    sys.put(RECORD, "eac=renamed\n");
    sys.fail_rename.push(EAC_BAK.to_string());
    let (rc, out) = undo(&mut sys, false);
    assert_eq!(rc, 5);
    assert!(out.ends_with(&format!("[!] couldn't rename {EAC_BAK} back (error 5)\n")));
}

#[test]
fn a_leftover_rule_that_is_not_in_place_is_added_again() {
    let mut sys = machine();
    sys.put(RECORD, "time=2026-11-16 20:00\nfirewall=added\n");
    let w = PcWanted {
        firewall: true,
        ..PcWanted::default()
    };
    let (rc, _) = apply(&mut sys, &w, false);
    assert_eq!(rc, 0);
    assert_eq!(sys.commands()[0], show_rule(R_GAME));
    assert_eq!(sys.commands()[1], block(R_GAME, "out", GAME, ""));
}

#[test]
fn describe_lists_the_recorded_changes() {
    let mut out = Vec::new();
    describe_pc_changes(
        "time=x\nfirewall=added\nfw_crash=added\nhosts=added\neac=renamed\n",
        &mut out,
    );
    assert_eq!(
        text(out),
        format!(
            "          - firewall rule \"{R_GAME}\"\n          - firewall rule \"{R_CRASH}\"\n          - hosts line for {EAC_HOST}\n          - EasyAntiCheat_EOS.exe renamed to .bak\n"
        )
    );
}

// --- helper -------------------------------------------------------------------------------

fn args(list: &[&str]) -> Vec<String> {
    list.iter().map(|s| s.to_string()).collect()
}

fn run_helper(sys: &mut FakeSystem, hooks: &mut FakeHooks, list: &[&str]) -> (i32, String) {
    let mut out = Vec::new();
    let rc = helper_run(
        &args(list),
        &mut Io {
            sys,
            hooks,
            out: &mut out,
        },
    );
    (rc, text(out))
}

#[test]
fn flags_round_trip() {
    let w = all_wanted();
    assert_eq!(pc_flags(&w, false), "fhel");
    assert_eq!(pc_flags(&w, true), "-");
    assert_eq!(pc_flags(&PcWanted::default(), false), "-");
    assert_eq!(parse_pc_flags("fhel"), w);
    assert_eq!(parse_pc_flags("-"), PcWanted::default());
    assert_eq!(
        parse_pc_flags("h"),
        PcWanted {
            eac_hosts: true,
            ..PcWanted::default()
        }
    );
    assert!(needs_pc_changes(&w, false));
    assert!(!needs_pc_changes(&w, true));
    // `l` alone is not a PC change.
    assert!(!needs_pc_changes(&parse_pc_flags("l"), false));
}

#[test]
fn the_launcher_passes_the_helper_these_arguments() {
    let mut sys = machine();
    let mut w = all_wanted();
    let start = plan_helper_start(&mut sys, "play", BIN, 4242, "Local\\sco-ready", &w);
    assert_eq!(
        start.args,
        format!("--helper play \"{BIN}\" 4242 Local\\sco-ready fhel")
    );
    assert!(start.elevated);
    assert_eq!(
        start.notice.unwrap(),
        "[i] Windows will ask for administrator rights once: the helper blocks the game's network and\n    turns Easy Anti-Cheat off while you play, and undoes both when the game closes.\n    Only the helper runs as administrator, not the game.\n"
    );
    w.allow = "100.64.0.0/10".to_string();
    let start = plan_helper_start(&mut sys, "play", BIN, 4242, "ev", &w);
    assert_eq!(
        start.args,
        format!("--helper play \"{BIN}\" 4242 ev fhel 100.64.0.0/10")
    );
    // The allow list is passed only while the LAN is open.
    w.lan = false;
    let start = plan_helper_start(&mut sys, "play", BIN, 4242, "ev", &w);
    assert_eq!(start.args, format!("--helper play \"{BIN}\" 4242 ev fhe"));
}

#[test]
fn the_helper_runs_elevated_only_when_it_has_to() {
    let mut sys = machine();
    sys.writable.insert(BIN.to_string());
    sys.writable
        .insert(r"C:\Games\StarCitizen\LIVE".to_string());
    let none = PcWanted::default();
    // No PC changes, folders writable: normal rights, no notice.
    let s = plan_helper_start(&mut sys, "play", BIN, 1, "ev", &none);
    assert_eq!((s.elevated, s.notice), (false, None));
    // PC changes wanted: elevated.
    let s = plan_helper_start(&mut sys, "play", BIN, 1, "ev", &all_wanted());
    assert!(s.elevated);
    // install with PC switches on does not make PC changes.
    let s = plan_helper_start(&mut sys, "install", BIN, 1, "ev", &all_wanted());
    assert!(!s.elevated);
    // A leftover record makes play and uninstall elevated, install not.
    sys.put(RECORD, "hosts=added\n");
    for (op, elevated) in [("play", true), ("uninstall", true), ("install", false)] {
        let s = plan_helper_start(&mut sys, op, BIN, 1, "ev", &none);
        assert_eq!(s.elevated, elevated, "{op}");
    }
    // A game folder that is not writable: elevated, with the mod notice.
    let mut locked = machine();
    let s = plan_helper_start(&mut locked, "install", BIN, 1, "ev", &none);
    assert!(s.elevated);
    assert_eq!(
        s.notice.unwrap(),
        "[i] the game folder needs administrator rights for the mod; Windows will ask once.\n    Only the copy/remove helper runs as administrator, not the game.\n"
    );
    // Under Wine there are no PC changes and the flags are "-".
    let mut wine = machine();
    wine.wine = true;
    wine.writable.insert(BIN.to_string());
    wine.writable
        .insert(r"C:\Games\StarCitizen\LIVE".to_string());
    let s = plan_helper_start(&mut wine, "play", BIN, 1, "ev", &all_wanted());
    assert!(!s.elevated);
    assert!(s.args.ends_with(" ev -"));
}

#[test]
fn the_elevation_query_goes_through_the_system() {
    let mut sys = machine();
    use scol_pcchanges::System;
    assert!(!sys.is_elevated());
    sys.elevated = true;
    assert!(sys.is_elevated());
}

#[test]
fn helper_dispatch_matches_wmain() {
    assert!(is_helper_invocation(&args(&[
        "--helper", "play", "b", "1", "e", "f"
    ])));
    assert!(is_helper_invocation(&args(&[
        "--HELPER", "play", "b", "1", "e", "f", "x"
    ])));
    assert!(!is_helper_invocation(&args(&[
        "--helper", "play", "b", "1", "e"
    ])));
    assert!(!is_helper_invocation(&args(&[
        "--helper", "play", "b", "1", "e", "f", "x", "y"
    ])));
    assert!(!is_helper_invocation(&args(&[
        "play", "play", "b", "1", "e", "f"
    ])));
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    assert_eq!(run_helper(&mut sys, &mut hooks, &["--helper", "play"]).0, 4);
    assert!(hooks.log.is_empty() && sys.log.is_empty());
}

#[test]
fn helper_play_makes_the_changes_then_undoes_them_when_the_game_closes() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    let (rc, out) = run_helper(
        &mut sys,
        &mut hooks,
        &[
            "--helper",
            "play",
            BIN,
            "4242",
            "ev",
            "fhel",
            "100.64.0.0/10",
        ],
    );
    assert_eq!(rc, 0, "{out}");
    assert_eq!(
        hooks.log,
        vec![
            "open_parent 4242".to_string(),
            format!("put_mod {BIN} play"),
            "signal_ready ev".to_string(),
            "wait_for_session_end ev".to_string(),
            "close_parent".to_string(),
            format!("wait_game_closed {BIN}"),
            format!("take_mod {BIN}"),
        ]
    );
    assert!(out.ends_with("[+] PC changes undone\n"));
    // Everything is back: rules gone, hosts as before, exe back, record gone.
    assert!(sys.rules.is_empty());
    assert_eq!(sys.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
    assert!(sys.text(EAC).is_some() && sys.text(EAC_BAK).is_none());
    assert!(sys.text(RECORD).is_none());
    let cmds = sys.commands();
    assert!(cmds[0].contains("remoteip=0.0.0.0-9.255.255.255,11.0.0.0-100.63.255.255"));
    assert!(cmds.contains(&delete_rule(R_MP)));
}

#[test]
fn helper_play_with_a_pc_failure_undoes_and_exits_5_before_the_mod_goes_in() {
    let mut sys = machine();
    sys.fail_rename.push(EAC.to_string());
    let mut hooks = FakeHooks::new();
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "play", BIN, "7", "ev", "fhe"],
    );
    assert_eq!(rc, 5);
    assert_eq!(
        hooks.log,
        vec!["open_parent 7".to_string(), "close_parent".to_string()]
    );
    assert!(sys.rules.is_empty());
    assert_eq!(sys.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
    assert!(sys.text(RECORD).is_none());
}

#[test]
fn helper_play_with_a_failed_mod_copy_undoes_and_returns_its_code() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    hooks.put_result = 2;
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "play", BIN, "7", "ev", "h"],
    );
    assert_eq!(rc, 2);
    assert_eq!(hooks.log.last().unwrap(), "close_parent");
    assert!(!hooks.log.iter().any(|l| l.starts_with("signal_ready")));
    assert_eq!(sys.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
}

#[test]
fn helper_play_needs_the_launcher_process() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    hooks.parent_opens = false;
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "play", BIN, "7", "ev", "fhe"],
    );
    assert_eq!(rc, 4);
    assert!(
        sys.log.is_empty(),
        "no PC change before the launcher is known"
    );
}

#[test]
fn helper_install_only_puts_the_mod_in() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "install", BIN, "0", "ev", "fhel"],
    );
    assert_eq!(rc, 0);
    assert_eq!(
        hooks.log,
        vec![
            format!("put_mod {BIN} install"),
            "signal_ready ev".to_string()
        ]
    );
    assert!(sys.log.is_empty(), "install makes no PC change");
}

#[test]
fn helper_uninstall_takes_the_mod_out_and_undoes_leftovers() {
    let mut sys = machine();
    sys.put(HOSTS, &format!("127.0.0.1 {EAC_HOST} {TAG}\r\n"));
    sys.put(RECORD, "hosts=added\n");
    let mut hooks = FakeHooks::new();
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "uninstall", BIN, "0", "ev", "-"],
    );
    assert_eq!(rc, 0);
    assert_eq!(hooks.log, vec![format!("take_mod {BIN}")]);
    assert_eq!(sys.text(HOSTS).unwrap(), "");
    assert!(sys.text(RECORD).is_none());

    // A failed removal wins over the undo's code.
    let mut sys = machine();
    sys.put(RECORD, "hosts=added\n");
    sys.fail_write.push(HOSTS.to_string());
    let mut hooks = FakeHooks::new();
    hooks.take_result = 3;
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "uninstall", BIN, "0", "ev", "-"],
    );
    assert_eq!(rc, 3);
}

#[test]
fn helper_with_an_unknown_operation_exits_4() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    let (rc, _) = run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "fly", BIN, "0", "ev", "fhe"],
    );
    assert_eq!(rc, 4);
    assert!(hooks.log.is_empty() && sys.log.is_empty());
}

#[test]
fn helper_reads_a_pid_like_wcstoul() {
    let mut sys = machine();
    let mut hooks = FakeHooks::new();
    run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "play", BIN, "12abc", "ev", "-"],
    );
    assert_eq!(hooks.log[0], "open_parent 12");
    let mut hooks = FakeHooks::new();
    run_helper(
        &mut sys,
        &mut hooks,
        &["--helper", "play", BIN, "abc", "ev", "-"],
    );
    assert_eq!(hooks.log[0], "open_parent 0");
}

#[test]
fn untidy_game_paths_find_the_same_crash_handler_and_launcher_as_a_tidy_one() {
    // ParentDir resolves the path first (as ParentForSuffix does), so forward slashes, `..` and
    // doubled backslashes give the same channel folder.
    let w = PcWanted {
        firewall: true,
        ..PcWanted::default()
    };
    let untidy = [
        r"C:/Games/StarCitizen/LIVE/Bin64/StarCitizen.exe",
        r"C:\Games\StarCitizen\LIVE\Tools\..\Bin64\StarCitizen.exe",
        r"C:\Games\StarCitizen\LIVE\.\Bin64\StarCitizen.exe",
        r"C:\Games\StarCitizen\LIVE\Bin64\Sub\..\StarCitizen.exe",
    ];
    for game in untidy {
        let mut sys = machine();
        let mut out = Vec::new();
        let rc = apply_pc_changes(&mut sys, &mut out, &w, game, false);
        assert_eq!(rc, 0, "{game}");
        let cmds = sys.commands();
        assert_eq!(cmds.len(), 6, "{game}");
        assert!(cmds[2].contains(&format!("program=\"{RSI}\"")), "{game}");
        assert!(cmds[4].contains(&format!("program=\"{CRASH}\"")), "{game}");
    }
}

#[test]
fn a_bin64_with_a_trailing_backslash_still_gives_the_channel_folder() {
    use scol_pcchanges::system::parent_dir;
    let mut sys = machine();
    assert_eq!(
        parent_dir(&mut sys, r"C:\Games\StarCitizen\LIVE\Bin64\"),
        r"C:\Games\StarCitizen\LIVE"
    );
    assert_eq!(
        parent_dir(&mut sys, "C:/Games/StarCitizen/LIVE/Bin64/"),
        r"C:\Games\StarCitizen\LIVE"
    );
    assert_eq!(
        parent_dir(&mut sys, r"C:\Games\StarCitizen\LIVE\x\..\Bin64"),
        r"C:\Games\StarCitizen\LIVE"
    );
    // Cannot be resolved: the plain split of the C++ fallback.
    assert_eq!(parent_dir(&mut sys, "no-separator"), "no-separator");
    assert_eq!(parent_dir(&mut sys, ""), "");
}
