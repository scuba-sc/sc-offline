// SPDX-License-Identifier: GPL-3.0-only
//! The generated list of game builds and the comparison the launcher makes with it. Build numbers
//! are public facts from Core's register. Nothing here was run in game.

use scol_contract::game_builds::{describe, fit, listed, parse, verified, Fit, JSON};

/// The hand reader of the generated file gives what a JSON parser gives.
#[test]
fn the_generated_list_reads_the_same_with_a_json_parser() {
    let doc: serde_json::Value = serde_json::from_str(JSON).expect("game_builds.json is JSON");
    assert_eq!(doc["verified_build"].as_str(), Some(verified().as_str()));
    let builds = doc["builds"].as_array().expect("builds is a list");
    let ours = listed();
    assert_eq!(builds.len(), ours.len());
    assert!(!ours.is_empty());
    for (theirs, ours) in builds.iter().zip(&ours) {
        assert_eq!(theirs["build"].as_str(), Some(ours.build.as_str()));
        assert_eq!(theirs["patch"].as_str(), Some(ours.patch.as_str()));
    }
    // The tool's own rules: four-part numbers, oldest first, the verified build is the newest.
    let numbers: Vec<[u32; 4]> = ours
        .iter()
        .map(|l| parse(&l.build).unwrap_or_else(|| panic!("{}", l.build)))
        .collect();
    assert!(numbers.windows(2).all(|w| w[0] < w[1]), "{numbers:?}");
    assert_eq!(numbers.last().copied(), parse(&verified()));
    assert!(
        !JSON.contains("\\\""),
        "the hand reader assumes no escaped quotes"
    );
}

#[test]
fn build_texts_are_read_as_the_launcher_shows_them() {
    assert_eq!(
        parse("4.10.200.58746 (CL 13165946)"),
        Some([4, 10, 200, 58746])
    );
    assert_eq!(parse("4.10.201.48085"), Some([4, 10, 201, 48085]));
    for not_a_build in [
        "",
        "(CL 13165946)",
        "4.10.200",
        "4.10.200.58746.1",
        "4.10.x.1",
        "4..200.1",
        "StarCitizen.exe 1 bytes, 2026-11-22 21:42",
    ] {
        assert_eq!(parse(not_a_build), None, "{not_a_build:?}");
    }
}

#[test]
fn an_installed_build_is_placed_against_the_verified_one() {
    let pin = verified();
    let p = parse(&pin).unwrap();
    assert_eq!(fit(&format!("{pin} (CL 1)")), Fit::Verified);
    // one changelist up: a hotfix or a newer patch
    let newer = format!("{}.{}.{}.{}", p[0], p[1], p[2] + 1, 1);
    assert_eq!(fit(&newer), Fit::Newer);
    assert_eq!(fit(&format!("{}.{}.0.1", p[0], p[1] + 1)), Fit::Newer);
    // every earlier build of the register is named by its patch
    let all = listed();
    for l in &all[..all.len() - 1] {
        assert_eq!(fit(&l.build), Fit::Earlier(l.patch.clone()), "{}", l.build);
    }
    // lower than the verified build and not in the register
    assert_eq!(fit("0.0.0.1"), Fit::Unlisted);
    assert_eq!(
        fit("StarCitizen.exe 1 bytes, 2026-11-22 21:42"),
        Fit::Unknown
    );
    assert_eq!(fit(""), Fit::Unknown);

    // The sentences name the verified build and never say "tested" or "supported".
    assert_eq!(describe(&Fit::Unknown), "");
    for f in [
        Fit::Newer,
        Fit::Unlisted,
        Fit::Earlier("Alpha 4.10.2".into()),
    ] {
        let text = describe(&f);
        assert!(text.contains(&pin), "{text}");
        assert!(
            !text.contains("tested") && !text.contains("supported"),
            "{text}"
        );
    }
    assert!(describe(&Fit::Earlier("Alpha 4.10.2".into())).contains("Alpha 4.10.2"));
    assert_eq!(
        describe(&Fit::Verified),
        "the build this release was checked on"
    );
}
