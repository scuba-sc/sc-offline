// SPDX-License-Identifier: GPL-3.0-only
//! Golden checks: every file the C++ launcher wrote under `tests/golden` for ini parsing, ini
//! edits, the environment block and plugin.ini must be reproduced exactly by this crate.
//! The files come from `tools/launcher-contract`, which compiles the real launcher code.
//!
//! Source and synthetic evidence only. Nothing here was run in game.

use scol_config::env::{play_env, render as render_env, PlayPaths};
use scol_config::plugin_ini::{
    self, apply_kept, canon_setting, parse_plugin_ini, parse_setting_line, pending_writes,
};
use scol_config::{ini_edit, read_config, Config, Parsed};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

fn golden_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/golden")
}

/// Top-level entries of the golden tree this crate's tests consume.
const OWNED: [&str; 3] = ["ini", "ini-edit", "plugins"];
/// Top-level entries consumed by the `scol-contract` tests.
const OTHER_CRATE: [&str; 7] = [
    "field",
    "marker",
    "pc-changes",
    "constants.txt",
    "paths.txt",
    "player-owned.txt",
    "README.md",
];

struct Golden {
    read: BTreeSet<PathBuf>,
    failures: Vec<String>,
}

impl Golden {
    fn bytes(&mut self, rel: &str) -> Vec<u8> {
        let path = golden_dir().join(rel);
        self.read.insert(path.clone());
        std::fs::read(&path).unwrap_or_else(|e| panic!("cannot read {rel}: {e}"))
    }

    fn text(&mut self, rel: &str) -> String {
        String::from_utf8(self.bytes(rel)).unwrap_or_else(|_| panic!("{rel} is not UTF-8"))
    }

    fn exists(&self, rel: &str) -> bool {
        golden_dir().join(rel).exists()
    }

    fn expect(&mut self, what: &str, got: &str, want: &str) {
        if got != want {
            self.failures
                .push(format!("{what}\n  got:  {got:?}\n  want: {want:?}"));
        }
    }

    fn expect_bytes(&mut self, what: &str, got: &[u8], want: &[u8]) {
        if got != want {
            self.failures.push(format!(
                "{what}\n  got:  {:?}\n  want: {:?}",
                String::from_utf8_lossy(got),
                String::from_utf8_lossy(want)
            ));
        }
    }
}

fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            walk(&path, out);
        } else {
            out.push(path);
        }
    }
}

fn names_with_suffix(dir: &str, suffix: &str) -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(golden_dir().join(dir))
        .unwrap()
        .filter_map(|e| e.ok())
        .filter_map(|e| e.file_name().to_str().map(str::to_string))
        .filter_map(|n| n.strip_suffix(suffix).map(str::to_string))
        .collect();
    v.sort();
    v
}

fn on_off(b: bool) -> &'static str {
    if b {
        "on"
    } else {
        "off"
    }
}

/// The text `tools/launcher-contract` writes for a parse result.
fn dump_config(c: &Config, messages: &[String], returned: bool) -> String {
    let mut t = String::new();
    let mut line = |k: &str, v: &str| t.push_str(&format!("{k}={v}\n"));
    line("returned", if returned { "true" } else { "false" });
    line("game", &c.game);
    line("language", c.language.word());
    line("channel", &c.channel);
    line("boot_map", &c.boot_map);
    line("start_ship", &c.start_ship);
    line("start", &c.start);
    line("registry_probe", &c.registry_probe);
    line("block_network", on_off(c.firewall));
    line("eac_hosts", on_off(c.eac_hosts));
    line("eac_rename", on_off(c.eac_rename));
    line("clean_logs", c.clean_logs.word());
    line("check_updates", on_off(c.check_updates));
    line(
        "update_channel",
        if c.prereleases {
            "prerelease"
        } else {
            "stable"
        },
    );
    line("crash_reports", on_off(c.crash_reports));
    line("discord_off", if c.discord_off { "true" } else { "false" });
    line("plugins", on_off(c.plugins));
    line("relocation", on_off(c.relocation));
    line("multiplayer", on_off(c.multiplayer));
    line("multiplayer_allow", &c.multiplayer_allow);
    line("asop", on_off(c.asop));
    line("asop_fleet_list", &c.asop_fleet_list);
    for m in messages {
        line("message", m);
    }
    t
}

