// SPDX-License-Identifier: GPL-3.0-only
//! Shared helpers for the integration tests: scratch folders, a test signer, a synthetic plugin.
#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

use ed25519_compact::{KeyPair, Seed};
use scol_catalogue::policy::Host;
use scol_catalogue::semver::Version;
use scol_catalogue::trust::{key_id, TrustRoot, SIGNED_PREFIX, SIG_MAGIC};
use scol_plugin_manager::cli::{run_with, Capture, Ctx};
use scol_plugin_manager::layout::Layout;
use scol_plugin_manager::txn::Fault;
use scol_plugin_manager::zipx::{collect_folder, write_stored_zip, Member};
use scol_plugins::sha256::hex_digest;
use serde_json::{json, Value};

/// 2026-11-18T11:06:40Z
pub const NOW: i64 = 1_795_000_000;
const KEY_SEED: [u8; 32] = [7u8; 32];

fn no_game() -> bool {
    false
}

pub struct Scratch {
    pub name: String,
    pub dir: PathBuf,
    /// The launcher folder, `<dir>/launcher`.
    pub root: PathBuf,
}

static COUNTER: AtomicU32 = AtomicU32::new(0);

impl Scratch {
    pub fn new(name: &str) -> Scratch {
        let name = format!(
            "{}-{}",
            name.replace(['{', '}', ':', ' '], "_"),
            COUNTER.fetch_add(1, Ordering::SeqCst)
        );
        let dir = std::env::temp_dir()
            .join(format!("scol-pm-{}", std::process::id()))
            .join(&name);
        let _ = std::fs::remove_dir_all(&dir);
        let root = dir.join("launcher");
        std::fs::create_dir_all(&root).unwrap();
        Scratch { name, dir, root }
    }

    pub fn ctx(&self) -> Ctx {
        self.ctx_with_key(KEY_SEED)
    }

    /// A context that trusts the key made from `seed`.
    pub fn ctx_with_key(&self, seed: [u8; 32]) -> Ctx {
        let kp = KeyPair::from_seed(Seed::new(seed));
        let hex: String = kp.pk.iter().map(|b| format!("{b:02x}")).collect();
        Ctx {
            layout: Layout::new(&self.root),
            now: NOW,
            game_running: no_game,
            trust: TrustRoot::from_hex_keys(&[&hex]).unwrap(),
            host: Host {
                api_major: 1,
                api_minor: 1,
                sdk: Version::parse("2.4.1").unwrap(),
            },
            fault: Fault::None,
        }
    }

    pub fn make_plugin_folder(&self, version: &str) -> PathBuf {
        let d = self.dir.join(format!("src-{version}")).join("demo");
        std::fs::create_dir_all(&d).unwrap();
        std::fs::write(d.join("plugin.ini"), demo_ini(version)).unwrap();
        std::fs::write(d.join("demo.dll"), format!("MZ-demo-{version}")).unwrap();
        d
    }

    pub fn make_plugin_zip(&self, version: &str, out: &Path) -> PathBuf {
        let d = self.make_plugin_folder(version);
        let members = collect_folder(&d, "demo", "disabled").unwrap();
        write_stored_zip(out, &members).unwrap();
        out.to_path_buf()
    }

