// SPDX-License-Identifier: GPL-3.0-only
//! End to end tests of `plugins ...` against scratch launcher folders: local install, enable,
//! disable, remove, the journal under every fault, and the signed catalogue (list, install,
//! update, replay protection, tampering). Synthetic plugin, TEST signing key, nothing run in game.

mod common;

use common::*;
use scol_plugin_manager::txn::{self, Fault};

#[test]
fn list_empty_and_hand_copied_folder() {
    let t = Scratch::new("list");
    let ctx = t.ctx();
    let (rc, io) = run(&ctx, &["list"]);
    assert_eq!(rc, 0);
    assert_eq!(io.text(), "no plugins installed");
    // A hand-copied folder shows as local and enabled (no marker).
    let dir = t.root.join("data/plugins/demo");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("plugin.ini"), demo_ini("1.0.0")).unwrap();
    std::fs::write(dir.join("demo.dll"), b"MZ-demo").unwrap();
    let (rc, io) = run(&ctx, &["list"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    let text = io.text();
    assert!(
        text.contains("demo  Demo  1.0.0  [enabled]  native plugin"),
        "{text}"
    );
    assert!(text.contains("source local"), "{text}");
    assert!(text.contains("native code"), "{text}");
    let (_, io) = run(&ctx, &["list", "--json"]);
    let v: serde_json::Value = serde_json::from_str(&io.text()).unwrap();
    assert_eq!(v[0]["id"], "demo");
    assert_eq!(v[0]["state"], "enabled");
}

#[test]
fn local_folder_install_enable_disable_remove() {
    let t = Scratch::new("local");
    let ctx = t.ctx();
    let src = t.make_plugin_folder("1.0.0");
    let before = snapshot(&t.root);
    let (rc, io) = run(&ctx, &["install", src.to_str().unwrap()]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    assert!(io.text().contains("it is disabled"));
    assert!(
        io.text().contains("native code"),
        "native warning: {}",
        io.text()
    );
    // Lands disabled, with the record.
    assert!(t.root.join("data/plugins/demo/disabled").is_file());
    assert!(t.root.join("data/plugins/demo/demo.dll").is_file());
    let rec = installed_json(&t.root);
    assert_eq!(rec["plugins"][0]["source"], "local");
    assert_eq!(rec["plugins"][0]["state"], "disabled");
    // Only these places changed.
    assert_only_under(
        &before,
        &snapshot(&t.root),
        &["data/plugins/demo", "data/launcher"],
    );
    // Nothing left in staging, no journal.
    assert!(!t.root.join("data/launcher/plugin-journal.json").exists());
    assert!(
        !t.root.join("data/launcher/staging").exists()
            || dir_is_empty(&t.root.join("data/launcher/staging"))
    );

    let (_, io) = run(&ctx, &["list"]);
    assert!(io.text().contains("[disabled]"));
    let (rc, _) = run(&ctx, &["enable", "demo"]);
    assert_eq!(rc, 0);
    assert!(!t.root.join("data/plugins/demo/disabled").exists());
    assert_eq!(installed_json(&t.root)["plugins"][0]["state"], "enabled");
    let (_, io) = run(&ctx, &["list"]);
    assert!(io.text().contains("[enabled]"), "{}", io.text());
    let (rc, _) = run(&ctx, &["disable", "demo"]);
    assert_eq!(rc, 0);
    assert!(t.root.join("data/plugins/demo/disabled").is_file());

    // Remove keeps settings unless --purge.
    let db = t.root.join("data/storage/demo.db");
    std::fs::create_dir_all(db.parent().unwrap()).unwrap();
    std::fs::write(&db, b"settings").unwrap();
    let (rc, io) = run(&ctx, &["remove", "demo"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    assert!(!t.root.join("data/plugins/demo").exists());
    assert!(db.is_file(), "settings are kept");
    assert!(io.text().contains("--purge"));
    assert_eq!(
        installed_json(&t.root)["plugins"].as_array().unwrap().len(),
        0
    );
    let (rc, _) = run(&ctx, &["remove", "demo"]);
    assert_eq!(rc, 1, "removing twice is refused");
    // Install again, remove with purge.
    assert_eq!(run(&ctx, &["install", src.to_str().unwrap()]).0, 0);
    let (rc, io) = run(&ctx, &["remove", "demo", "--purge"]);
    assert_eq!(rc, 0);
    assert!(!db.exists());
    assert!(io.text().contains("purged 1"));
}

#[test]
fn zip_install_and_replace() {
    let t = Scratch::new("zip");
    let ctx = t.ctx();
    let zip = t.make_plugin_zip(
        "1.0.0",
        &t.root
            .parent()
            .unwrap()
            .join(format!("{}-demo.zip", t.name)),
    );
    assert_eq!(run(&ctx, &["install", zip.to_str().unwrap()]).0, 0);
    let (rc, io) = run(&ctx, &["install", zip.to_str().unwrap()]);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("--replace"));
    let zip2 = t.make_plugin_zip(
        "1.0.1",
        &t.root
            .parent()
            .unwrap()
            .join(format!("{}-demo2.zip", t.name)),
    );
    run(&ctx, &["enable", "demo"]);
    let (rc, io) = run(&ctx, &["install", zip2.to_str().unwrap(), "--replace"]);
    assert_eq!(rc, 0, "{}", io.err.join("\n"));
    // A replace lands disabled again, new version.
    assert!(t.root.join("data/plugins/demo/disabled").is_file());
    assert_eq!(installed_json(&t.root)["plugins"][0]["version"], "1.0.1");
}

#[test]
fn refuses_while_game_runs_and_changes_nothing() {
    let t = Scratch::new("running");
    let mut ctx = t.ctx();
    let src = t.make_plugin_folder("1.0.0");
    assert_eq!(run(&ctx, &["install", src.to_str().unwrap()]).0, 0);
    ctx.game_running = || true;
    let before = snapshot(&t.root);
    for args in [
        vec!["install", src.to_str().unwrap(), "--replace"],
        vec!["enable", "demo"],
        vec!["disable", "demo"],
        vec!["remove", "demo"],
    ] {
        let (rc, io) = run(&ctx, &args);
        assert_eq!(rc, 1, "{args:?}");
        assert!(io.err.join("\n").contains("game is running"), "{args:?}");
    }
    assert_eq!(snapshot(&t.root), before);
    // list still works while the game runs.
    assert_eq!(run(&ctx, &["list"]).0, 0);
}

#[test]
fn refuses_bad_packages_and_ids_without_writing() {
    let t = Scratch::new("bad");
    let ctx = t.ctx();
    let before = snapshot(&t.root);
    // Path escape inside a zip.
    let evil = t
        .root
        .parent()
        .unwrap()
        .join(format!("{}-evil.zip", t.name));
    write_zip(
        &evil,
        &[
            ("demo/plugin.ini", demo_ini("1.0.0").as_bytes()),
            ("demo/demo.dll", b"MZ"),
            ("demo/../../escape.txt", b"x"),
        ],
    );
    let (rc, io) = run(&ctx, &["install", evil.to_str().unwrap()]);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("refused"), "{:?}", io.err);
    // Folder name differs from id.
    let wrong = t
        .root
        .parent()
        .unwrap()
        .join(format!("{}-wrong.zip", t.name));
    write_zip(
        &wrong,
        &[
            ("other/plugin.ini", demo_ini("1.0.0").as_bytes()),
            ("other/demo.dll", b"MZ"),
        ],
    );
    assert_eq!(run(&ctx, &["install", wrong.to_str().unwrap()]).0, 1);
    // Reserved id.
    let reserved = t
        .root
        .parent()
        .unwrap()
        .join(format!("{}-host.zip", t.name));
    let ini = demo_ini("1.0.0")
        .replace("id = demo", "id = host")
        .replace("name = Demo", "name = Host");
    write_zip(
        &reserved,
        &[
            ("host/plugin.ini", ini.as_bytes()),
            ("host/demo.dll", b"MZ"),
        ],
    );
    let (rc, io) = run(&ctx, &["install", reserved.to_str().unwrap()]);
    assert_eq!(rc, 1, "{:?}", io.err);
    // Newer plugin api than the host.
    let newer = t.root.parent().unwrap().join(format!("{}-api.zip", t.name));
    let ini = demo_ini("1.0.0").replace("api = 1.1", "api = 1.9");
    write_zip(
        &newer,
        &[
            ("demo/plugin.ini", ini.as_bytes()),
            ("demo/demo.dll", b"MZ"),
        ],
    );
    assert_eq!(run(&ctx, &["install", newer.to_str().unwrap()]).0, 1);
    // Ids that are paths.
    for id in ["../x", "a/b", "A", "", "a b"] {
        assert_eq!(run(&ctx, &["enable", id]).0, 1, "{id:?}");
        assert_eq!(run(&ctx, &["remove", id]).0, 1, "{id:?}");
    }
    // Nothing changed in the launcher folder, and nothing escaped next to it.
    assert_eq!(snapshot(&t.root), before);
    assert!(!t.root.parent().unwrap().join("escape.txt").exists());
    assert!(!t.root.join("escape.txt").exists());
}

#[test]
fn journal_rolls_back_at_every_fault() {
    for fault in [
        Fault::AfterJournal,
        Fault::AfterBackup,
        Fault::AfterSwap,
        Fault::AfterRecord,
    ] {
        // Fresh install interrupted.
        let t = Scratch::new(&format!("fault-new-{fault:?}"));
        let mut ctx = t.ctx();
        let src = t.make_plugin_folder("1.0.0");
        let before = snapshot(&t.root);
        ctx.fault = fault;
        let (rc, _) = run(&ctx, &["install", src.to_str().unwrap()]);
        assert_eq!(rc, 1, "{fault:?}");
        assert!(
            t.root.join("data/launcher/plugin-journal.json").exists(),
            "{fault:?}"
        );
        ctx.fault = Fault::None;
        let note = txn::recover(&ctx.layout).unwrap();
        assert!(note.is_some(), "{fault:?}");
        let after = snapshot(&t.root);
        let strip = |m: &std::collections::BTreeMap<String, String>| {
            m.iter()
                .filter(|(k, _)| !k.starts_with("data"))
                .map(|(k, v)| (k.clone(), v.clone()))
                .collect::<std::collections::BTreeMap<_, _>>()
        };
        assert_eq!(strip(&after), strip(&before), "{fault:?}");
        assert!(
            !t.root.join("data/plugins/demo").exists(),
            "{fault:?}: no half folder"
        );
        assert!(
            !t.root.join("data/launcher/installed.json").exists(),
            "{fault:?}"
        );
        assert!(!t.root.join("data/launcher/plugin-journal.json").exists());

        // Replace interrupted: the old install comes back byte for byte.
        let t = Scratch::new(&format!("fault-rep-{fault:?}"));
        let mut ctx = t.ctx();
        let v1 = t.make_plugin_folder("1.0.0");
        assert_eq!(run(&ctx, &["install", v1.to_str().unwrap()]).0, 0);
        run(&ctx, &["enable", "demo"]);
        let v2 = t.make_plugin_folder("2.0.0");
        let before = snapshot(&t.root);
        ctx.fault = fault;
        assert_eq!(
            run(&ctx, &["install", v2.to_str().unwrap(), "--replace"]).0,
            1
        );
        ctx.fault = Fault::None;
        let note = txn::recover(&ctx.layout).unwrap();
        assert!(note.is_some(), "{fault:?}");
        assert_eq!(snapshot(&t.root), before, "{fault:?}");

        // Remove interrupted.
        let before_rm = snapshot(&t.root);
        ctx.fault = fault;
        assert_eq!(run(&ctx, &["remove", "demo"]).0, 1);
        ctx.fault = Fault::None;
        // Any changing command first rolls the half done one back.
        let (rc, io) = run(&ctx, &["enable", "demo"]);
        assert_eq!(rc, 0, "{:?}", io.err);
        assert!(
            io.text().contains("did not finish"),
            "{fault:?}: {}",
            io.text()
        );
        assert_eq!(snapshot(&t.root), before_rm, "{fault:?}");
    }
}

#[test]
fn corrupt_installed_json_is_never_overwritten() {
    let t = Scratch::new("corrupt");
    let ctx = t.ctx();
    std::fs::create_dir_all(t.root.join("data/launcher")).unwrap();
    std::fs::write(t.root.join("data/launcher/installed.json"), b"{ not json").unwrap();
    let src = t.make_plugin_folder("1.0.0");
    let before = snapshot(&t.root);
    let (rc, io) = run(&ctx, &["install", src.to_str().unwrap()]);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("installed.json"));
    assert_eq!(snapshot(&t.root), before);
}

// ---- catalogue ------------------------------------------------------------------------------

#[test]
fn catalogue_list_install_enable_update_remove() {
    let t = Scratch::new("cat");
    let ctx = t.ctx();
    let cat = t.catalogue_dir();
    t.publish(&cat, "1.0.0", 5, Review::FirstParty);
    let src = ["--source", cat.to_str().unwrap()];
    let before = snapshot(&t.root);

    let (rc, io) = run(&ctx, &[&["catalogue", "list"][..], &src].concat());
    assert_eq!(rc, 0, "{:?}", io.err);
    let text = io.text();
    assert!(
        text.contains("first index seen: sequence 5 is now pinned"),
        "{text}"
    );
    assert!(text.contains("demo 1.0.0 (native)"), "{text}");
    assert!(
        text.contains("not installed; can be installed here"),
        "{text}"
    );
    // Only the pin was written.
    assert_only_under(
        &before,
        &snapshot(&t.root),
        &["data/launcher/catalogue-state.json"],
    );
    let st: serde_json::Value = read_json(&t.root.join("data/launcher/catalogue-state.json"));
    assert_eq!(st["sequence"], 5);

    let before = snapshot(&t.root);
    let (rc, io) = run(
        &ctx,
        &[&["catalogue", "install", "demo"][..], &src].concat(),
    );
    assert_eq!(rc, 0, "{:?}", io.err);
    assert!(io.text().contains("it is disabled"), "{}", io.text());
    assert!(io.text().contains("native code"), "{}", io.text());
    assert!(t.root.join("data/plugins/demo/disabled").is_file());
    let rec = installed_json(&t.root);
    assert_eq!(rec["plugins"][0]["source"], "catalogue");
    assert_eq!(rec["plugins"][0]["index_sequence"], 5);
    assert_eq!(rec["plugins"][0]["zip_sha256"].as_str().unwrap().len(), 64);
    assert_only_under(
        &before,
        &snapshot(&t.root),
        &[
            "data/plugins/demo",
            "data/launcher/installed.json",
            "data/launcher/staging",
        ],
    );

    assert_eq!(run(&ctx, &["enable", "demo"]).0, 0);
    let (_, io) = run(&ctx, &["list"]);
    assert!(
        io.text().contains("demo  Demo  1.0.0  [enabled]"),
        "{}",
        io.text()
    );
    assert!(io.text().contains("source catalogue"));

    // Same index again: accepted (same bytes), up to date.
    let (rc, io) = run(&ctx, &[&["update"][..], &src].concat());
    assert_eq!(rc, 0, "{:?}", io.err);
    assert!(
        io.text().contains("demo: up to date (1.0.0)"),
        "{}",
        io.text()
    );

    // A newer index: update lands disabled again, settings kept.
    let db = t.root.join("data/storage/demo.db");
    std::fs::create_dir_all(db.parent().unwrap()).unwrap();
    std::fs::write(&db, b"settings").unwrap();
    t.publish(&cat, "1.1.0", 6, Review::FirstParty);
    let (rc, io) = run(&ctx, &[&["update"][..], &src].concat());
    assert_eq!(rc, 0, "{:?}", io.err);
    assert!(
        io.text().contains("updating demo 1.0.0 -> 1.1.0"),
        "{}",
        io.text()
    );
    assert!(io.text().contains("replaced catalogue install 1.0.0"));
    assert!(t.root.join("data/plugins/demo/disabled").is_file());
    assert_eq!(installed_json(&t.root)["plugins"][0]["version"], "1.1.0");
    assert_eq!(std::fs::read(&db).unwrap(), b"settings");
    assert_eq!(
        read_json(&t.root.join("data/launcher/catalogue-state.json"))["sequence"],
        6
    );

    assert_eq!(run(&ctx, &["remove", "demo"]).0, 0);
    assert!(!t.root.join("data/plugins/demo").exists());
    assert_eq!(
        installed_json(&t.root)["plugins"].as_array().unwrap().len(),
        0
    );
}

#[test]
fn catalogue_replay_and_rollback_across_runs() {
    let t = Scratch::new("replay");
    let ctx = t.ctx();
    let cat = t.catalogue_dir();
    let src = ["--source", cat.to_str().unwrap()];
    let list = [&["catalogue", "list"][..], &src].concat();
    t.publish(&cat, "1.1.0", 7, Review::FirstParty);
    assert_eq!(run(&ctx, &list).0, 0);
    // An older (validly signed) index is a rollback: refused, pin unchanged.
    t.publish(&cat, "1.0.0", 6, Review::FirstParty);
    let (rc, io) = run(&ctx, &list);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("rollback"), "{:?}", io.err);
    assert_eq!(
        read_json(&t.root.join("data/launcher/catalogue-state.json"))["sequence"],
        7
    );
    // Same sequence, different bytes: refused.
    t.publish(&cat, "1.0.5", 7, Review::FirstParty);
    let (rc, io) = run(&ctx, &list);
    assert_eq!(rc, 1);
    assert!(
        io.err.join("\n").contains("sequence-replay"),
        "{:?}",
        io.err
    );
    // Nothing was installed by any of this.
    assert!(!t.root.join("data/plugins").exists());
    // A corrupt pin is not replaced silently.
    std::fs::write(t.root.join("data/launcher/catalogue-state.json"), b"junk").unwrap();
    let (rc, io) = run(&ctx, &list);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("catalogue-state.json"));
}

