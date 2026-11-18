// SPDX-License-Identifier: GPL-3.0-only
//! `collect_status` (the window's Status panel checks) on a synthetic folder. The ini names the game,
//! so discovery does not run. The values are those `CollectStatus` of the C++ launcher gives for the
//! same files; nothing was run in game.
#![cfg(windows)]

use scol_install::collect_status;

#[test]
fn panel_fields_for_a_synthetic_install() {
    let root = std::env::temp_dir().join(format!("scol-install-collect-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let here = root.join("here");
    let live = root.join("lib").join("StarCitizen").join("LIVE");
    let bin = live.join("Bin64");
    std::fs::create_dir_all(&bin).unwrap();
    std::fs::write(bin.join("StarCitizen.exe"), b"x").unwrap();
    std::fs::write(
        live.join("build_manifest.id"),
        br#"{"Version":"4.1","RequestedP4ChangeNum":"7"}"#,
    )
    .unwrap();
    std::fs::write(
        bin.join("sc-offline.installed"),
        b"time=2026-11-17 09:05:03\n",
    )
    .unwrap();
    for (name, disabled) in [("one", false), ("two", true), ("three", false)] {
        let f = here.join("data").join("plugins").join(name);
        std::fs::create_dir_all(&f).unwrap();
        std::fs::write(f.join("plugin.ini"), b"").unwrap();
        if disabled {
            std::fs::write(f.join("disabled"), b"").unwrap();
        }
    }
    std::fs::create_dir_all(here.join("data").join("plugins").join("no-ini")).unwrap();
    let ini = format!(
        "game = {}\nblock_network = off\neac_rename = off\nplugins = off\n",
        root.join("lib").join("StarCitizen").to_string_lossy()
    );
    std::fs::write(here.join("sc-offline.ini"), ini).unwrap();

    let r = collect_status(&here.to_string_lossy());
    assert!(r.checked && r.folder_known);
    assert_eq!(r.bin, bin.to_string_lossy());
    assert_eq!(r.build, "4.1 (CL 7)");
    assert!(r.mod_installed, "the marker counts as installed");
    assert!(!r.network_blocked && !r.eac_rename && !r.plugins_on);
    assert_eq!((r.plugins_enabled, r.plugins_total), (2, 3));

    // The game moved: the ini now names nothing that resolves, the panel says the folder is unknown.
    std::fs::write(
        here.join("sc-offline.ini"),
        "game = D:\\no\\such\\scol\\folder\n",
    )
    .unwrap();
    let r = collect_status(&here.to_string_lossy());
    assert!(
        r.checked && !r.folder_known && r.bin.is_empty() && r.build.is_empty() && !r.mod_installed
    );
    let _ = std::fs::remove_dir_all(&root);
}
