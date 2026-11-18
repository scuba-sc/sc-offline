// SPDX-License-Identifier: GPL-3.0-only
//! Golden checks for the contracts that live in `scol-contract`: the key=value records (install
//! marker, pc-changes.txt), the constants the C++ launcher keeps in code, the game paths and the
//! player-owned file list. The files come from the C++ launcher (`tools/launcher-contract`).
//!
//! Source and synthetic evidence only. Nothing here was run in game.

use scol_contract::{files, pc_changes, player_owned, record};
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

fn golden_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/golden")
}

/// Top-level entries of the golden tree this crate's tests consume.
const OWNED: [&str; 7] = [
    "field",
    "marker",
    "pc-changes",
    "constants.txt",
    "paths.txt",
    "player-owned.txt",
    "README.md",
];
/// Top-level entries consumed by the `scol-config` tests.
const OTHER_CRATE: [&str; 5] = ["caps", "ini", "ini-edit", "plugins", ".gitattributes"];

struct Golden {
    read: BTreeSet<PathBuf>,
    failures: Vec<String>,
}

impl Golden {
    fn text(&mut self, rel: &str) -> String {
        let path = golden_dir().join(rel);
        self.read.insert(path.clone());
        let bytes = std::fs::read(&path).unwrap_or_else(|e| panic!("cannot read {rel}: {e}"));
        String::from_utf8(bytes).unwrap_or_else(|_| panic!("{rel} is not UTF-8"))
    }