#[test]
fn catalogue_refuses_tampering() {
    let t = Scratch::new("tamper");
    let ctx = t.ctx();
    let cat = t.catalogue_dir();
    let src = ["--source", cat.to_str().unwrap()];
    let install = [&["catalogue", "install", "demo"][..], &src].concat();
    let list = [&["catalogue", "list"][..], &src].concat();

    // Wrong signature (index bytes changed after signing).
    t.publish(&cat, "1.0.0", 1, Review::FirstParty);
    let idx = std::fs::read(cat.join("index.json")).unwrap();
    let mut bad = idx.clone();
    let p = bad.windows(5).position(|w| w == b"1.0.0").unwrap();
    bad[p + 4] = b'1';
    std::fs::write(cat.join("index.json"), &bad).unwrap();
    let (rc, io) = run(&ctx, &list);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("bad-signature"), "{:?}", io.err);
    assert!(
        !t.root.join("data/launcher/catalogue-state.json").exists(),
        "a refused index is not pinned"
    );
    std::fs::write(cat.join("index.json"), &idx).unwrap();

    // Signed by another key.
    let other = t.ctx_with_key([9u8; 32]);
    let (rc, io) = run(&other, &list);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("unknown-key"), "{:?}", io.err);

    // Wrong zip bytes (same size): refused, nothing installed.
    let zip = cat.join("demo-1.0.0.zip");
    let good = std::fs::read(&zip).unwrap();
    let mut evil = good.clone();
    let n = evil.len() / 2;
    evil[n] ^= 0xff;
    std::fs::write(&zip, &evil).unwrap();
    let before = snapshot(&t.root);
    let (rc, io) = run(&ctx, &install);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("zip-digest"), "{:?}", io.err);
    assert_only_under(
        &before,
        &snapshot(&t.root),
        &[
            "data/launcher/catalogue-state.json",
            "data/launcher/staging",
        ],
    );
    assert!(!t.root.join("data/plugins/demo").exists());
    std::fs::write(&zip, &good).unwrap();

    // Expired index.
    let mut late = t.ctx();
    late.now += 365 * 86_400;
    let (rc, io) = run(&late, &list);
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("expired"), "{:?}", io.err);

    // Unreviewed entry is not offered and cannot be installed (the index parser refuses it).
    let t2 = Scratch::new("unreviewed");
    let cat2 = t2.catalogue_dir();
    t2.publish(&cat2, "1.0.0", 1, Review::Unreviewed);
    let src2 = ["--source", cat2.to_str().unwrap()];
    let (rc, _) = run(
        &t2.ctx(),
        &[&["catalogue", "install", "demo"][..], &src2].concat(),
    );
    assert_eq!(rc, 1);
    assert!(!t2.root.join("data/plugins/demo").exists());

    // Unknown id.
    let (rc, io) = run(
        &ctx,
        &[&["catalogue", "install", "nope"][..], &src].concat(),
    );
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("not-in-index"));

    // Compatibility: an SDK the plugin does not match.
    let mut old_sdk = t.ctx();
    old_sdk.host.sdk = scol_catalogue::semver::Version::parse("2.3.0").unwrap();
    let (rc, io) = run(&old_sdk, &install);
    assert_eq!(rc, 1);
    assert!(
        io.err.join("\n").contains("incompatible-sdk"),
        "{:?}",
        io.err
    );
}

