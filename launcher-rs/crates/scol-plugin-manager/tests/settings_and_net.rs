// SPDX-License-Identifier: GPL-3.0-only
//! `plugins settings` (the `[settings]` values of a plugin in its own database) and `plugins catalogue ...`
//! without `--source` (the product URL through a network client). The network client here is a local
//! folder served through the fetcher trait, so no request leaves the machine. Synthetic plugin and TEST
//! key; nothing run in game.

mod common;

use std::path::PathBuf;
use std::sync::Mutex;

use common::*;
use scol_catalogue::fetch::Fetcher;
use scol_plugin_manager::cli::{run_with_net, Capture, NetFactory};
use scol_plugin_manager::fetcher::FileFetcher;

const SETTINGS_INI: &str = "id = demo\nname = Demo\nversion = 1.0.0\nauthor = tests\napi = 1.1\nkind = native\nentry = demo.dll\n[settings]\nspeed = int default 5 min 1 max 9 label \"Speed\" help \"how fast\"\nmode = enum(a,b) default a\n";

fn with_settings_plugin(t: &Scratch) {
    let dir = t.root.join("data/plugins/demo");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("plugin.ini"), SETTINGS_INI).unwrap();
    std::fs::write(dir.join("demo.dll"), b"MZ-demo").unwrap();
}

