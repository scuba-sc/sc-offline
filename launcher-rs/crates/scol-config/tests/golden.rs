// SPDX-License-Identifier: GPL-3.0-only
//! Golden checks: every file the C++ launcher wrote under `tests/golden` for ini parsing, ini
//! edits, the environment block and plugin.ini must be reproduced exactly by this crate.
//! The files come from `tools/launcher-contract`, which compiles the real launcher code.
//!
//! Source and synthetic evidence only. Nothing here was run in game.

use scol_config::env::{
    apply_capabilities, play_env, render as render_env, render_capabilities, PlayPaths,
};
use scol_config::plugin_ini::{
    self, apply_kept, canon_setting, parse_plugin_ini, parse_setting_line, pending_writes,
};
use scol_config::{ini_edit, read_config, Config, Parsed};
use scol_contract::capability_env::SWITCHES;
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

fn golden_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/golden")
}

/// Top-level entries of the golden tree this crate's tests consume.
const OWNED: [&str; 4] = ["caps", "ini", "ini-edit", "plugins"];
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
    for s in &SWITCHES {
        line(s.ini_key, on_off(c.capabilities.is_on(s)));
    }
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
        // The launcher inherits every switch variable as "yes": an on key replaces it with 1, an
        // off key removes it and says so.
        let want_caps = g.text(&format!("ini/{name}.caps.txt"));
        g.expect(
            &format!("ini/{name} capability environment"),
            &render_capabilities(&apply_capabilities(&config.capabilities, &|_| true)),
            &want_caps,
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
    let expected: usize = cases.len() * 4 + 1;
    let have = std::fs::read_dir(golden_dir().join("ini")).unwrap().count();
    if have != expected {
        g.failures
            .push(format!("ini/ holds {have} files, expected {expected}"));
    }
}