fn check_ini(g: &mut Golden) {
    let cases = names_with_suffix("ini", ".ini");
    assert!(
        cases.len() > 60,
        "only {} ini cases: is tests/golden missing?",
        cases.len()
    );
    for name in &cases {
        let bytes = g.bytes(&format!("ini/{name}.ini"));
        let Parsed { config, messages } = Config::parse(&bytes);
        let want = g.text(&format!("ini/{name}.parsed.txt"));
        g.expect(
            &format!("ini/{name} parsed"),
            &dump_config(&config, &messages, true),
            &want,
        );
        let paths = PlayPaths {
            data: "C:\\sc-offline\\data",
            user_dir: "C:\\Games\\StarCitizen\\LIVE\\user\\client\\0",
        };
        let want_env = g.text(&format!("ini/{name}.env.txt"));
        g.expect(
            &format!("ini/{name} env"),
            &render_env(&play_env(&config, paths)),
            &want_env,
        );
        // No golden file uses a capability key: the switches keep their default (on).
        g.expect(
            &format!("ini/{name} capabilities"),
            &format!("{:?}", config.capabilities),
            &format!("{:?}", scol_config::Capabilities::default()),
        );
    }
    // A file that cannot be read.
    let missing = golden_dir().join("ini/does-not-exist-on-purpose.ini");
    assert!(read_config(&missing).is_none());
    let want = g.text("ini/missing-file.parsed.txt");
    g.expect(
        "ini/missing-file",
        &dump_config(&Config::default(), &[], false),
        &want,
    );
    // Every case has all three files, and nothing else is in the folder.
    let expected: usize = cases.len() * 3 + 1;
    let have = std::fs::read_dir(golden_dir().join("ini")).unwrap().count();
    if have != expected {
        g.failures
            .push(format!("ini/ holds {have} files, expected {expected}"));
    }
}