#[test]
fn update_never_touches_local_plugins_and_clash_needs_replace() {
    let t = Scratch::new("clash");
    let ctx = t.ctx();
    let cat = t.catalogue_dir();
    t.publish(&cat, "1.1.0", 3, Review::FirstParty);
    let src = ["--source", cat.to_str().unwrap()];
    // A local demo 1.0.0 is installed.
    let folder = t.make_plugin_folder("1.0.0");
    assert_eq!(run(&ctx, &["install", folder.to_str().unwrap()]).0, 0);
    let before = snapshot(&t.root);
    let (rc, io) = run(&ctx, &[&["update"][..], &src].concat());
    assert_eq!(rc, 0, "{:?}", io.err);
    assert_eq!(installed_json(&t.root)["plugins"][0]["version"], "1.0.0");
    let (rc, _) = run(&ctx, &[&["update", "demo"][..], &src].concat());
    assert_eq!(rc, 1, "named plugin that is not a catalogue install");
    // Catalogue install over a local folder is refused without --replace.
    let (rc, io) = run(
        &ctx,
        &[&["catalogue", "install", "demo"][..], &src].concat(),
    );
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("local-clash"), "{:?}", io.err);
    assert!(!snapshot(&t.root)
        .iter()
        .any(|(k, _)| k.contains("staging/demo")));
    let _ = before;
    let (rc, io) = run(
        &ctx,
        &[&["catalogue", "install", "demo", "--replace"][..], &src].concat(),
    );
    assert_eq!(rc, 0, "{:?}", io.err);
    assert_eq!(installed_json(&t.root)["plugins"][0]["source"], "catalogue");
    // A downgrade over a catalogue install needs the flag.
    t.publish(&cat, "1.0.0", 4, Review::FirstParty);
    let (rc, io) = run(
        &ctx,
        &[&["catalogue", "install", "demo"][..], &src].concat(),
    );
    assert_eq!(rc, 1);
    assert!(io.err.join("\n").contains("downgrade"), "{:?}", io.err);
    let (rc, _) = run(
        &ctx,
        &[
            &["catalogue", "install", "demo", "--allow-downgrade"][..],
            &src,
        ]
        .concat(),
    );
    assert_eq!(rc, 0);
}
