// SPDX-License-Identifier: GPL-3.0-only
//! Flow tests for the window's library side, through `scol-plugin-manager`: add from a zip, enable and
//! disable, remove, catalogue install from a local folder, refusals, the trust-root rule, the
//! Capabilities rows, and the window's use of the backend. Zips come from the shared corpus and the
//! index is signed with the TEST key, as in the `scol-catalogue` tests. Synthetic only; no process is
//! started and nothing is run in game.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use ed25519_compact::{KeyPair, Seed};
use scol_catalogue::trust::{from_hex, key_id, TrustRoot, SIGNED_PREFIX, SIG_MAGIC};
use scol_plugin_manager::Ctx;
use scol_plugins::verify::verify_archive;
use scol_ui::backend::{Backend, Command};
use scol_ui::catalogue::{self, State};
use scol_ui::plugins;
use serde_json::{json, Value};

fn launcher_rs() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn corpus_zip(name: &str) -> PathBuf {
    launcher_rs()
        .join("tests/zip-corpus/cases")
        .join(format!("{name}.zip"))
}

fn golden(name: &str) -> String {
    std::fs::read_to_string(launcher_rs().join("tests/catalogue/golden").join(name)).unwrap()
}

struct Scratch(PathBuf);

impl Scratch {
    fn new(tag: &str) -> Scratch {
        let p = std::env::temp_dir().join(format!("scol-ui-test-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        std::fs::create_dir_all(&p).unwrap();
        Scratch(p)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// A backend with the TEST key (or the shipped empty root) and a game that never runs.
struct TestBackend {
    trusted: bool,
}

impl Backend for TestBackend {
    fn trust(&self) -> TrustRoot {
        if self.trusted {
            TrustRoot::from_hex_keys(&[golden("TEST-ONLY-public.hex").trim()]).unwrap()
        } else {
            TrustRoot::embedded()
        }
    }
}

fn ctx(here: &Path, trusted: bool) -> Ctx {
    plugins::pm_ctx(here, &TestBackend { trusted })
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// `YYYY-MM-DDTHH:MM:SSZ` for a Unix time.
fn utc(t: i64) -> String {
    let days = t.div_euclid(86400);
    let secs = t.rem_euclid(86400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z",
        secs / 3600,
        secs % 3600 / 60,
        secs % 60
    )
}

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs() as i64
}

/// Write a signed index for `zip` (a corpus case) and the zip itself into the catalogue folder.
/// `corrupt` flips one byte of the zip copy (same size), so the digest in the index no longer fits.
fn write_catalogue(here: &Path, zip: &str, review: &str, sequence: u64, corrupt: bool) {
    let report = verify_archive(&corpus_zip(zip)).unwrap();
    let v: Value = json!({
        "schema": 1, "sequence": sequence,
        "issued": utc(now() - 86_400), "expires": utc(now() + 30 * 86_400),
        "plugins": [{
            "id": report.manifest.id, "name": report.manifest.name, "version": report.manifest.version,
            "kind": report.manifest.kind.as_str(), "api": report.manifest.api,
            "sdk": ">=2.4.1, <2.5", "license": "GPL-3.0-only", "maintainers": ["example-org"],
            "review": review, "evidence": "Synthetic fixture. Not run in game.",
            "source": {"repository": "https://github.com/example-org/sc-fixture",
                       "commit": "0123456789abcdef0123456789abcdef01234567", "path": "plugins/fixture"},
            "zip": {"url": format!("https://github.com/example-org/sc-plugins-fixture/releases/download/v1/{zip}.zip"),
                    "sha256": report.archive.sha256, "size": report.archive.size_bytes}
        }]
    });
    let mut bytes = serde_json::to_string(&v).unwrap().into_bytes();
    bytes.push(b'\n');
    let seed =
        Seed::from_slice(&from_hex(golden("TEST-ONLY-private-seed.hex").trim(), 32).unwrap())
            .unwrap();
    let kp = KeyPair::from_seed(seed);
    let mut msg = SIGNED_PREFIX.to_vec();
    msg.extend_from_slice(&bytes);
    let sig = format!(
        "{SIG_MAGIC} {} {}\n",
        key_id(kp.pk.as_ref()),
        hex(kp.sk.sign(&msg, None).as_ref())
    );
    let d = catalogue::dir(here);
    std::fs::create_dir_all(&d).unwrap();
    std::fs::write(d.join("index.json"), &bytes).unwrap();
    std::fs::write(d.join("index.json.sig"), sig).unwrap();
    let mut z = std::fs::read(corpus_zip(zip)).unwrap();
    if corrupt {
        let n = z.len() / 2;
        z[n] ^= 0xFF;
    }
    std::fs::write(d.join(format!("{zip}.zip")), z).unwrap();
}

#[test]
fn add_zip_lands_disabled_and_refuses_a_second_copy() {
    let s = Scratch::new("zip");
    let c = ctx(&s.0, false);
    let a = plugins::add_path(&c, &corpus_zip("good-lua-plugin")).unwrap();
    assert_eq!(a.id, "pilot");
    assert_eq!(a.kind, "lua");
    let rows = plugins::scan(&c);
    assert_eq!(rows.len(), 1);
    assert!(!rows[0].enabled, "a new plugin starts disabled");
    assert_eq!(rows[0].source, "local");
    assert!(s.0.join("data/plugins/pilot/main.lua").is_file());
    let again = plugins::add_path(&c, &corpus_zip("good-lua-plugin")).unwrap_err();
    assert!(!again.is_empty());
    plugins::set_enabled(&c, "pilot", true).unwrap();
    assert!(plugins::scan(&c)[0].enabled);
    plugins::set_enabled(&c, "pilot", false).unwrap();
    assert!(!plugins::scan(&c)[0].enabled);
}

#[test]
fn remove_takes_the_folder_and_keeps_nothing_else_in_the_list() {
    let s = Scratch::new("remove");
    let c = ctx(&s.0, false);
    plugins::add_path(&c, &corpus_zip("good-lua-plugin")).unwrap();
    plugins::remove(&c, "pilot").unwrap();
    assert!(plugins::scan(&c).is_empty());
    assert!(!s.0.join("data/plugins/pilot").exists());
    assert!(
        plugins::remove(&c, "pilot").is_err(),
        "a second remove is refused"
    );
}

#[test]
fn add_zip_shows_settings() {
    let s = Scratch::new("settings");
    let c = ctx(&s.0, false);
    plugins::add_path(&c, &corpus_zip("good-settings-block")).unwrap();
    let rows = plugins::scan(&c);
    assert_eq!(rows[0].settings.len(), 5);
    assert_eq!(rows[0].ini.kind, "native");
    assert!(rows[0].settings.iter().all(|r| !r.name.is_empty()));
    assert!(rows[0].settings.iter().any(|r| r.kind == "bool"));
    assert!(rows[0]
        .settings
        .iter()
        .any(|r| r.kind == "enum" && !r.range.is_empty()));
}

#[test]
fn add_from_a_folder_works_like_a_zip() {
    let s = Scratch::new("folder");
    let c = ctx(&s.0, false);
    let src = s.0.join("src").join("delta");
    std::fs::create_dir_all(&src).unwrap();
    std::fs::write(
        src.join("plugin.ini"),
        "id = delta\nname = Delta\nversion = 1.0\napi = 1.0\nkind = lua\nentry = main.lua\n",
    )
    .unwrap();
    std::fs::write(src.join("main.lua"), "-- synthetic\n").unwrap();
    let a = plugins::add_path(&c, &src).unwrap();
    assert_eq!((a.id.as_str(), a.name.as_str()), ("delta", "Delta"));
    assert!(!plugins::scan(&c)[0].enabled);
}

#[test]
fn hostile_zip_is_refused_and_nothing_lands() {
    let s = Scratch::new("hostile");
    let c = ctx(&s.0, false);
    let e = plugins::add_path(&c, &corpus_zip("bad-path-dotdot")).unwrap_err();
    assert!(!e.is_empty());
    assert!(plugins::scan(&c).is_empty());
}

#[test]
fn bad_plugin_id_is_refused() {
    let s = Scratch::new("badid");
    let c = ctx(&s.0, false);
    assert!(plugins::set_enabled(&c, "..", true).is_err());
    assert!(plugins::set_enabled(&c, "a/b", true).is_err());
    assert!(plugins::remove(&c, "con\\x").is_err());
    assert!(plugins::add_path(&c, &s.0.join("no-such-thing")).is_err());
}

#[test]
fn a_running_game_blocks_changes() {
    let s = Scratch::new("running");
    let mut c = ctx(&s.0, false);
    plugins::add_path(&c, &corpus_zip("good-lua-plugin")).unwrap();
    c.game_running = || true;
    let e = plugins::set_enabled(&c, "pilot", true).unwrap_err();
    assert!(e.contains("game is running"), "{e}");
    assert!(plugins::remove(&c, "pilot").is_err());
    assert!(plugins::add_path(&c, &corpus_zip("good-settings-block")).is_err());
}

#[test]
fn catalogue_install_lands_disabled_and_records_it() {
    let s = Scratch::new("cat");
    write_catalogue(&s.0, "good-lua-plugin", "reviewed", 1, false);
    let c = ctx(&s.0, true);
    let State::Loaded(loaded) = catalogue::load(&c) else {
        panic!("the index should load");
    };
    assert_eq!(loaded.rows.len(), 1);
    assert!(loaded.rows[0].refused.is_none());
    let i = catalogue::install(&c, "pilot").unwrap();
    assert_eq!((i.id.as_str(), i.kind.as_str()), ("pilot", "lua"));
    assert!(s.0.join("data/plugins/pilot/disabled").is_file());
    assert!(s.0.join("data/plugins/pilot/main.lua").is_file());
    let rec = std::fs::read_to_string(s.0.join("data/launcher/installed.json")).unwrap();
    assert!(rec.contains("\"source\": \"catalogue\"") && rec.contains("\"state\": \"disabled\""));
    assert_eq!(plugins::scan(&c)[0].source, "catalogue");
    // Loaded again, the row shows as installed.
    let State::Loaded(again) = catalogue::load(&c) else {
        panic!("the index should load again");
    };
    assert_eq!(again.rows[0].installed.as_deref(), Some("1.0"));
}

#[test]
fn catalogue_install_refuses_a_changed_download() {
    let s = Scratch::new("cat-bad");
    write_catalogue(&s.0, "good-lua-plugin", "reviewed", 1, true);
    let c = ctx(&s.0, true);
    let e = catalogue::install(&c, "pilot").unwrap_err();
    assert!(!e.is_empty());
    assert!(
        !s.0.join("data/plugins/pilot").exists(),
        "nothing lands from a download that fails the index"
    );
}

#[test]
fn the_shipped_empty_trust_root_says_no_catalogue_is_published() {
    let s = Scratch::new("cat-trust");
    write_catalogue(&s.0, "good-lua-plugin", "reviewed", 1, false);
    assert!(matches!(
        catalogue::load(&ctx(&s.0, false)),
        State::NoCatalogue
    ));
    assert!(
        catalogue::install(&ctx(&s.0, false), "pilot").is_err(),
        "with no key the index is refused, so nothing installs"
    );
    assert!(!s.0.join("data/plugins/pilot").exists());
}

#[test]
fn unreviewed_entries_are_not_listed_and_an_empty_folder_says_so() {
    let s = Scratch::new("cat-review");
    write_catalogue(&s.0, "good-lua-plugin", "community-unreviewed", 2, false);
    let State::Loaded(l) = catalogue::load(&ctx(&s.0, true)) else {
        panic!("the index should load");
    };
    assert!(l.rows.is_empty(), "unreviewed entries are never offered");
    let none = Scratch::new("cat-none");
    assert!(matches!(
        catalogue::load(&ctx(&none.0, true)),
        State::NoIndex
    ));
}

#[test]
fn a_bad_signature_is_refused() {
    let s = Scratch::new("cat-sig");
    write_catalogue(&s.0, "good-lua-plugin", "reviewed", 1, false);
    let sig = catalogue::dir(&s.0).join("index.json.sig");
    let text = std::fs::read_to_string(&sig).unwrap();
    // Change the last hex digit of the signature.
    let mut chars: Vec<char> = text.trim_end().chars().collect();
    let last = chars.len() - 1;
    chars[last] = if chars[last] == '0' { '1' } else { '0' };
    std::fs::write(&sig, chars.into_iter().collect::<String>() + "\n").unwrap();
    assert!(matches!(
        catalogue::load(&ctx(&s.0, true)),
        State::Refused(_)
    ));
}

#[test]
fn capability_registry_rows_come_from_capabilities_json() {
    let s = Scratch::new("caps");
    let rows = scol_ui::caps::registry_rows(&s.0);
    assert!(rows.len() > 10);
    assert!(rows.iter().all(|r| !r.capability.is_empty()));
    // A capabilities.json next to the launcher wins over the built-in table.
    std::fs::write(
        s.0.join("capabilities.json"),
        r#"{"capabilities":[{"capability":"x.y","evidence_level":"synthetic_only","registry_default":"OFF","switch":null}]}"#,
    )
    .unwrap();
    let rows = scol_ui::caps::registry_rows(&s.0);
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0].capability, "x.y");
    assert_eq!(rows[0].level, "synthetic_only");
    assert_eq!(rows[0].default, "OFF");
    assert!(!rows[0].has_switch);
}

// ---- the window's use of the backend ------------------------------------------------------

/// Play blocks for a moment (the game is running); Stop answers at once.
struct SlowPlay;

impl Backend for SlowPlay {
    fn run(&self, c: Command, _h: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        match c {
            Command::Play => {
                out("playing");
                std::thread::sleep(std::time::Duration::from_millis(600));
                Ok(0)
            }
            Command::Stop => {
                out("stopping");
                Ok(0)
            }
            _ => Err("not connected yet".into()),
        }
    }
}

#[test]
fn stop_runs_beside_play_and_play_stays_running() {
    use scol_ui::app::{App, Job};
    let s = Scratch::new("stop");
    let mut app = App::new(s.0.clone(), Arc::new(SlowPlay));
    app.submit(Job::Command(Command::Play));
    app.submit(Job::Command(Command::Stop));
    // Wait for the Stop line and its completion.
    let start = std::time::Instant::now();
    while !app.output.iter().any(|l| l == "stopping") || app.running.contains(&Command::Stop) {
        app.pump();
        assert!(start.elapsed().as_secs() < 5, "Stop did not finish");
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
    assert!(
        app.command_running(),
        "Play still runs after Stop finished: {:?}",
        app.running
    );
    while app.busy > 0 {
        app.pump();
        // Each finished command starts a status job that reads the real PC state (firewall rules, hosts
        // file); that takes seconds on a slow machine, so this wait is generous.
        assert!(start.elapsed().as_secs() < 180, "jobs did not finish");
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
    assert!(app.running.is_empty());
}

#[test]
fn unconnected_backend_answers_in_words() {
    let u = scol_ui::Unconnected;
    assert!(u
        .pick_folder("x")
        .unwrap_err()
        .contains("not connected yet"));
    assert!(u
        .save_plugin_setting(Path::new("."), "p", "n", "v")
        .unwrap_err()
        .contains("not connected yet"));
    assert!(u
        .run(Command::Update, Path::new("."), &mut |_| {})
        .unwrap_err()
        .contains("not connected yet"));
    assert!(!u.game_running());
    assert!(!(u.game_probe())());
}