/// `caps/`: a file, a set of inherited variables, and what the C++ launcher leaves in the game's
/// environment and prints about it.
fn check_caps(g: &mut Golden) {
    let list = g.text("caps/cases.txt");
    let names: Vec<&str> = list.lines().collect();
    assert!(names.len() >= 10, "only {} caps cases", names.len());
    for name in names {
        let ini = g.bytes(&format!("caps/{name}.ini"));
        let inherited_text = g.text(&format!("caps/{name}.inherited"));
        let inherited: BTreeSet<&str> = inherited_text
            .lines()
            .filter_map(|l| l.split_once('=').map(|(k, _)| k))
            .collect();
        let want = g.text(&format!("caps/{name}.result.txt"));
        let parsed = Config::parse(&ini);
        let apply = apply_capabilities(&parsed.config.capabilities, &|n| inherited.contains(n));
        g.expect(&format!("caps/{name}"), &render_capabilities(&apply), &want);
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
    check_caps(&mut g);
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

/// The environment the game process ends up with from a block: a variable with an empty value
/// is removed (`SetVar` in the C++ launcher), every other one is set.
fn applied(block: &[scol_config::EnvVar]) -> BTreeMap<&'static str, String> {
    block
        .iter()
        .filter(|e| !e.is_unset())
        .map(|e| (e.name, e.value.clone()))
        .collect()
}

#[test]
fn defaults_are_five_on_and_the_diagnostic_off() {
    let c = Config::default();
    assert_eq!(SWITCHES.len(), 6);
    for s in &SWITCHES {
        assert_eq!(s.default_on, s.env != "SCO_ENTITIES_DIAG", "{}", s.ini_key);
        assert_eq!(c.capabilities.is_on(s), !s.diagnostic_only, "{}", s.env);
    }
    let env = applied(&scol_config::env::capability_env(&c.capabilities));
    assert_eq!(env.len(), 5);
    assert!(!env.contains_key("SCO_ENTITIES_DIAG"));
    assert!(env.values().all(|v| v == "1"));
}

/// One switch at a time: on gives exactly "1" under its name; off gives no entry at all and an
/// inherited value is reported as removed.
#[test]
fn each_switch_on_holds_exactly_one_and_off_has_no_entry() {
    for target in &SWITCHES {
        for (word, on) in [("on", true), ("off", false)] {
            // The other switches get the opposite value, so the key under test must do the work.
            let others = if on { "off" } else { "on" };
            let mut ini = String::new();
            for s in &SWITCHES {
                let value = if s.env == target.env { word } else { others };
                ini.push_str(&format!("{} = {value}\r\n", s.ini_key));
            }
            let parsed = Config::parse(ini.as_bytes());
            assert!(parsed.messages.is_empty(), "{:?}", parsed.messages);
            let caps = &parsed.config.capabilities;
            let block = scol_config::env::capability_env(caps);
            assert_eq!(block.len(), SWITCHES.len());
            let env = applied(&block);
            for s in &SWITCHES {
                let want_on = if s.env == target.env { on } else { !on };
                assert_eq!(
                    caps.is_on(s),
                    want_on,
                    "{} with {} = {word}",
                    s.env,
                    target.ini_key
                );
                if want_on {
                    assert_eq!(env.get(s.env).map(String::as_str), Some("1"), "{}", s.env);
                } else {
                    assert_eq!(env.get(s.env), None, "{}", s.env);
                }
            }
            let result = apply_capabilities(caps, &|_| true);
            assert_eq!(
                result.removed.split(", ").any(|n| n == target.env),
                !on,
                "{}",
                target.env
            );
            assert_eq!(
                result.on_summary.contains(&format!("{}=1)", target.env)),
                on,
                "{}",
                target.env
            );
        }
    }
}

#[test]
fn inherited_variables_are_removed_when_the_key_is_off() {
    let parsed = Config::parse(b"exp_cvars_native = off\r\n");
    let caps = &parsed.config.capabilities;
    let none = apply_capabilities(caps, &|_| false);
    assert_eq!(none.removed, "");
    let all = apply_capabilities(caps, &|_| true);
    assert_eq!(all.removed, "SCO_CVARS_NATIVE, SCO_ENTITIES_DIAG");
    // The game's environment is the same whatever was inherited.
    assert_eq!(none.env, all.env);
    assert_eq!(
        all.on_summary,
        "exp_loadout_async (SCO_LOADOUT_ASYNC=1), exp_inventory_read (SCO_INVENTORY_READ=1), \
         exp_actor_stats_write (SCO_ACTOR_STATS_WRITE=1), exp_audio_native (SCO_AUDIO_NATIVE=1)"
    );
}

#[test]
fn capability_keys_ignore_case_and_accept_the_usual_spellings() {
    let parsed = Config::parse(
        b"EXP_LOADOUT_ASYNC = No\r\nExp_Inventory_Read = 0\r\nexp_actor_stats_write = FALSE\r\n\
          exp_cvars_native = Yes\r\nexp_audio_native = 1\r\nexp_entities_diag = True\r\n",
    );
    assert!(parsed.messages.is_empty(), "{:?}", parsed.messages);
    let on: Vec<bool> = SWITCHES
        .iter()
        .map(|s| parsed.config.capabilities.is_on(s))
        .collect();
    assert_eq!(on, [false, false, false, true, true, true]);
}

#[test]
fn old_unprefixed_keys_are_unknown_keys() {
    let parsed = Config::parse(b"loadout_async = off\r\nentities_diag = on\r\n");
    assert_eq!(
        parsed.messages,
        [
            "[!] sc-offline.ini line 1: unknown key 'loadout_async'",
            "[!] sc-offline.ini line 2: unknown key 'entities_diag'"
        ]
    );
    assert_eq!(
        parsed.config.capabilities,
        scol_config::Capabilities::default()
    );
}

#[test]
fn malformed_values_restore_the_default_even_after_a_valid_line() {
    let parsed = Config::parse(
        b"exp_loadout_async = off\r\nexp_loadout_async = maybe\r\n\
          exp_entities_diag = on\r\nexp_entities_diag = nope\r\n\
          exp_audio_native =\r\nexp_cvars_native = 2\r\n",
    );
    assert_eq!(
        parsed.messages,
        [
            "[!] sc-offline.ini line 2: exp_loadout_async must be on or off; left on",
            "[!] sc-offline.ini line 4: exp_entities_diag must be on or off; left off",
            "[!] sc-offline.ini line 5: exp_audio_native must be on or off; left on",
            "[!] sc-offline.ini line 6: exp_cvars_native must be on or off; left on",
        ]
    );
    assert_eq!(
        parsed.config.capabilities,
        scol_config::Capabilities::default()
    );
}

#[test]
fn duplicate_keys_last_valid_line_wins() {
    let off_on = Config::parse(b"exp_inventory_read = off\r\nexp_inventory_read = on\r\n");
    assert!(off_on.config.capabilities.is_on(&SWITCHES[1]));
    let on_off = Config::parse(b"exp_inventory_read = on\r\nexp_inventory_read = off\r\n");
    assert!(!on_off.config.capabilities.is_on(&SWITCHES[1]));
    assert!(on_off.messages.is_empty());
    // A duplicate with different case is the same key.
    let mixed = Config::parse(b"exp_inventory_read = off\r\nEXP_INVENTORY_READ = on\r\n");
    assert!(mixed.config.capabilities.is_on(&SWITCHES[1]));
}

#[test]
fn crlf_lf_mixed_bom_and_missing_final_newline_give_the_same_switches() {
    let body = "exp_loadout_async = off\nexp_entities_diag = on\nexp_audio_native = off";
    let state =
        |c: &Config| -> Vec<bool> { SWITCHES.iter().map(|s| c.capabilities.is_on(s)).collect() };
    let lf = Config::parse(body.as_bytes());
    assert_eq!(state(&lf.config), [false, true, true, true, false, true]);
    assert!(lf.messages.is_empty());
    let crlf = Config::parse(body.replace('\n', "\r\n").as_bytes());
    let mut bom = vec![0xEF, 0xBB, 0xBF];
    bom.extend_from_slice(body.replace('\n', "\r\n").as_bytes());
    let bom = Config::parse(&bom);
    let mut bom_lf = vec![0xEF, 0xBB, 0xBF];
    bom_lf.extend_from_slice(body.as_bytes());
    let bom_lf = Config::parse(&bom_lf);
    let mixed = Config::parse(
        b"exp_loadout_async = off\r\nexp_entities_diag = on\nexp_audio_native = off\r\n",
    );
    for p in [&crlf, &bom, &bom_lf, &mixed] {
        assert_eq!(state(&p.config), state(&lf.config));
        assert!(p.messages.is_empty(), "{:?}", p.messages);
    }
}

/// The shipped `launcher/sc-offline.ini`, read from the repository: the golden tree holds a CRLF
/// copy of it, the C++ launcher's reading of that copy, and the environment it leaves.
#[test]
fn shipped_ini_golden() {
    let shipped_path =
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../launcher/sc-offline.ini");
    let shipped = std::fs::read(&shipped_path)
        .unwrap_or_else(|e| panic!("cannot read the shipped sc-offline.ini: {e}"));
    let text = String::from_utf8(shipped.clone()).expect("shipped ini is UTF-8");
    let mut g = Golden {
        read: BTreeSet::new(),
        failures: Vec::new(),
    };

    // The golden copy is this file with CRLF line endings.
    let crlf = text.replace("\r\n", "\n").replace('\n', "\r\n");
    let golden_copy = g.bytes("ini/shipped.ini");
    g.expect_bytes(
        "golden copy of the shipped ini",
        crlf.as_bytes(),
        &golden_copy,
    );

    // Reading it gives no message and the decided defaults, every key written out once.
    let parsed = Config::parse(&shipped);
    assert!(parsed.messages.is_empty(), "{:?}", parsed.messages);
    assert_eq!(
        parsed.config.capabilities,
        scol_config::Capabilities::default()
    );
    for s in &SWITCHES {
        let lines: Vec<&str> = text
            .lines()
            .filter(|l| {
                !l.trim_start().starts_with('#')
                    && l.split_once('=')
                        .is_some_and(|(k, _)| k.trim().eq_ignore_ascii_case(s.ini_key))
            })
            .collect();
        assert_eq!(lines.len(), 1, "{} must be a visible key, once", s.ini_key);
        let value = lines[0].split_once('=').unwrap().1.trim();
        assert_eq!(
            value,
            if s.default_on { "on" } else { "off" },
            "{}",
            s.ini_key
        );
        // The comment line above the key says it has not been run in game.
        let comment = format!("# {}:", s.ini_key);
        let line = text
            .lines()
            .find(|l| l.starts_with(&comment))
            .unwrap_or_else(|| panic!("no comment for {}", s.ini_key));
        assert!(line.contains("Not run in game"), "{line}");
    }

    // Same results as the C++ launcher wrote for the file.
    let want = g.text("ini/shipped.parsed.txt");
    g.expect(
        "shipped parsed",
        &dump_config(&parsed.config, &parsed.messages, true),
        &want,
    );
    let paths = PlayPaths {
        data: "C:\\sc-offline\\data",
        user_dir: "C:\\Games\\StarCitizen\\LIVE\\user\\client\\0",
    };
    let want_env = g.text("ini/shipped.env.txt");
    g.expect(
        "shipped env",
        &render_env(&play_env(&parsed.config, paths)),
        &want_env,
    );
    let want_caps = g.text("ini/shipped.caps.txt");
    g.expect(
        "shipped capability environment",
        &render_capabilities(&apply_capabilities(&parsed.config.capabilities, &|_| true)),
        &want_caps,
    );
    // The five on switches are exactly "1" and the diagnostic one has no entry.
    let env = applied(&scol_config::env::capability_env(
        &parsed.config.capabilities,
    ));
    assert_eq!(env.len(), 5);
    assert!(env.values().all(|v| v == "1"));
    assert!(!env.contains_key("SCO_ENTITIES_DIAG"));

    assert!(g.failures.is_empty(), "{}", g.failures.join("\n"));
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