    pub fn catalogue_dir(&self) -> PathBuf {
        let d = self.dir.join("catalogue");
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    /// Write `demo-<version>.zip`, a signed `index.json` and `index.json.sig` into `cat`.
    pub fn publish(&self, cat: &Path, version: &str, sequence: u64, review: Review) {
        let zip = cat.join(format!("demo-{version}.zip"));
        self.make_plugin_zip(version, &zip);
        let bytes = std::fs::read(&zip).unwrap();
        let index = json!({
            "schema": 1,
            "sequence": sequence,
            "issued": "2026-11-17T00:00:00Z",
            "expires": "2026-12-17T00:00:00Z",
            "plugins": [{
                "id": "demo",
                "name": "Demo",
                "version": version,
                "kind": "native",
                "api": "1.1",
                "sdk": ">=2.4.1, <2.5",
                "license": "GPL-3.0-only",
                "maintainers": ["scuba-sc"],
                "review": review.text(),
                "evidence": "Synthetic test plugin; not run in game.",
                "source": {
                    "repository": "https://github.com/scuba-sc/sc-demo",
                    "commit": "0123456789abcdef0123456789abcdef01234567",
                    "path": "plugins/demo"
                },
                "zip": {
                    "url": format!("https://github.com/scuba-sc/sc-demo/releases/download/v{version}/demo-{version}.zip"),
                    "sha256": hex_digest(&bytes),
                    "size": bytes.len()
                }
            }]
        });
        let mut text = serde_json::to_string(&index).unwrap();
        text.push('\n');
        std::fs::write(cat.join("index.json"), &text).unwrap();
        let kp = KeyPair::from_seed(Seed::new(KEY_SEED));
        let mut msg = SIGNED_PREFIX.to_vec();
        msg.extend_from_slice(text.as_bytes());
        let sig = kp.sk.sign(&msg, None);
        let sig_hex: String = sig.iter().map(|b| format!("{b:02x}")).collect();
        std::fs::write(
            cat.join("index.json.sig"),
            format!("{SIG_MAGIC} {} {sig_hex}\n", key_id(kp.pk.as_ref())),
        )
        .unwrap();
    }
}

#[derive(Clone, Copy)]
pub enum Review {
    FirstParty,
    Unreviewed,
}

impl Review {
    fn text(self) -> &'static str {
        match self {
            Review::FirstParty => "first-party",
            Review::Unreviewed => "community-unreviewed",
        }
    }
}

pub fn demo_ini(version: &str) -> String {
    format!(
        "id = demo\nname = Demo\nversion = {version}\nauthor = tests\napi = 1.1\nkind = native\nentry = demo.dll\n"
    )
}

pub fn run(ctx: &Ctx, args: &[&str]) -> (i32, Capture) {
    let args: Vec<String> = args.iter().map(|s| s.to_string()).collect();
    let mut io = Capture::default();
    let rc = run_with(&args, &mut io, ctx);
    (rc, io)
}

/// Relative path (forward slashes) to `dir` or the SHA-256 of the file.
pub fn snapshot(root: &Path) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    fn walk(base: &Path, dir: &Path, m: &mut BTreeMap<String, String>) {
        let Ok(rd) = std::fs::read_dir(dir) else {
            return;
        };
        for e in rd.flatten() {
            let p = e.path();
            let rel = p
                .strip_prefix(base)
                .unwrap()
                .to_string_lossy()
                .replace('\\', "/");
            if p.is_dir() {
                m.insert(rel, "dir".into());
                walk(base, &p, m);
            } else {
                m.insert(rel, hex_digest(&std::fs::read(&p).unwrap()));
            }
        }
    }
    walk(root, root, &mut m);
    m
}

/// Every difference between the two trees is at or below one of `allowed` (or is a parent
/// folder of one).
pub fn assert_only_under(
    before: &BTreeMap<String, String>,
    after: &BTreeMap<String, String>,
    allowed: &[&str],
) {
    let ok = |k: &str| {
        allowed
            .iter()
            .any(|a| k == *a || k.starts_with(&format!("{a}/")) || a.starts_with(&format!("{k}/")))
    };
    for (k, v) in after {
        if before.get(k) != Some(v) {
            assert!(ok(k), "unexpected change: {k}");
        }
    }
    for k in before.keys() {
        if !after.contains_key(k) {
            assert!(ok(k), "unexpected removal: {k}");
        }
    }
}

pub fn read_json(p: &Path) -> Value {
    serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap()
}

pub fn installed_json(root: &Path) -> Value {
    read_json(&root.join("data/launcher/installed.json"))
}

pub fn dir_is_empty(p: &Path) -> bool {
    std::fs::read_dir(p)
        .map(|mut r| r.next().is_none())
        .unwrap_or(true)
}

/// Write a stored zip with arbitrary member names (the names are not checked here).
pub fn write_zip(out: &Path, files: &[(&str, &[u8])]) {
    let members: Vec<Member> = files
        .iter()
        .map(|(n, d)| Member {
            name: n.to_string(),
            data: d.to_vec(),
        })
        .collect();
    write_stored_zip(out, &members).unwrap();
}