    fn expect(&mut self, what: &str, got: &str, want: &str) {
        if got != want {
            self.failures
                .push(format!("{what}\n  got:  {got:?}\n  want: {want:?}"));
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

fn key_values(text: &str) -> Vec<(String, String)> {
    text.lines()
        .map(|l| {
            let (k, v) = l.split_once('=').unwrap_or((l, ""));
            (k.to_string(), v.to_string())
        })
        .collect()
}

fn lookup(rows: &[(String, String)], key: &str) -> String {
    rows.iter()
        .find(|(k, _)| k == key)
        .map(|(_, v)| v.clone())
        .unwrap_or_else(|| panic!("no {key}"))
}

#[test]
fn golden_contracts() {
    let mut g = Golden {
        read: BTreeSet::new(),
        failures: Vec::new(),
    };

    // constants.txt
    let rows = key_values(&g.text("constants.txt"));
    g.expect("game_exe", pc_changes::GAME_EXE, &lookup(&rows, "game_exe"));
    for (key, want) in [
        ("exit.ok", scol_contract::exit_code::OK),
        ("exit.error", scol_contract::exit_code::ERROR),
        ("exit.eac", scol_contract::exit_code::EAC_ACTIVE),
        ("exit.running", scol_contract::exit_code::GAME_RUNNING),
        ("exit.restart", scol_contract::exit_code::RESTART),
    ] {
        g.expect(key, &want.to_string(), &lookup(&rows, key));
    }
    g.expect("eac_host", pc_changes::EAC_HOST, &lookup(&rows, "eac_host"));
    g.expect(
        "hosts_tag",
        pc_changes::HOSTS_TAG,
        &lookup(&rows, "hosts_tag"),
    );
    for (key, name) in pc_changes::FIREWALL_RULES {
        g.expect(
            &format!("fw_rule.{key}"),
            name,
            &lookup(&rows, &format!("fw_rule.{key}")),
        );
    }
    g.expect(
        "fw_rule count",
        &rows
            .iter()
            .filter(|(k, _)| k.starts_with("fw_rule."))
            .count()
            .to_string(),
        "4",
    );
    // The capability switches: key, variable and default, in the order of `kExperimental`.
    let exp: Vec<(String, String)> = rows
        .iter()
        .filter_map(|(k, v)| k.strip_prefix("exp.").map(|k| (k.to_string(), v.clone())))
        .collect();
    let have: Vec<(String, String)> = scol_contract::capability_env::SWITCHES
        .iter()
        .map(|s| {
            (
                s.ini_key.to_string(),
                format!("{},{}", s.env, if s.default_on { "on" } else { "off" }),
            )
        })
        .collect();
    g.expect(
        "capability switches",
        &format!("{have:?}"),
        &format!("{exp:?}"),
    );
    g.expect(
        "pc_changes_rel",
        &format!("sc-offline\\{}", files::PC_CHANGES),
        &lookup(&rows, "pc_changes_rel"),
    );

    // paths.txt
    let rows = key_values(&g.text("paths.txt"));
    let bin = "C:\\Games\\StarCitizen\\LIVE\\Bin64";
    let channel = &bin[..bin.rfind('\\').unwrap()];
    let user_dir = format!("{channel}\\{}", files::USER_DIR);
    let xml = format!("{user_dir}\\{}", files::DEFAULT_XML);
    let dll = format!("{bin}\\{}", files::MOD_DLL);
    g.expect("dll", &dll, &lookup(&rows, "dll"));
    g.expect(
        "dll_backup",
        &format!("{dll}{}", files::BACKUP_SUFFIX),
        &lookup(&rows, "dll_backup"),
    );
    g.expect(
        "marker",
        &format!("{bin}\\{}", files::MARKER),
        &lookup(&rows, "marker"),
    );
    g.expect("user_dir", &user_dir, &lookup(&rows, "user_dir"));
    g.expect("xml", &xml, &lookup(&rows, "xml"));
    g.expect(
        "xml_backup",
        &format!("{xml}{}", files::BACKUP_SUFFIX),
        &lookup(&rows, "xml_backup"),
    );

    // player-owned.txt
    for line in g.text("player-owned.txt").lines() {
        let (path, want) = line.split_once('\t').unwrap();
        g.expect(
            &format!("player-owned {path}"),
            &player_owned::is_player_owned(path).to_string(),
            want,
        );
    }

    // marker/
    for line in g.text("marker/cases.txt").lines() {
        let f: Vec<&str> = line.split('\t').collect();
        assert_eq!(f.len(), 6, "{line}");
        let want = g.text(&format!("marker/{}.installed", f[0]));
        g.expect(
            &format!("marker {}", f[0]),
            &record::marker_text(f[1], f[2], f[3], f[4], f[5]),
            &want,
        );
        // The record reads back through `field`.
        g.expect(
            &format!("marker {} xml", f[0]),
            &record::field(&want, "xml"),
            f[5],
        );
        g.expect(
            &format!("marker {} time", f[0]),
            &record::field(&want, "time"),
            f[4],
        );
        g.expect(
            &format!("marker {} mode", f[0]),
            &record::field(&want, "mode"),
            f[3],
        );
    }
    let times = g.text("marker/times.txt");
    let want = format!(
        "marker={}\nmarker={}\npc={}\npc={}\n",
        record::marker_time(2026, 1, 2, 3, 4, 5),
        record::marker_time(2026, 11, 17, 9, 5, 3),
        record::pc_time(2026, 1, 2, 3, 4),
        record::pc_time(2026, 11, 17, 9, 5)
    );
    g.expect("marker/times.txt", &want, &times);

    // field/
    for line in g.text("field/cases.txt").lines() {
        // The value is last and may itself end in a tab.
        let f: Vec<&str> = line.splitn(3, '\t').collect();
        assert_eq!(f.len(), 3, "{line:?}");
        let text = g.text(&format!("field/{}.in", f[0]));
        g.expect(
            &format!("field {}", f[0]),
            &record::field(&text, f[1]),
            f[2],
        );
    }

    // pc-changes/
    for name in g
        .text("pc-changes/cases.txt")
        .lines()
        .map(str::to_string)
        .collect::<Vec<_>>()
    {
        let mut rec = g.text(&format!("pc-changes/{name}.leftover"));
        for step in g.text(&format!("pc-changes/{name}.steps")).lines() {
            let f: Vec<&str> = step.split('\t').collect();
            match f[0] {
                "time" => {
                    if !rec.contains("time=") {
                        let n: Vec<u32> = f[1..].iter().map(|x| x.parse().unwrap()).collect();
                        record::add(
                            &mut rec,
                            pc_changes::TIME_KEY,
                            &record::pc_time(n[0], n[1], n[2], n[3], n[4]),
                        );
                    }
                }
                "add" => {
                    record::add(&mut rec, f[1], f[2]);
                }
                other => panic!("unknown step {other}"),
            }
        }
        let want = g.text(&format!("pc-changes/{name}.record"));
        g.expect(&format!("pc-changes {name}"), &rec, &want);
    }

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
            if top != "README.md" && !g.read.contains(&path) {
                g.failures
                    .push(format!("golden file never read: {}", rel.display()));
            }
        } else if !OTHER_CRATE.contains(&top.as_str()) {
            g.failures.push(format!(
                "golden entry claimed by no crate: {}",
                rel.display()
            ));
        }
    }
    assert!(
        g.read.len() > 40,
        "only {} golden files were read: is tests/golden missing?",
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
fn private_range_matches_the_cpp_table() {
    use scol_contract::network::private_range;
    assert!(private_range(0x0A00_0000, 0x0AFF_FFFF));
    assert!(private_range(0x6440_0000, 0x647F_FFFF));
    assert!(private_range(0x7F00_0001, 0x7F00_0001));
    assert!(private_range(0xA9FE_0000, 0xA9FE_FFFF));
    assert!(private_range(0xAC10_0000, 0xAC1F_FFFF));
    assert!(private_range(0xC0A8_0000, 0xC0A8_FFFF));
    assert!(!private_range(0x0800_0800, 0x0800_08FF));
    assert!(!private_range(0xAC10_0000, 0xAC20_0000));
    assert!(!private_range(0xC0A8_0000, 0xC0A9_0000));
    assert!(!private_range(5, 4));
}

#[test]
fn settings_store_format() {
    use scol_contract::settings_store as s;
    assert_eq!(s::key("speed"), "sco.settings.speed");
    assert_eq!(s::value("float", "0.5"), "float:0.5");
    assert_eq!(s::split_value("string:a:b"), Some(("string", "a:b")));
    assert_eq!(s::split_value("novalue"), None);
}
