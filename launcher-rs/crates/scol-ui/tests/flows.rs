// SPDX-License-Identifier: GPL-3.0-only
//! Flow tests for the window's library side: add from a zip, catalogue install with a local fetcher,
//! refusals, and the Capabilities rows. Zips come from the shared corpus and the index is signed
//! with the TEST key, as in the `scol-catalogue` tests. Synthetic only; not run in game.

use std::path::{Path, PathBuf};

use ed25519_compact::{KeyPair, Seed};
use scol_catalogue::fetch::{Fetcher, Step};
use scol_catalogue::trust::{from_hex, key_id, TrustRoot, SIGNED_PREFIX, SIG_MAGIC};
use scol_plugins::verify::verify_archive;
use scol_ui::catalogue;
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
        let base = if Path::new("D:/t").is_dir() {
            PathBuf::from("D:/t")
        } else {
            std::env::temp_dir()
        };
        let p = base.join(format!("scol-ui-test-{tag}-{}", std::process::id()));
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

fn root() -> TrustRoot {
    TrustRoot::from_hex_keys(&[golden("TEST-ONLY-public.hex").trim()]).unwrap()
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// Write a signed index for `zip` (a corpus case) into the install folder.
fn write_index(here: &Path, zip: &str, review: &str, sequence: u64) {
    let report = verify_archive(&corpus_zip(zip)).unwrap();
    let v: Value = json!({
        "schema": 1, "sequence": sequence,
        "issued": "2026-11-17T00:00:00Z", "expires": "2026-12-17T00:00:00Z",
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
}

/// Serves a corpus zip for the entry's URL; `corrupt` flips a byte (same size).
struct LocalFetcher {
    zip: PathBuf,
    corrupt: bool,
}

impl Fetcher for LocalFetcher {
    fn fetch_bytes(&mut self, _url: &str, _max: u64) -> Result<Step<Vec<u8>>, String> {
        Err("no index over the network in this test".into())
    }
    fn fetch_to_file(&mut self, _url: &str, dest: &Path, _max: u64) -> Result<Step<u64>, String> {
        let mut b = std::fs::read(&self.zip).map_err(|e| e.to_string())?;
        if self.corrupt {
            let n = b.len() / 2;
            b[n] ^= 0xFF;
        }
        std::fs::write(dest, &b).map_err(|e| e.to_string())?;
        Ok(Step::Done(b.len() as u64))
    }
}

const HOSTS: (&[&str], &[&str]) = (&["github.com"], &["objects.githubusercontent.com"]);

#[test]
fn add_zip_lands_disabled_and_refuses_a_second_copy() {
    let s = Scratch::new("zip");
    let a = plugins::add_zip(&s.0, &corpus_zip("good-lua-plugin")).unwrap();
    assert_eq!(a.id, "pilot");
    let rows = plugins::scan(&s.0);
    assert_eq!(rows.len(), 1);
    assert!(!rows[0].enabled, "a new plugin starts disabled");
    assert!(s.0.join("data/plugins/pilot/main.lua").is_file());
    let again = plugins::add_zip(&s.0, &corpus_zip("good-lua-plugin")).unwrap_err();
    assert!(again.contains("already"), "{again}");
    plugins::set_enabled(&s.0, "pilot", true).unwrap();
    assert!(plugins::scan(&s.0)[0].enabled);
}

#[test]
fn add_zip_shows_settings_read_only() {
    let s = Scratch::new("settings");
    plugins::add_zip(&s.0, &corpus_zip("good-settings-block")).unwrap();
    let rows = plugins::scan(&s.0);
    assert_eq!(rows[0].settings.len(), 5);
    assert_eq!(rows[0].ini.kind, "native");
}

#[test]
fn hostile_zip_is_refused_and_nothing_lands() {
    let s = Scratch::new("hostile");
    let r = plugins::add_zip(&s.0, &corpus_zip("bad-collision-case"));
    assert!(r.is_err());
    assert!(plugins::scan(&s.0).is_empty());
}

#[test]
fn bad_plugin_id_is_refused() {
    let s = Scratch::new("badid");
    assert!(plugins::set_enabled(&s.0, "..", true).is_err());
    assert!(plugins::set_enabled(&s.0, "a/b", true).is_err());
    assert!(!plugins::valid_id("con\\x"));
}

#[test]
fn catalogue_install_lands_disabled_and_records_it() {
    let s = Scratch::new("cat");
    write_index(&s.0, "good-lua-plugin", "reviewed", 1);
    let loaded = catalogue::load(&s.0, &root()).unwrap();
    assert_eq!(loaded.rows.len(), 1);
    assert!(loaded.rows[0].refused.is_none());
    let mut f = LocalFetcher {
        zip: corpus_zip("good-lua-plugin"),
        corrupt: false,
    };
    let i = catalogue::install(&s.0, &loaded, "pilot", &mut f, HOSTS).unwrap();
    assert_eq!(i.id, "pilot");
    assert!(s.0.join("data/plugins/pilot/disabled").is_file());
    assert!(s.0.join("data/plugins/pilot/main.lua").is_file());
    assert!(
        !s.0.join("data/launcher/staging/pilot").exists(),
        "staging is cleaned"
    );
    let rec = std::fs::read_to_string(catalogue::installed_json(&s.0)).unwrap();
    assert!(rec.contains("\"source\": \"catalogue\"") && rec.contains("\"state\": \"disabled\""));
    // Loaded again, the row shows as installed and a second install is a reinstall of the same version.
    let again = catalogue::load(&s.0, &root()).unwrap();
    assert_eq!(again.rows[0].installed.as_deref(), Some("1.0"));
}

#[test]
fn catalogue_install_refuses_a_changed_download() {
    let s = Scratch::new("cat-bad");
    write_index(&s.0, "good-lua-plugin", "reviewed", 1);
    let loaded = catalogue::load(&s.0, &root()).unwrap();
    let mut f = LocalFetcher {
        zip: corpus_zip("good-lua-plugin"),
        corrupt: true,
    };
    let e = catalogue::install(&s.0, &loaded, "pilot", &mut f, HOSTS).unwrap_err();
    assert!(!e.is_empty());
    assert!(
        !s.0.join("data/plugins/pilot").exists(),
        "nothing lands from a download that fails the index"
    );
}

#[test]
fn catalogue_without_a_trusted_key_is_refused_and_unreviewed_is_not_listed() {
    let s = Scratch::new("cat-trust");
    write_index(&s.0, "good-lua-plugin", "reviewed", 1);
    let e = catalogue::load(&s.0, &TrustRoot::embedded()).unwrap_err();
    assert!(
        !e.is_empty(),
        "the built-in root has no key yet, so the index is refused"
    );
    write_index(&s.0, "good-lua-plugin", "community-unreviewed", 2);
    let l = catalogue::load(&s.0, &root()).unwrap();
    assert!(l.rows.is_empty(), "unreviewed entries are never offered");
    let none = Scratch::new("cat-none");
    assert_eq!(catalogue::load(&none.0, &root()).unwrap_err(), "");
}

#[test]
fn capability_registry_rows_come_from_capabilities_json() {
    let s = Scratch::new("caps");
    let rows = scol_ui::pages::registry_rows(&s.0);
    assert!(rows.len() > 10);
    assert!(rows.iter().all(|r| !r.0.is_empty()));
    // A capabilities.json next to the launcher wins over the built-in table.
    std::fs::write(
        s.0.join("capabilities.json"),
        r#"{"capabilities":[{"capability":"x.y","evidence_level":"synthetic_only","registry_default":"OFF","switch":null}]}"#,
    )
    .unwrap();
    let rows = scol_ui::pages::registry_rows(&s.0);
    assert_eq!(
        rows,
        vec![(
            "x.y".to_string(),
            "synthetic_only".to_string(),
            "OFF".to_string()
        )]
    );
}