#[test]
fn settings_list_set_and_reset() {
    let t = Scratch::new("settings");
    with_settings_plugin(&t);
    let ctx = t.ctx();
    let (rc, io) = run(&ctx, &["settings", "demo"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    let text = io.text();
    assert!(text.contains("speed = 5  (int, default)  Speed"), "{text}");
    assert!(text.contains("    how fast"), "{text}");
    assert!(
        text.contains("mode = a  (enum, default; one of a, b)"),
        "{text}"
    );
    assert!(
        !t.root.join("data/storage").exists(),
        "listing writes nothing"
    );

    let (rc, io) = run(&ctx, &["settings", "demo", "speed=7", "mode=b"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    let text = io.text();
    assert!(text.contains("speed set to 7"), "{text}");
    assert!(text.contains("mode set to b"), "{text}");
    assert!(text.contains("saved; the game reads these values at its next start"));
    assert!(t.root.join("data/storage/demo.db").is_file());
    let (_, io) = run(&ctx, &["settings", "demo"]);
    assert!(
        io.text().contains("speed = 7  (int, kept)"),
        "{}",
        io.text()
    );

    // the same values again: nothing to change
    let (rc, io) = run(&ctx, &["settings", "demo", "speed=7"]);
    assert_eq!(rc, 0);
    assert_eq!(io.text(), "nothing to change");

    let (rc, io) = run(&ctx, &["settings", "demo", "--reset"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    assert!(io.text().contains("speed back to its default"));
    let (_, io) = run(&ctx, &["settings", "demo"]);
    assert!(io.text().contains("speed = 5  (int, default)"));
}

#[test]
fn settings_refusals_change_nothing() {
    let t = Scratch::new("settings-refuse");
    with_settings_plugin(&t);
    let ctx = t.ctx();
    for (args, needle) in [
        (&["settings", "demo", "speed=99"][..], "Speed: at most 9"),
        (
            &["settings", "demo", "speed=x"][..],
            "Speed: a whole number",
        ),
        (
            &["settings", "demo", "mode=z"][..],
            "pick one of the choices",
        ),
        (
            &["settings", "demo", "nope=1"][..],
            "plugin demo has no setting nope",
        ),
        (&["settings", "demo", "speed"][..], "is not name=value"),
        (&["settings", "missing"][..], "no plugin missing"),
        (&["settings", "../x"][..], "is not a plugin id"),
        (
            &["settings", "demo", "speed=1", "--reset"][..],
            "--reset takes no name=value",
        ),
        (&["settings"][..], "give one plugin id"),
    ] {
        let (rc, io) = run(&ctx, args);
        assert_eq!(rc, 1, "{args:?}");
        assert!(io.err.join("\n").contains(needle), "{args:?}: {:?}", io.err);
    }
    // one bad value among good ones writes none of them
    let (rc, _) = run(&ctx, &["settings", "demo", "mode=b", "speed=99"]);
    assert_eq!(rc, 1);
    assert!(!t.root.join("data/storage").exists());
}

#[test]
fn settings_are_not_written_while_the_game_runs() {
    let t = Scratch::new("settings-running");
    with_settings_plugin(&t);
    let mut ctx = t.ctx();
    ctx.game_running = || true;
    let (rc, io) = run(&ctx, &["settings", "demo", "speed=3"]);
    assert_eq!(rc, 1);
    assert!(
        io.err.join("\n").contains("the game is running"),
        "{:?}",
        io.err
    );
    assert!(!t.root.join("data/storage").exists());
    // reading is fine while it runs
    let (rc, _) = run(&ctx, &["settings", "demo"]);
    assert_eq!(rc, 0);
}

// ---- the catalogue without --source ---------------------------------------------------------------------

static SERVED: Mutex<Option<PathBuf>> = Mutex::new(None);
static ASKED: Mutex<u32> = Mutex::new(0);
/// The two network tests share the counters above: one at a time.
static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());

fn served_fetcher() -> Box<dyn Fetcher> {
    *ASKED.lock().unwrap() += 1;
    let dir = SERVED.lock().unwrap().clone().expect("a folder is served");
    Box::new(FileFetcher::new(&dir))
}

fn net_run(
    ctx: &scol_plugin_manager::Ctx,
    args: &[&str],
    net: Option<NetFactory>,
) -> (i32, Capture) {
    let args: Vec<String> = args.iter().map(|s| s.to_string()).collect();
    let mut io = Capture::default();
    let rc = run_with_net(&args, &mut io, ctx, net);
    (rc, io)
}

#[test]
fn a_plain_catalogue_command_says_nothing_is_published_and_asks_nobody() {
    let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
    let t = Scratch::new("net-empty");
    let mut ctx = t.ctx();
    // the trust root this build ships: empty
    ctx.trust = scol_catalogue::trust::TrustRoot::embedded();
    assert!(ctx.trust.is_empty());
    *ASKED.lock().unwrap() = 0;
    let before = snapshot(&t.root);
    for args in [
        &["catalogue", "list"][..],
        &["catalogue", "install", "demo"][..],
        &["catalogue", "update"][..],
        &["update"][..],
    ] {
        let (rc, io) = net_run(&ctx, args, Some(served_fetcher));
        assert_eq!(rc, 1, "{args:?}");
        assert_eq!(
            io.err.join("\n"),
            "no catalogue is published for this build yet",
            "{args:?}"
        );
    }
    assert_eq!(*ASKED.lock().unwrap(), 0, "no client was even made");
    assert_eq!(snapshot(&t.root), before, "nothing written");
}

#[test]
fn the_network_path_works_with_a_trusted_key_and_respects_check_updates() {
    let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
    let t = Scratch::new("net-trusted");
    let ctx = t.ctx(); // trusts the TEST key
    let cat = t.catalogue_dir();
    t.publish(&cat, "1.0.0", 1, Review::FirstParty);
    *SERVED.lock().unwrap() = Some(cat);

    // no client in the program: a clear refusal
    let (rc, io) = net_run(&ctx, &["catalogue", "list"], None);
    assert_eq!(rc, 1);
    assert!(
        io.err.join("\n").contains("no network client"),
        "{:?}",
        io.err
    );

    // with a client: list and install, from the product URL
    *ASKED.lock().unwrap() = 0;
    let (rc, io) = net_run(&ctx, &["catalogue", "list"], Some(served_fetcher));
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    assert!(io.text().contains("demo 1.0.0"), "{}", io.text());
    assert_eq!(*ASKED.lock().unwrap(), 1);
    let (rc, io) = net_run(
        &ctx,
        &["catalogue", "install", "demo"],
        Some(served_fetcher),
    );
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    assert!(
        t.root.join("data/plugins/demo/disabled").is_file(),
        "lands disabled"
    );

    // check_updates = off: the network is not asked, not even for a client
    std::fs::write(t.root.join("sc-offline.ini"), "check_updates = off\r\n").unwrap();
    *ASKED.lock().unwrap() = 0;
    let (rc, io) = net_run(&ctx, &["catalogue", "list"], Some(served_fetcher));
    assert_eq!(rc, 1);
    assert!(
        io.err
            .join("\n")
            .starts_with("check_updates = off in sc-offline.ini"),
        "{:?}",
        io.err
    );
    assert_eq!(*ASKED.lock().unwrap(), 0);
    // a local folder still works with check_updates = off
    let src = t.dir.join("catalogue");
    let (rc, io) = net_run(
        &ctx,
        &["catalogue", "list", "--source", src.to_str().unwrap()],
        None,
    );
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    // and `check_updates = on` asks again
    std::fs::write(t.root.join("sc-offline.ini"), "check_updates = on\r\n").unwrap();
    let (rc, _) = net_run(&ctx, &["catalogue", "list"], Some(served_fetcher));
    assert_eq!(rc, 0);
}

#[test]
fn the_product_constants_are_what_the_network_path_uses() {
    use scol_contract::product::*;
    assert!(CATALOGUE_TRUST_KEYS.is_empty());
    assert_eq!(
        scol_catalogue::trust::PRODUCTION_KEYS.len(),
        CATALOGUE_TRUST_KEYS.len()
    );
    assert!(CATALOGUE_INDEX_URL.starts_with("https://github.com/"));
    assert!(CATALOGUE_HOSTS.contains(&"github.com"));
}