fn scratch_dir(name: &str) -> PathBuf {
    let dir =
        std::env::temp_dir().join(format!("scol-config-golden-{}-{name}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn check_ini_edit(g: &mut Golden) {
    let dir = scratch_dir("edit");
    let list = g.text("ini-edit/cases.txt");
    assert!(list.lines().count() > 20);
    for row in list.lines() {
        let f: Vec<&str> = row.split('\t').collect();
        assert_eq!(f.len(), 7, "{row:?}");
        let (name, op, key, value, present, returned, written) =
            (f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
        let file = dir.join("edit.ini");
        let _ = std::fs::remove_file(&file);
        let input = if present == "present" {
            let bytes = g.bytes(&format!("ini-edit/{name}.in.ini"));
            std::fs::write(&file, &bytes).unwrap();
            Some(bytes)
        } else {
            None
        };
        let got_returned = match op {
            "drop" => ini_edit::drop_ini_key(&file, key),
            "set" => ini_edit::set_ini_value(&file, key, value),
            other => panic!("unknown op {other}"),
        };
        g.expect(
            &format!("ini-edit/{name} returned"),
            &got_returned.to_string(),
            returned,
        );
        let got_written = got_returned && file.exists();
        g.expect(
            &format!("ini-edit/{name} written"),
            if got_written { "written" } else { "unwritten" },
            written,
        );
        if written == "written" {
            let want = g.bytes(&format!("ini-edit/{name}.out.ini"));
            let got = std::fs::read(&file).unwrap();
            g.expect_bytes(&format!("ini-edit/{name} file"), &got, &want);
            // The byte-level editors give the same result without the file.
            let pure = match op {
                "drop" => ini_edit::drop_key(input.as_deref().unwrap_or(&[]), key),
                _ => ini_edit::set_value(input.as_deref().unwrap_or(&[]), key, value),
            };
            g.expect_bytes(&format!("ini-edit/{name} pure"), &pure, &want);
        } else if g.exists(&format!("ini-edit/{name}.out.ini")) {
            g.failures.push(format!(
                "ini-edit/{name}: an output exists for an unwritten case"
            ));
        }
    }
    let _ = std::fs::remove_dir_all(&dir);
}

fn check_plugin_files(g: &mut Golden) {
    let cases = names_with_suffix("plugins", ".parsed.txt");
    assert!(cases.len() > 25, "only {} plugin cases", cases.len());
    for name in &cases {
        let bytes = g.bytes(&format!("plugins/{name}.ini"));
        let p = parse_plugin_ini(name, &bytes);
        let want = g.text(&format!("plugins/{name}.parsed.txt"));
        g.expect(&format!("plugins/{name}"), &plugin_ini::render(&p), &want);
    }
}

fn check_canon(g: &mut Golden) {
    for row in g
        .text("plugins/canon.txt")
        .lines()
        .map(str::to_string)
        .collect::<Vec<_>>()
    {
        let f: Vec<&str> = row.split('\t').collect();
        assert_eq!(f.len(), 4, "{row:?}");
        let (decl, text, verdict, value) = (f[0], f[1], f[2], f[3]);
        let s =
            parse_setting_line("x", decl).unwrap_or_else(|| panic!("declaration {decl:?} refused"));
        // The two length rows name their text instead of holding it.
        let long = text
            .strip_prefix('<')
            .and_then(|t| t.strip_suffix(" x a>"))
            .map(|n| n.parse::<usize>().unwrap());
        let input = long
            .map(|n| "a".repeat(n))
            .unwrap_or_else(|| text.to_string());
        let (got_verdict, got_value) = match canon_setting(&s, &input) {
            Ok(v) if long.is_some() => ("ok", format!("len {}", v.len())),
            Ok(v) => ("ok", v),
            Err(_) => ("fail", String::new()),
        };
        g.expect(
            &format!("canon {decl:?} {text:?} verdict"),
            got_verdict,
            verdict,
        );
        g.expect(&format!("canon {decl:?} {text:?} value"), &got_value, value);
    }
}

/// The settings store, modelled by a sorted map of `sco_kv` rows.
fn rows_text(db: &BTreeMap<String, String>) -> String {
    db.iter().map(|(k, v)| format!("{k}\t{v}\n")).collect()
}

fn apply(db: &mut BTreeMap<String, String>, writes: Vec<(String, Option<String>)>) {
    for (k, v) in writes {
        match v {
            Some(v) => {
                db.insert(k, v);
            }
            None => {
                db.remove(&k);
            }
        }
    }
}

fn loaded_text(p: &plugin_ini::PluginIni) -> String {
    p.settings
        .iter()
        .map(|s| format!("{}\t{}\t{}\n", s.name, s.value, s.kept))
        .collect()
}

fn check_store(g: &mut Golden) {
    let decl = g.bytes("plugins/all-types.ini");
    let parse = |bytes: &[u8]| parse_plugin_ini("storedemo", bytes);
    let mut db: BTreeMap<String, String> = BTreeMap::new();
    let mut t = String::new();
    // 1. Edit four values; unchanged ones are not written.
    let p = parse(&decl);
    let edits: HashMap<String, String> = [
        ("enabled", "false"),
        ("count", "007"),
        ("speed", "0.7"),
        ("title", "New title"),
    ]
    .iter()
    .map(|(a, b)| (a.to_string(), b.to_string()))
    .collect();
    let writes = pending_writes(&p, &edits, false).expect("valid edits");
    apply(&mut db, writes);
    t.push_str("save_ok=true\nrows:\n");
    t.push_str(&rows_text(&db));
    // 2. Load them back into a fresh parse.
    let mut q = parse(&decl);
    apply_kept(&mut q, |k| db.get(k).cloned());
    t.push_str("loaded:\n");
    t.push_str(&loaded_text(&q));
    // 3. An invalid edit writes nothing and fails.
    let r = parse(&decl);
    let bad: HashMap<String, String> = [("count".to_string(), "99".to_string())].into();
    let ok = pending_writes(&r, &bad, false).is_ok();
    t.push_str(&format!("invalid_save_ok={ok}\nrows_after_invalid:\n"));
    t.push_str(&rows_text(&db));
    // 4. A kept value that no longer fits its declaration is ignored on load.
    let mut s2 = parse(b"[settings]\r\ncount = int default 5 min 1 max 3\r\nenabled = string\r\n");
    apply_kept(&mut s2, |k| db.get(k).cloned());
    t.push_str("loaded_after_redeclare:\n");
    t.push_str(&loaded_text(&s2));
    // 5. Reset removes the kept rows.
    let writes = pending_writes(&q, &HashMap::new(), true).expect("reset");
    apply(&mut db, writes);
    t.push_str("reset_ok=true\nrows_after_reset:\n");
    t.push_str(&rows_text(&db));
    let want = g.text("plugins/store.txt");
    g.expect("plugins/store.txt", &t, &want);
}

#[test]
fn golden_config() {
    let mut g = Golden {
        read: BTreeSet::new(),
        failures: Vec::new(),
    };
    check_ini(&mut g);
    check_ini_edit(&mut g);
    check_plugin_files(&mut g);
    check_canon(&mut g);
    check_store(&mut g);

    // Every file under the entries this crate owns was read, and nothing unclaimed is there.
    let mut all = Vec::new();
    walk(&golden_dir(), &mut all);
    for path in all {
        let rel = path.strip_prefix(golden_dir()).unwrap();
        let top = rel
            .components()
            .next()
            .unwrap()
            .as_os_str()
            .to_string_lossy()
            .to_string();
        if OWNED.contains(&top.as_str()) {
            if !g.read.contains(&path) {
                g.failures
                    .push(format!("golden file never read: {}", rel.display()));
            }
        } else if top != ".gitattributes" && !OTHER_CRATE.contains(&top.as_str()) {
            g.failures.push(format!(
                "golden entry claimed by no crate: {}",
                rel.display()
            ));
        }
    }
    assert!(
        g.read.len() > 200,
        "only {} golden files were read",
        g.read.len()
    );
    assert!(
        g.failures.is_empty(),
        "{} golden mismatches:\n{}",
        g.failures.len(),
        g.failures.join("\n")
    );
}

#[test]
fn capability_keys_default_on_and_can_be_turned_off() {
    use scol_config::env::{capability_env, full_env};
    use scol_contract::capability_env::{ENTITIES_DIAG, LOADOUT_ASYNC, SWITCHES};
    let defaults = Config::default();
    // Owner decision: every capability switch is on by default. The logging-only one is off.
    for s in &SWITCHES {
        assert_eq!(
            defaults.capabilities.is_on(s),
            !s.diagnostic_only,
            "{} default",
            s.env
        );
    }
    let block = capability_env(&defaults.capabilities);
    assert_eq!(block.len(), SWITCHES.len());
    assert!(block
        .iter()
        .all(|e| (e.value == "1") == (e.name != ENTITIES_DIAG)));

    let parsed =
        Config::parse(b"loadout_async = off\r\nInventory_Read = no\r\nentities_diag = maybe\r\n");
    assert!(!parsed.config.capabilities.is_on(&SWITCHES[0]));
    assert!(!parsed.config.capabilities.is_on(&SWITCHES[1]));
    assert!(parsed.config.capabilities.is_on(&SWITCHES[2]));
    assert_eq!(
        parsed.messages,
        ["[!] sc-offline.ini line 3: entities_diag must be on or off"]
    );

    let paths = PlayPaths {
        data: "D",
        user_dir: "U",
    };
    let full = full_env(&parsed.config, paths);
    assert_eq!(full.len(), 14 + SWITCHES.len());
    let get = |name: &str| full.iter().find(|e| e.name == name).unwrap();
    assert!(
        get(LOADOUT_ASYNC).is_unset(),
        "an off switch is removed from the game's environment"
    );
    assert!(
        get(ENTITIES_DIAG).is_unset(),
        "diagnostic switch is off by default"
    );
    // The first 14 entries are the C++ block, unchanged.
    assert_eq!(&full[..14], &play_env(&parsed.config, paths)[..]);
}

#[test]
fn capability_text_always_says_not_run_in_game() {
    for s in &scol_contract::capability_env::SWITCHES {
        let text = scol_config::Capabilities::status_text(s);
        assert!(text.ends_with("Not run in game."), "{text}");
        assert!(!text.to_lowercase().contains("verified"), "{text}");
    }
}

#[test]
fn allow_list_examples() {
    use scol_config::config::parse_allow_list;
    assert_eq!(
        parse_allow_list("100.64.0.0/10, 192.168.1.0/24").as_deref(),
        Some("100.64.0.0/10,192.168.1.0/24")
    );
    assert_eq!(parse_allow_list("").as_deref(), Some(""));
    assert_eq!(parse_allow_list("8.8.8.0/24"), None);
    assert_eq!(parse_allow_list("10.0.0.0/8,"), None);
}
