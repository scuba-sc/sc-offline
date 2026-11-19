// SPDX-License-Identifier: GPL-3.0-only
//! The game folder picker (the last resort when no game was found) and the update check before `play`,
//! over the fake host: the folder dialog and the update are scripted, discovery is scripted, nothing opens a
//! window or reaches the network. Folders under the system temp folder. Nothing here was run in game.
#![cfg(windows)]

mod common;
use common::*;
use scol_install::identity::Found;
use scol_play::Mode;

const NO_PC: &str = "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\nclean_logs = never\ncrash_reports = off\n";

/// A world whose ini names no game, so discovery decides.
fn world_without_game_line(tag: &str, ini: &str) -> World {
    let w = World::new(tag, ini);
    std::fs::write(format!("{}\\sc-offline.ini", w.here), ini).unwrap();
    w
}

fn library(w: &World) -> String {
    w.channel.trim_end_matches("\\LIVE").to_string()
}

fn has(text: &str, line: &str) {
    assert!(
        text.lines().any(|l| l == line),
        "missing line {line:?} in:\n{text}"
    );
}

#[test]
fn nothing_found_and_a_pick_names_the_game() {
    let w = world_without_game_line("pick-ok", NO_PC);
    let mut host = w.host(w.system());
    host.discovered = Some(vec![]);
    host.can_pick = true;
    host.picks.push_back(Some(library(&w)));
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    has(
        &text,
        "[i] couldn't find Star Citizen (LIVE) by itself. Pick your StarCitizen folder in the window that opens.",
    );
    has(&text, &format!("Folder:   {}", w.bin));
    assert_eq!(host.pick_calls, 1);
    // remembered for the next run
    let remembered = std::fs::read_to_string(format!("{}\\data\\game-path.txt", w.here)).unwrap();
    assert_eq!(remembered.trim(), w.bin);
}

#[test]
fn a_dry_run_picks_but_remembers_nothing() {
    let w = world_without_game_line("pick-dry", NO_PC);
    let mut host = w.host(w.system());
    host.discovered = Some(vec![]);
    host.can_pick = true;
    host.picks.push_back(Some(w.bin.clone())); // the Bin64 folder itself is accepted too
    let (code, text) = run(
        &w,
        &mut host,
        &args(Mode::Play, true, &["play", "--dry-run"]),
    );
    assert_eq!(code, 0, "{text}");
    has(&text, &format!("Folder:   {}", w.bin));
    assert!(!w.exists(&format!("{}\\data\\game-path.txt", w.here)));
}

#[test]
fn a_cancelled_or_wrong_pick_says_how_to_set_the_game() {
    let w = world_without_game_line("pick-bad", NO_PC);
    let mut host = w.host(w.system());
    host.discovered = Some(vec![]);
    host.can_pick = true;
    // cancelled
    host.picks.push_back(None);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1);
    assert!(
        text.contains("[!] no StarCitizen.exe under (nothing picked) (channel LIVE). Open sc-offline.ini and set\n    game = <your StarCitizen folder>"),
        "{text}"
    );
    // a folder with no game in it
    let empty = format!("{}\\nothing-here", w.root.display());
    std::fs::create_dir_all(&empty).unwrap();
    host.picks.push_back(Some(empty.clone()));
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1);
    assert!(
        text.contains(&format!("no StarCitizen.exe under {empty} (channel LIVE)")),
        "{text}"
    );
    assert_eq!(host.pick_calls, 2);
    assert!(!w.exists(&w.marker()), "nothing was installed");
}

#[test]
fn no_dialog_unless_the_host_can_show_one_and_nothing_was_named_or_found() {
    // the host cannot show a dialog (no console, not the window): the C++ launcher's message
    let w = world_without_game_line("pick-no", NO_PC);
    let mut host = w.host(w.system());
    host.discovered = Some(vec![]);
    host.can_pick = false;
    host.picks.push_back(Some(library(&w)));
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1);
    assert!(
        text.contains("[!] couldn't find Star Citizen (LIVE)"),
        "{text}"
    );
    assert_eq!(host.pick_calls, 0);

    // --game wins: no dialog, even for a wrong folder
    host.can_pick = true;
    let mut a = args(
        Mode::Play,
        false,
        &["play", "--game", "C:\\scol-test\\nope"],
    );
    a.game_arg = "C:\\scol-test\\nope".into();
    let (code, text) = run(&w, &mut host, &a);
    assert_eq!(code, 1);
    assert!(
        text.contains("no StarCitizen.exe under --game C:\\scol-test\\nope"),
        "{text}"
    );
    assert_eq!(host.pick_calls, 0);

    // a game found by itself: no dialog
    host.discovered = Some(vec![Found {
        bin: w.bin.clone(),
        how: "in a scripted place",
    }]);
    let (code, text) = run(
        &w,
        &mut host,
        &args(Mode::Play, true, &["play", "--dry-run"]),
    );
    assert_eq!(code, 0, "{text}");
    has(&text, "[i] found the game in a scripted place");
    assert_eq!(host.pick_calls, 0);
}

#[test]
fn game_in_the_ini_wins_over_the_dialog() {
    let w = World::new("pick-ini", NO_PC); // has a `game =` line
    let mut host = w.host(w.system());
    host.can_pick = true;
    host.picks
        .push_back(Some("C:\\scol-test\\elsewhere".into()));
    let (code, _) = run(
        &w,
        &mut host,
        &args(Mode::Play, true, &["play", "--dry-run"]),
    );
    assert_eq!(code, 0);
    assert_eq!(host.pick_calls, 0);
}

#[test]
fn an_update_that_took_over_ends_play_before_anything_else() {
    let w = World::new(
        "update-ends-play",
        "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = on\nclean_logs = never\ncrash_reports = off\n",
    );
    let mut host = w.host(w.system());
    host.update_ends_play = Some(10);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 10, "{text}");
    assert_eq!(host.update_checks, 1);
    assert!(
        !text.contains("Folder:"),
        "the game is not even looked for: {text}"
    );
    assert!(
        host.helper_starts.is_empty() && !w.exists(&w.marker()),
        "nothing was installed"
    );

    // no update to take over: play goes on
    host.update_ends_play = None;
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert_eq!(host.update_checks, 2);
    has(&text, &format!("Folder:   {}", w.bin));

    // a dry run and the other commands never check
    let (code, _) = run(
        &w,
        &mut host,
        &args(Mode::Play, true, &["play", "--dry-run"]),
    );
    assert_eq!(code, 0);
    let (code, _) = run(&w, &mut host, &args(Mode::Install, false, &["install"]));
    assert_eq!(code, 0);
    assert_eq!(host.update_checks, 2);
}
