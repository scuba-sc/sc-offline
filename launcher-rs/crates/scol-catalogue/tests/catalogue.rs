// SPDX-License-Identifier: GPL-3.0-only
//! Tests of the catalogue client. Synthetic only: indexes are made in memory and signed with the
//! TEST key from `launcher-rs/tests/catalogue/golden/`, zips come from the shared corpus in
//! `launcher-rs/tests/zip-corpus/cases/`, and fetching is a local fake. Not run in game.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use ed25519_compact::{KeyPair, Seed};
use scol_catalogue::cli::{run_with, Io};
use scol_catalogue::fetch::{
    check_redirect_url, check_url, download_zip, fetch_index, Fetcher, Step, MAX_REDIRECTS,
};
use scol_catalogue::index::{open_index, Entry, LastAccepted, Review, MAX_INDEX_BYTES};
use scol_catalogue::install::{
    plan_install, verify_download, InstalledFile, InstalledRecord, LocalState, PlanOptions,
    Replaces, DISABLED_MARKER, NATIVE_WARNING,
};
use scol_catalogue::policy::{check_entry, offered, Host};
use scol_catalogue::semver::Version;
use scol_catalogue::time::parse_utc;
use scol_catalogue::trust::{from_hex, key_id, TrustRoot, SIGNED_PREFIX, SIG_MAGIC};
use scol_plugins::sha256;
use scol_plugins::verify::verify_archive;
use serde_json::{json, Value};

const ISSUED: &str = "2026-11-17T00:00:00Z";
const EXPIRES: &str = "2026-12-17T00:00:00Z";

fn now() -> i64 {
    parse_utc("2026-11-20T00:00:00Z").unwrap()
}

fn launcher_rs() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn golden(name: &str) -> PathBuf {
    launcher_rs().join("tests/catalogue/golden").join(name)
}

fn corpus_zip(name: &str) -> PathBuf {
    launcher_rs()
        .join("tests/zip-corpus/cases")
        .join(format!("{name}.zip"))
}

fn text(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn test_seed() -> Seed {
    Seed::from_slice(&from_hex(text(&golden("TEST-ONLY-private-seed.hex")).trim(), 32).unwrap())
        .unwrap()
}

fn test_public_hex() -> String {
    text(&golden("TEST-ONLY-public.hex")).trim().to_string()
}

fn test_root() -> TrustRoot {
    TrustRoot::from_hex_keys(&[&test_public_hex()]).unwrap()
}

/// Sign with the TEST key (or another seed) in the documented format.
fn sign_with(seed: Seed, index: &[u8]) -> String {
    let kp = KeyPair::from_seed(seed);
    let mut msg = SIGNED_PREFIX.to_vec();
    msg.extend_from_slice(index);
    let sig = kp.sk.sign(&msg, None);
    format!(
        "{SIG_MAGIC} {} {}\n",
        key_id(kp.pk.as_ref()),
        hex(sig.as_ref())
    )
}

fn sign(index: &[u8]) -> String {
    sign_with(test_seed(), index)
}

fn host() -> Host {
    Host {
        api_major: 1,
        api_minor: 1,
        sdk: Version::parse("2.4.1").unwrap(),
    }
}

/// serde_json keeps keys sorted (no `preserve_order`), so this is the canonical shape.
fn to_bytes(v: &Value) -> Vec<u8> {
    let mut s = serde_json::to_string(v).unwrap();
    s.push('\n');
    s.into_bytes()
}

/// An index entry for a corpus zip, with the facts taken from the verifier.
fn entry_for(zip: &str, review: &str) -> Value {
    let path = corpus_zip(zip);
    let report = verify_archive(&path).unwrap_or_else(|e| panic!("{zip}: {e}"));
    json!({
        "id": report.manifest.id,
        "name": report.manifest.name,
        "version": report.manifest.version,
        "kind": report.manifest.kind.as_str(),
        "api": report.manifest.api,
        "sdk": ">=2.4.1, <2.5",
        "license": "GPL-3.0-only",
        "maintainers": ["example-org"],
        "review": review,
        "evidence": "Synthetic fixture. Not run in game.",
        "source": {
            "repository": "https://github.com/example-org/sc-fixture",
            "commit": "0123456789abcdef0123456789abcdef01234567",
            "path": "plugins/fixture"
        },
        "zip": {
            "url": format!("https://github.com/example-org/sc-plugins-fixture/releases/download/v1/{zip}.zip"),
            "sha256": report.archive.sha256,
            "size": report.archive.size_bytes
        }
    })
}

fn doc(sequence: u64, plugins: Vec<Value>) -> Value {
    json!({"schema": 1, "sequence": sequence, "issued": ISSUED, "expires": EXPIRES, "plugins": plugins})
}

fn open(v: &Value) -> Result<scol_catalogue::index::AcceptedIndex, scol_catalogue::Error> {
    let bytes = to_bytes(v);
    open_index(&bytes, &sign(&bytes), &test_root(), now(), None)
}

fn code<T: std::fmt::Debug>(r: Result<T, scol_catalogue::Error>) -> &'static str {
    r.unwrap_err().code()
}

struct Scratch(PathBuf);

impl Scratch {
    fn new(tag: &str) -> Scratch {
        let dir = std::env::temp_dir().join(format!("scol-catalogue-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }
    fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn pilot_entry() -> Entry {
    let acc = open(&doc(1, vec![entry_for("good-minimal", "first-party")])).unwrap();
    acc.index.plugins[0].clone()
}

// ---- cross-language golden ------------------------------------------------------------------

#[test]
fn index_written_and_signed_by_the_python_tools_verifies() {
    let bytes = std::fs::read(golden("index.json")).unwrap();
    let sig = text(&golden("index.json.sig"));
    let acc = open_index(&bytes, &sig, &test_root(), now(), None).expect("golden index");
    assert_eq!(acc.index.sequence, 7);
    assert_eq!(acc.digest, sha256::hex_digest(&bytes));
    let ids: Vec<&str> = acc.index.plugins.iter().map(|e| e.id.as_str()).collect();
    assert_eq!(ids, ["cratepack", "lanternlua", "pilot"]);
    let pilot = acc.index.find("pilot").unwrap();
    assert_eq!(
        (pilot.version.as_str(), pilot.api.as_str(), pilot.review),
        ("1.0.0", "1.1", Review::FirstParty)
    );
    assert_eq!(pilot.sdk, ">=2.4.1, <2.5");
    assert_eq!(pilot.commit.len(), 40);
    assert!(pilot.zip_url.starts_with("https://github.com/"));
    // The Rust signer produces the same signature bytes as the Python one (Ed25519 is deterministic).
    assert_eq!(sign(&bytes), sig);
}

#[test]
fn golden_index_is_refused_by_the_production_root_and_by_a_stranger() {
    let bytes = std::fs::read(golden("index.json")).unwrap();
    let sig = text(&golden("index.json.sig"));
    // No production key is compiled in yet, so nothing verifies (fail closed).
    assert_eq!(
        code(open_index(
            &bytes,
            &sig,
            &TrustRoot::embedded(),
            now(),
            None
        )),
        "no-trust-keys"
    );
    let stranger = KeyPair::from_seed(Seed::from_slice(&[7u8; 32]).unwrap());
    let other = TrustRoot::from_hex_keys(&[&hex(stranger.pk.as_ref())]).unwrap();
    assert_eq!(
        code(open_index(&bytes, &sig, &other, now(), None)),
        "unknown-key"
    );
}

// ---- signature ------------------------------------------------------------------------------

#[test]
fn tampered_index_is_refused() {
    let bytes = to_bytes(&doc(3, vec![entry_for("good-minimal", "first-party")]));
    let sig = sign(&bytes);
    assert!(open_index(&bytes, &sig, &test_root(), now(), None).is_ok());
    let mut changed = bytes.clone();
    let at = changed.iter().position(|&b| b == b'3').unwrap();
    changed[at] = b'4'; // sequence 3 -> 4
    assert_eq!(
        code(open_index(&changed, &sig, &test_root(), now(), None)),
        "bad-signature"
    );
    let mut truncated = bytes.clone();
    truncated.pop();
    assert_eq!(
        code(open_index(&truncated, &sig, &test_root(), now(), None)),
        "bad-signature"
    );
    let mut longer = bytes.clone();
    longer.push(b' ');
    assert_eq!(
        code(open_index(&longer, &sig, &test_root(), now(), None)),
        "bad-signature"
    );
}

#[test]
fn tampered_or_foreign_signature_is_refused() {
    let bytes = to_bytes(&doc(3, vec![entry_for("good-minimal", "first-party")]));
    let sig = sign(&bytes);
    let (head, tail) = sig.trim_end().rsplit_once(' ').unwrap();
    let flipped = format!(
        "{head} {}{}\n",
        if tail.starts_with('0') { '1' } else { '0' },
        &tail[1..]
    );
    assert_eq!(
        code(open_index(&bytes, &flipped, &test_root(), now(), None)),
        "bad-signature"
    );
    // Signed by another key with the right key id written in: still refused.
    let other = sign_with(Seed::from_slice(&[9u8; 32]).unwrap(), &bytes);
    let forged = format!("{head} {}\n", other.trim_end().rsplit_once(' ').unwrap().1);
    assert_eq!(
        code(open_index(&bytes, &forged, &test_root(), now(), None)),
        "bad-signature"
    );
    // Signed by another key under its own id: not in the trust root.
    assert_eq!(
        code(open_index(&bytes, &other, &test_root(), now(), None)),
        "unknown-key"
    );
    // A signature without the domain prefix is not valid.
    let kp = KeyPair::from_seed(test_seed());
    let bare = kp.sk.sign(&bytes, None);
    let line = format!(
        "{SIG_MAGIC} {} {}\n",
        key_id(kp.pk.as_ref()),
        hex(bare.as_ref())
    );
    assert_eq!(
        code(open_index(&bytes, &line, &test_root(), now(), None)),
        "bad-signature"
    );
    assert_eq!(
        code(open_index(&bytes, "garbage\n", &test_root(), now(), None)),
        "sig-format"
    );
}

#[test]
fn nothing_is_parsed_before_the_signature_is_checked() {
    // Not JSON at all, and unsigned: the error is about the signature, not the JSON.
    let junk = b"{{{{ not json";
    assert_eq!(
        code(open_index(junk, &sign(b"other"), &test_root(), now(), None)),
        "bad-signature"
    );
}

#[test]
fn oversize_index_is_refused() {
    let big = vec![b' '; MAX_INDEX_BYTES + 1];
    assert_eq!(
        code(open_index(&big, &sign(&big), &test_root(), now(), None)),
        "too-large"
    );
}

// ---- expiry and sequence --------------------------------------------------------------------

#[test]
fn expired_index_is_refused() {
    let v = doc(1, vec![]);
    let bytes = to_bytes(&v);
    let sig = sign(&bytes);
    let expires = parse_utc(EXPIRES).unwrap();
    assert!(open_index(&bytes, &sig, &test_root(), expires - 1, None).is_ok());
    assert_eq!(
        code(open_index(&bytes, &sig, &test_root(), expires, None)),
        "expired"
    );
    assert_eq!(
        code(open_index(
            &bytes,
            &sig,
            &test_root(),
            expires + 86_400,
            None
        )),
        "expired"
    );
}

fn last_state(sequence: u64, digest: &str) -> LastAccepted {
    LastAccepted {
        sequence,
        digest: digest.to_string(),
    }
}

#[test]
fn sequence_may_not_go_back() {
    let bytes = to_bytes(&doc(5, vec![]));
    let sig = sign(&bytes);
    let digest = scol_plugins::sha256::hex_digest(&bytes);
    let other = "0".repeat(64);
    assert_eq!(
        code(open_index(
            &bytes,
            &sig,
            &test_root(),
            now(),
            Some(&last_state(6, &digest))
        )),
        "rollback"
    );
    assert!(
        open_index(
            &bytes,
            &sig,
            &test_root(),
            now(),
            Some(&last_state(5, &digest))
        )
        .is_ok(),
        "same sequence and same bytes is a re-fetch"
    );
    assert!(open_index(
        &bytes,
        &sig,
        &test_root(),
        now(),
        Some(&last_state(4, &other))
    )
    .is_ok());
    assert!(open_index(&bytes, &sig, &test_root(), now(), None).is_ok());
}

#[test]
fn same_sequence_with_other_bytes_is_a_replay() {
    // Two different, validly signed indexes with sequence 5.
    let first = to_bytes(&doc(5, vec![]));
    let mut v = doc(5, vec![]);
    v["issued"] = serde_json::json!("2026-11-02T00:00:00Z");
    let second = to_bytes(&v);
    assert_ne!(first, second);
    let accepted = open_index(&first, &sign(&first), &test_root(), now(), None).unwrap();
    let state = last_state(5, &accepted.digest);
    assert_eq!(
        code(open_index(
            &second,
            &sign(&second),
            &test_root(),
            now(),
            Some(&state)
        )),
        "sequence-replay"
    );
    // The same bytes again are fine; a higher sequence with other bytes is fine.
    assert!(open_index(&first, &sign(&first), &test_root(), now(), Some(&state)).is_ok());
    let third = to_bytes(&doc(6, vec![]));
    assert!(open_index(&third, &sign(&third), &test_root(), now(), Some(&state)).is_ok());
}

#[test]
fn expiry_is_checked_before_sequence_and_both_after_signature() {
    let bytes = to_bytes(&doc(1, vec![]));
    let expires = parse_utc(EXPIRES).unwrap();
    assert_eq!(
        code(open_index(
            &bytes,
            &sign(&bytes),
            &test_root(),
            expires,
            Some(&last_state(9, "x"))
        )),
        "expired"
    );
}

// ---- strict parse ---------------------------------------------------------------------------

fn signed_text(s: &str) -> Result<scol_catalogue::index::AcceptedIndex, scol_catalogue::Error> {
    open_index(s.as_bytes(), &sign(s.as_bytes()), &test_root(), now(), None)
}

#[test]
fn unknown_fields_and_duplicate_keys_are_refused_even_when_signed() {
    let base = to_bytes(&doc(1, vec![entry_for("good-minimal", "first-party")]));
    let base = String::from_utf8(base).unwrap();
    assert!(signed_text(&base).is_ok());
    // Unknown field at the top, in an entry, in source and in zip.
    assert_eq!(
        code(signed_text(&base.replacen(
            "{\"expires\"",
            "{\"extra\":1,\"expires\"",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"api\"",
            "\"extra\":1,\"api\"",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"commit\"",
            "\"extra\":1,\"commit\"",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"sha256\"",
            "\"extra\":1,\"sha256\"",
            1
        ))),
        "index-format"
    );
    // Duplicate keys, top level and nested.
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"sequence\":1",
            "\"sequence\":1,\"sequence\":2",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"api\":",
            "\"api\":\"1.0\",\"api\":",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"size\":",
            "\"size\":1,\"size\":",
            1
        ))),
        "index-format"
    );
    // Missing field, wrong type, trailing data, not UTF-8.
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"issued\":\"2026-11-17T00:00:00Z\",",
            "",
            1
        ))),
        "index-format"
    );
    assert_eq!(
        code(signed_text(&base.replacen(
            "\"sequence\":1",
            "\"sequence\":\"1\"",
            1
        ))),
        "index-format"
    );
    assert_eq!(code(signed_text(&format!("{base}{{}}"))), "index-format");
    let bad_utf8 = vec![0xff, 0xfe];
    assert_eq!(
        code(open_index(
            &bad_utf8,
            &sign(&bad_utf8),
            &test_root(),
            now(),
            None
        )),
        "index-format"
    );
}

#[test]
fn bad_values_are_refused() {
    let e = entry_for("good-minimal", "first-party");
    let mutate = |f: &dyn Fn(&mut Value)| -> &'static str {
        let mut v = doc(1, vec![e.clone()]);
        f(&mut v);
        code(open(&v))
    };
    assert_eq!(mutate(&|v| v["schema"] = json!(2)), "index-format");
    assert_eq!(
        mutate(&|v| v["issued"] = json!("2026-11-17")),
        "index-format"
    );
    assert_eq!(mutate(&|v| v["expires"] = json!(ISSUED)), "index-format");
    assert_eq!(
        mutate(&|v| v["plugins"][0]["id"] = json!("Bad-Id")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["kind"] = json!("dll")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["api"] = json!("1")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["sdk"] = json!("2.4.1")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["review"] = json!("trusted")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["maintainers"] = json!([])),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["evidence"] = json!("")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["evidence"] = json!("a\nb")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["source"]["commit"] = json!("main")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["source"]["commit"] = json!("A".repeat(40))),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["source"]["path"] = json!("../x")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["source"]["repository"] = json!("https://example.com/a/b")),
        "index-format"
    );
    for repo in [
        "https://github.com/../b",
        "https://github.com/a/..",
        "https://github.com/./b",
    ] {
        assert_eq!(
            mutate(&|v| v["plugins"][0]["source"]["repository"] = json!(repo)),
            "index-format",
            "{repo}"
        );
    }
    // Text the player sees on the consent screen: no bidi override, zero width or control marks.
    for text in [
        "Pilot\u{202e}Tools",
        "Pi\u{200b}lot",
        "Pilot\u{feff}",
        "Pilot\u{0085}",
    ] {
        assert_eq!(
            mutate(&|v| v["plugins"][0]["name"] = json!(text)),
            "index-format",
            "name {text:?}"
        );
        assert_eq!(
            mutate(&|v| v["plugins"][0]["evidence"] = json!(text)),
            "index-format",
            "evidence {text:?}"
        );
        assert_eq!(
            mutate(&|v| v["plugins"][0]["maintainers"] = json!([text])),
            "index-format",
            "maintainers {text:?}"
        );
    }
    // The version must parse (same grammar as the Python tool).
    for version in ["", "latest", "1", "1.0.0.0", "01.0", "1.0 beta", "v1.0"] {
        assert_eq!(
            mutate(&|v| v["plugins"][0]["version"] = json!(version)),
            "index-format",
            "version {version:?}"
        );
    }
    assert_eq!(
        mutate(&|v| v["plugins"][0]["zip"]["url"] = json!("http://github.com/a/b.zip")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["zip"]["sha256"] = json!("abc")),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["zip"]["size"] = json!(0)),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["zip"]["size"] = json!(-5)),
        "index-format"
    );
    assert_eq!(
        mutate(&|v| v["plugins"][0]["zip"]["size"] = json!(1u64 << 40)),
        "index-format"
    );
}

#[test]
fn entries_must_be_sorted_and_unique() {
    let a = entry_for("good-minimal", "first-party"); // pilot
    let mut b = entry_for("good-lua-plugin", "reviewed");
    b["id"] = json!("zeta"); // the corpus zips all declare the id pilot; only the index is tested here
    let (ida, idb) = (
        a["id"].as_str().unwrap().to_string(),
        b["id"].as_str().unwrap().to_string(),
    );
    assert_ne!(ida, idb);
    let (first, second) = if ida < idb {
        (a.clone(), b.clone())
    } else {
        (b.clone(), a.clone())
    };
    assert!(open(&doc(1, vec![first.clone(), second.clone()])).is_ok());
    assert_eq!(
        code(open(&doc(1, vec![second, first.clone()]))),
        "index-format"
    );
    assert_eq!(
        code(open(&doc(1, vec![first.clone(), first]))),
        "index-format"
    );
}

// ---- review, reserved ids, compatibility ----------------------------------------------------

#[test]
fn unreviewed_entry_is_parsed_but_never_offered_or_installable() {
    let acc = open(&doc(
        1,
        vec![entry_for("good-minimal", "community-unreviewed")],
    ))
    .unwrap();
    let e = &acc.index.plugins[0];
    assert_eq!(e.review, Review::CommunityUnreviewed);
    assert_eq!(code(check_entry(e, &host())), "not-listed");
    assert!(offered(&acc.index, &host()).is_empty());
}

#[test]
fn reviewed_and_first_party_are_offered() {
    let mut second = entry_for("good-lua-plugin", "first-party");
    second["id"] = json!("zeta");
    let acc = open(&doc(
        1,
        vec![entry_for("good-data-pack", "reviewed"), second],
    ))
    .unwrap();
    let o = offered(&acc.index, &host());
    assert_eq!(o.len(), 2);
    assert!(o.iter().all(|x| x.compatible.is_ok()));
}

#[test]
fn reserved_ids_are_refused() {
    for id in ["sco", "host", "menu", "game"] {
        let mut e = entry_for("good-minimal", "first-party");
        e["id"] = json!(id);
        let acc = open(&doc(1, vec![e])).unwrap();
        assert_eq!(
            code(check_entry(&acc.index.plugins[0], &host())),
            "reserved-id",
            "{id}"
        );
        assert!(offered(&acc.index, &host()).is_empty());
    }
}

#[test]
fn api_major_must_match_and_minor_must_not_be_newer() {
    let with_api = |api: &str| {
        let mut e = entry_for("good-minimal", "first-party");
        e["api"] = json!(api);
        open(&doc(1, vec![e])).unwrap().index.plugins[0].clone()
    };
    assert!(check_entry(&with_api("1.0"), &host()).is_ok());
    assert!(check_entry(&with_api("1.1"), &host()).is_ok());
    assert_eq!(
        code(check_entry(&with_api("1.2"), &host())),
        "incompatible-api"
    );
    assert_eq!(
        code(check_entry(&with_api("2.0"), &host())),
        "incompatible-api"
    );
    assert_eq!(
        code(check_entry(&with_api("0.9"), &host())),
        "incompatible-api"
    );
}

#[test]
fn sdk_range_must_admit_the_host_sdk() {
    let with_sdk = |sdk: &str| {
        let mut e = entry_for("good-minimal", "first-party");
        e["sdk"] = json!(sdk);
        open(&doc(1, vec![e])).unwrap().index.plugins[0].clone()
    };
    let at = |v: &str| Host {
        sdk: Version::parse(v).unwrap(),
        ..host()
    };
    let e = with_sdk(">=2.4.1, <2.5");
    assert!(check_entry(&e, &at("2.4.1")).is_ok());
    assert!(check_entry(&e, &at("2.4.7")).is_ok());
    assert_eq!(code(check_entry(&e, &at("2.4.0"))), "incompatible-sdk");
    assert_eq!(code(check_entry(&e, &at("2.5.0"))), "incompatible-sdk");
    assert_eq!(code(check_entry(&e, &at("3.0.0"))), "incompatible-sdk");
    // A development build of 2.4.1 is older than 2.4.1.
    assert_eq!(
        code(check_entry(&e, &at("2.4.1-dev.6"))),
        "incompatible-sdk"
    );
    assert!(check_entry(&with_sdk(">=2.4.1-dev.1, <2.5"), &at("2.4.1-dev.6")).is_ok());
}

// ---- the zip --------------------------------------------------------------------------------

#[test]
fn good_zip_passes_and_reports_the_plugin() {
    let e = pilot_entry();
    let report = verify_download(&corpus_zip("good-minimal"), &e).expect("good zip");
    assert_eq!(report.manifest.id, e.id);
    assert_eq!(report.archive.sha256, e.zip_sha256);
}

#[test]
fn wrong_zip_hash_is_refused_before_the_verifier_runs() {
    let mut e = pilot_entry();
    e.zip_sha256 = "0".repeat(64);
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-digest"
    );
    // A hostile zip with a wrong hash is a hash error, not a parse of hostile bytes.
    let hostile = corpus_zip("bad-collision-case");
    let mut h = pilot_entry();
    h.zip_size = std::fs::metadata(&hostile).unwrap().len();
    assert_eq!(code(verify_download(&hostile, &h)), "zip-digest");
}

#[test]
fn wrong_zip_size_is_refused() {
    let mut e = pilot_entry();
    e.zip_size += 1;
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-digest"
    );
}

#[test]
fn zip_with_the_right_hash_still_goes_through_the_verifier() {
    // The index vouches for the bytes (hash matches) but the zip breaks a verifier rule.
    let path = corpus_zip("bad-collision-case");
    let bytes = std::fs::read(&path).unwrap();
    let mut e = pilot_entry();
    e.zip_sha256 = sha256::hex_digest(&bytes);
    e.zip_size = bytes.len() as u64;
    let err = verify_download(&path, &e).unwrap_err();
    assert_eq!(err.code(), "zip-refused", "{err}");
}

#[test]
fn zip_must_be_the_plugin_the_index_names() {
    let base = pilot_entry();
    // Right bytes, entry claims another id, version, kind or api.
    let mut e = base.clone();
    e.id = "other".into();
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-mismatch"
    );
    let mut e = base.clone();
    e.version = "9.9".into();
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-mismatch"
    );
    let mut e = base.clone();
    e.kind = scol_plugins::manifest::Kind::Lua;
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-mismatch"
    );
    let mut e = base;
    e.api_minor += 1;
    e.api = format!("{}.{}", e.api_major, e.api_minor);
    assert_eq!(
        code(verify_download(&corpus_zip("good-minimal"), &e)),
        "zip-mismatch"
    );
}

#[test]
fn missing_zip_is_an_io_error() {
    assert_eq!(
        code(verify_download(
            Path::new("no-such-file.zip"),
            &pilot_entry()
        )),
        "io"
    );
}

// ---- the plan -------------------------------------------------------------------------------

fn plan_for(
    zip: &str,
    review: &str,
    state: &LocalState,
    opts: PlanOptions,
) -> Result<scol_catalogue::install::InstallPlan, scol_catalogue::Error> {
    let acc = open(&doc(4, vec![entry_for(zip, review)])).unwrap();
    let e = &acc.index.plugins[0];
    let report = verify_download(&corpus_zip(zip), e)?;
    plan_install(
        e,
        &report,
        &acc.digest,
        acc.index.sequence,
        &host(),
        state,
        opts,
        Path::new("C:/sc-offline"),
        "2026-11-20T10:00:00Z",
    )
}

#[test]
fn a_new_install_lands_disabled_under_data_plugins() {
    let plan = plan_for(
        "good-minimal",
        "first-party",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap();
    assert_eq!(plan.id, "pilot");
    assert_eq!(
        plan.target_dir,
        Path::new("C:/sc-offline")
            .join("data")
            .join("plugins")
            .join("pilot")
    );
    assert_eq!(
        plan.staging_dir,
        Path::new("C:/sc-offline")
            .join("data")
            .join("launcher")
            .join("staging")
            .join("pilot")
    );
    assert_eq!(
        plan.installed_json,
        Path::new("C:/sc-offline")
            .join("data")
            .join("launcher")
            .join("installed.json")
    );
    assert_eq!(plan.disabled_marker, plan.target_dir.join(DISABLED_MARKER));
    assert_eq!(plan.replaces, Replaces::Nothing);
    assert_eq!(plan.record.state, "disabled");
    assert_eq!(plan.record.source, "catalogue");
    assert_eq!(plan.record.index_sequence, Some(4));
    assert_eq!(plan.record.installed, "2026-11-20T10:00:00Z");
    assert!(plan
        .record
        .zip_sha256
        .as_deref()
        .is_some_and(|s| s.len() == 64));
    assert!(plan
        .record
        .index_digest
        .as_deref()
        .is_some_and(|s| s.len() == 64));
    // Native plugin: the native-code warning is attached.
    assert_eq!(plan.warnings, vec![NATIVE_WARNING.to_string()]);
    // Files are relative to the plugin folder, with the plugin.ini among them, none under data/storage.
    assert!(
        plan.files.iter().any(|f| f.path == "plugin.ini"),
        "{:?}",
        plan.files
    );
    assert!(plan
        .files
        .iter()
        .all(|f| !f.path.starts_with("pilot/") && !f.path.contains("storage")));
    let mut sorted = plan.files.clone();
    sorted.sort_by(|a, b| a.path.cmp(&b.path));
    assert_eq!(plan.files, sorted);
    // installed.json after is valid and holds the record, disabled.
    let after = InstalledFile::parse(plan.installed_json_after.as_bytes()).unwrap();
    assert_eq!(after.find("pilot"), Some(&plan.record));
}

#[test]
fn data_packs_get_no_native_warning_and_lua_gets_the_script_note() {
    let data = plan_for(
        "good-data-pack",
        "reviewed",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap();
    assert!(data.warnings.is_empty());
    assert_eq!(data.record.state, "disabled");
    let lua = plan_for(
        "good-lua-plugin",
        "reviewed",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap();
    assert_eq!(lua.warnings.len(), 1);
    assert_ne!(lua.warnings[0], NATIVE_WARNING);
}

#[test]
fn the_plan_is_deterministic() {
    let a = plan_for(
        "good-minimal",
        "first-party",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap();
    let b = plan_for(
        "good-minimal",
        "first-party",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap();
    assert_eq!(a, b);
    assert_eq!(a.installed_json_after, b.installed_json_after);
}

#[test]
fn a_plan_is_refused_for_an_entry_policy_refuses() {
    let err = plan_for(
        "good-minimal",
        "community-unreviewed",
        &LocalState::default(),
        PlanOptions::default(),
    )
    .unwrap_err();
    assert_eq!(err.code(), "not-listed");
}

fn record(id: &str, version: &str, source: &str) -> InstalledRecord {
    InstalledRecord {
        id: id.into(),
        version: version.into(),
        source: source.into(),
        zip_sha256: (source == "catalogue").then(|| "a".repeat(64)),
        index_digest: (source == "catalogue").then(|| "b".repeat(64)),
        index_sequence: (source == "catalogue").then_some(2),
        installed: "2026-11-01T00:00:00Z".into(),
        state: "enabled".into(),
    }
}

fn state(records: Vec<InstalledRecord>, folders: &[&str]) -> LocalState {
    let mut installed = InstalledFile::default();
    for r in records {
        installed = installed.with_record(r);
    }
    LocalState {
        installed,
        folders: folders
            .iter()
            .map(|s| s.to_string())
            .collect::<BTreeSet<_>>(),
    }
}

#[test]
fn a_local_folder_is_not_replaced_without_a_confirm() {
    let s = state(vec![], &["pilot"]); // a hand-copied folder
    assert_eq!(
        plan_for("good-minimal", "first-party", &s, PlanOptions::default())
            .unwrap_err()
            .code(),
        "local-clash"
    );
    let s2 = state(vec![record("pilot", "0.1", "local")], &["pilot"]);
    assert_eq!(
        plan_for("good-minimal", "first-party", &s2, PlanOptions::default())
            .unwrap_err()
            .code(),
        "local-clash"
    );
    let ok = plan_for(
        "good-minimal",
        "first-party",
        &s,
        PlanOptions {
            replace_local: true,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(ok.replaces, Replaces::Local);
    assert_eq!(ok.record.state, "disabled");
}

#[test]
fn other_local_folders_do_not_block_and_are_untouched() {
    let s = state(vec![record("zzz", "1.0", "local")], &["zzz", "discord"]);
    let plan = plan_for("good-minimal", "first-party", &s, PlanOptions::default()).unwrap();
    let after = InstalledFile::parse(plan.installed_json_after.as_bytes()).unwrap();
    assert_eq!(after.plugins.len(), 2);
    assert_eq!(after.find("zzz"), s.installed.find("zzz"));
    assert!(plan.target_dir.ends_with("pilot"));
}

#[test]
fn updates_replace_a_catalogue_install_and_land_disabled_again() {
    let s = state(vec![record("pilot", "0.5", "catalogue")], &["pilot"]);
    let plan = plan_for("good-minimal", "first-party", &s, PlanOptions::default()).unwrap();
    assert_eq!(plan.replaces, Replaces::Catalogue("0.5".into()));
    assert_eq!(plan.record.state, "disabled");
    let after = InstalledFile::parse(plan.installed_json_after.as_bytes()).unwrap();
    assert_eq!(after.plugins.len(), 1);
    assert_eq!(after.plugins[0].version, "1.0");
}

#[test]
fn downgrade_needs_a_confirm() {
    let s = state(vec![record("pilot", "2.0", "catalogue")], &["pilot"]);
    assert_eq!(
        plan_for("good-minimal", "first-party", &s, PlanOptions::default())
            .unwrap_err()
            .code(),
        "downgrade"
    );
    let ok = plan_for(
        "good-minimal",
        "first-party",
        &s,
        PlanOptions {
            allow_downgrade: true,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(ok.replaces, Replaces::Catalogue("2.0".into()));
    // The same version again is a reinstall, not a downgrade.
    let same = state(vec![record("pilot", "1.0", "catalogue")], &["pilot"]);
    assert!(plan_for("good-minimal", "first-party", &same, PlanOptions::default()).is_ok());
}

#[test]
fn an_unparseable_installed_version_asks_for_the_confirm() {
    let s = state(vec![record("pilot", "banana", "catalogue")], &["pilot"]);
    assert_eq!(
        plan_for("good-minimal", "first-party", &s, PlanOptions::default())
            .unwrap_err()
            .code(),
        "downgrade"
    );
    let ok = plan_for(
        "good-minimal",
        "first-party",
        &s,
        PlanOptions {
            allow_downgrade: true,
            ..Default::default()
        },
    );
    assert!(ok.is_ok());
}

#[test]
fn plan_install_refuses_a_report_that_is_not_for_the_entry() {
    let acc = open(&doc(4, vec![entry_for("good-minimal", "first-party")])).unwrap();
    let e = &acc.index.plugins[0];
    let report = verify_download(&corpus_zip("good-minimal"), e).unwrap();
    let plan = |entry: &Entry| {
        plan_install(
            entry,
            &report,
            &acc.digest,
            acc.index.sequence,
            &host(),
            &LocalState::default(),
            PlanOptions::default(),
            Path::new("C:/sc-offline"),
            "2026-11-20T10:00:00Z",
        )
    };
    assert!(plan(e).is_ok());
    let mut v = e.clone();
    v.version = "9.9".into();
    assert_eq!(code(plan(&v)), "zip-mismatch");
    let mut k = e.clone();
    k.kind = scol_plugins::manifest::Kind::Lua;
    assert_eq!(code(plan(&k)), "zip-mismatch");
    let mut a = e.clone();
    a.api_minor = 0;
    a.api = format!("{}.0", a.api_major);
    if a.api_minor != e.api_minor {
        assert_eq!(code(plan(&a)), "zip-mismatch");
    }
    let mut z = e.clone();
    z.zip_size += 1;
    assert_eq!(code(plan(&z)), "zip-mismatch");
}

#[test]
fn installed_json_is_strict_and_round_trips() {
    let f = InstalledFile::default()
        .with_record(record("b", "1", "catalogue"))
        .with_record(record("a", "1", "local"));
    let text = f.render();
    assert!(text.ends_with("}\n"));
    assert_eq!(InstalledFile::parse(text.as_bytes()).unwrap(), f);
    assert_eq!(f.render(), text);
    assert_eq!(f.plugins[0].id, "a");
    for bad in [
        text.replace("\"schema\": 1", "\"schema\": 2"),
        text.replace("\"id\": \"a\"", "\"id\": \"a\", \"extra\": 1"),
        text.replace("\"source\": \"local\"", "\"source\": \"web\""),
        text.replace("\"state\": \"enabled\"", "\"state\": \"maybe\""),
        text.replace("\"id\": \"a\"", "\"id\": \"c\""), // unsorted
        "{\"schema\":1,\"schema\":1,\"plugins\":[]}".to_string(),
        "not json".to_string(),
    ] {
        assert_eq!(
            InstalledFile::parse(bad.as_bytes()).unwrap_err().code(),
            "installed-format",
            "{bad}"
        );
    }
}

// ---- fetching (a local fake) ----------------------------------------------------------------

#[derive(Default)]
struct FakeFetcher {
    files: Vec<(String, PathBuf)>,
    /// url -> Location, answered as a 302.
    redirects: Vec<(String, String)>,
    calls: Vec<String>,
    /// Pretend the server sends this many bytes more than asked.
    overshoot: bool,
}

impl FakeFetcher {
    fn find(&self, url: &str) -> Result<&PathBuf, String> {
        self.files
            .iter()
            .find(|(u, _)| u == url)
            .map(|(_, p)| p)
            .ok_or_else(|| format!("404 {url}"))
    }
    fn redirect(&self, url: &str) -> Option<String> {
        self.redirects
            .iter()
            .find(|(u, _)| u == url)
            .map(|(_, l)| l.clone())
    }
}

impl Fetcher for FakeFetcher {
    fn fetch_bytes(&mut self, url: &str, max_bytes: u64) -> Result<Step<Vec<u8>>, String> {
        self.calls.push(url.to_string());
        if let Some(l) = self.redirect(url) {
            return Ok(Step::Redirect(l));
        }
        let mut b = std::fs::read(self.find(url)?).map_err(|e| e.to_string())?;
        if self.overshoot {
            b.resize(max_bytes as usize + 1, b'x');
        }
        Ok(Step::Done(b))
    }
    fn fetch_to_file(
        &mut self,
        url: &str,
        dest: &Path,
        _max_bytes: u64,
    ) -> Result<Step<u64>, String> {
        self.calls.push(url.to_string());
        if let Some(l) = self.redirect(url) {
            return Ok(Step::Redirect(l));
        }
        let src = self.find(url)?.clone();
        std::fs::copy(src, dest)
            .map(Step::Done)
            .map_err(|e| e.to_string())
    }
}

#[test]
fn fetch_then_open_then_download_then_plan() {
    let scratch = Scratch::new("fetch");
    let v = doc(11, vec![entry_for("good-minimal", "first-party")]);
    let bytes = to_bytes(&v);
    let idx = scratch.path().join("index.json");
    let sigf = scratch.path().join("index.json.sig");
    std::fs::write(&idx, &bytes).unwrap();
    std::fs::write(&sigf, sign(&bytes)).unwrap();
    let zip_url = v["plugins"][0]["zip"]["url"].as_str().unwrap().to_string();
    let mut f = FakeFetcher {
        files: vec![
            (
                "https://github.com/scuba-sc/sc-plugins/releases/latest/download/index.json".into(),
                idx,
            ),
            (
                "https://github.com/scuba-sc/sc-plugins/releases/latest/download/index.json.sig"
                    .into(),
                sigf,
            ),
            (zip_url, corpus_zip("good-minimal")),
        ],
        ..Default::default()
    };
    let hosts = ["github.com"];
    let (ib, sig) = fetch_index(
        &mut f,
        "https://github.com/scuba-sc/sc-plugins/releases/latest/download/index.json",
        "https://github.com/scuba-sc/sc-plugins/releases/latest/download/index.json.sig",
        &hosts,
        &[],
    )
    .unwrap();
    let acc = open_index(&ib, &sig, &test_root(), now(), None).unwrap();
    let entry = acc.index.find("pilot").unwrap();
    let dest = scratch.path().join("pilot.zip");
    download_zip(&mut f, entry, &dest, &hosts, &[]).unwrap();
    let report = verify_download(&dest, entry).unwrap();
    let plan = plan_install(
        entry,
        &report,
        &acc.digest,
        acc.index.sequence,
        &host(),
        &LocalState::default(),
        PlanOptions::default(),
        scratch.path(),
        "2026-11-20T10:00:00Z",
    )
    .unwrap();
    assert_eq!(plan.record.index_sequence, Some(11));
    assert_eq!(f.calls.len(), 3);
    // The plan wrote nothing under the launcher folder.
    assert!(!scratch.path().join("data").exists());
}

#[test]
fn fetching_refuses_other_hosts_and_oversize_answers() {
    let scratch = Scratch::new("hosts");
    let idx = scratch.path().join("index.json");
    std::fs::write(&idx, b"{}").unwrap();
    let mut f = FakeFetcher {
        files: vec![("https://evil.example/index.json".into(), idx.clone())],
        ..Default::default()
    };
    let err = fetch_index(
        &mut f,
        "https://evil.example/index.json",
        "https://evil.example/index.json.sig",
        &["github.com"],
        &[],
    )
    .unwrap_err();
    assert_eq!(err.code(), "bad-url");
    assert!(
        f.calls.is_empty(),
        "nothing is requested from a host that is not allowed"
    );
    let mut f = FakeFetcher {
        files: vec![
            ("https://github.com/a/index.json".into(), idx.clone()),
            ("https://github.com/a/index.json.sig".into(), idx),
        ],
        overshoot: true,
        ..Default::default()
    };
    let err = fetch_index(
        &mut f,
        "https://github.com/a/index.json",
        "https://github.com/a/index.json.sig",
        &["github.com"],
        &[],
    )
    .unwrap_err();
    assert_eq!(err.code(), "too-large");
    let mut e = pilot_entry();
    e.zip_url = "http://github.com/a.zip".into();
    assert_eq!(
        code(download_zip(
            &mut f,
            &e,
            &scratch.path().join("x.zip"),
            &["github.com"],
            &[]
        )),
        "bad-url"
    );
    assert!(check_url("https://github.com/a/b", &["github.com"]).is_ok());
}

const CDN: &str = "objects.example-cdn.test";

fn redirect_fixture() -> (FakeFetcher, Entry) {
    let zip = corpus_zip("good-minimal");
    let mut e = pilot_entry();
    e.zip_url = "https://github.com/a/b/releases/download/v1/p.zip".into();
    let f = FakeFetcher {
        files: vec![(format!("https://{CDN}/blob/p.zip?sig=abc123&exp=9"), zip)],
        redirects: vec![(
            e.zip_url.clone(),
            format!("https://{CDN}/blob/p.zip?sig=abc123&exp=9"),
        )],
        ..Default::default()
    };
    (f, e)
}

#[test]
fn a_redirect_to_the_listed_second_host_is_followed_and_each_hop_is_checked() {
    let scratch = Scratch::new("redir-ok");
    let (mut f, e) = redirect_fixture();
    let dest = scratch.path().join("p.zip");
    download_zip(&mut f, &e, &dest, &["github.com"], &[CDN]).unwrap();
    assert_eq!(f.calls.len(), 2, "{:?}", f.calls);
    assert!(dest.exists());
}

#[test]
fn a_redirect_off_the_lists_or_to_http_is_refused_before_it_is_requested() {
    let scratch = Scratch::new("redir-bad");
    for target in [
        "https://evil.example/p.zip",
        "http://objects.example-cdn.test/p.zip",
        "https://objects.example-cdn.test@evil.example/p.zip",
        "https://objects.example-cdn.test:8443/p.zip",
        "https://objects.example-cdn.test/p.zip#frag",
        "https://objects.example-cdn.test.evil.example/p.zip",
        "https://objects.example-cdn.test",
        "/relative/p.zip",
        "//objects.example-cdn.test/p.zip",
        "https://objects.example-cdn.test/p zip",
    ] {
        let (mut f, e) = redirect_fixture();
        f.redirects = vec![(e.zip_url.clone(), target.to_string())];
        let err = download_zip(
            &mut f,
            &e,
            &scratch.path().join("p.zip"),
            &["github.com"],
            &[CDN],
        )
        .unwrap_err();
        assert_eq!(err.code(), "bad-url", "{target}");
        assert_eq!(
            f.calls.len(),
            1,
            "{target} must not be requested: {:?}",
            f.calls
        );
    }
    // With no second host listed, even the CDN is refused.
    let (mut f, e) = redirect_fixture();
    let err = download_zip(
        &mut f,
        &e,
        &scratch.path().join("p.zip"),
        &["github.com"],
        &[],
    )
    .unwrap_err();
    assert_eq!(err.code(), "bad-url");
    assert_eq!(f.calls.len(), 1);
}

#[test]
fn redirect_chains_are_capped_and_index_fetches_follow_the_same_rules() {
    let scratch = Scratch::new("redir-chain");
    // A loop between two allowed URLs stops after MAX_REDIRECTS hops.
    let mut f = FakeFetcher {
        redirects: vec![
            (
                "https://github.com/a/x".into(),
                "https://github.com/a/y".into(),
            ),
            (
                "https://github.com/a/y".into(),
                "https://github.com/a/x".into(),
            ),
        ],
        ..Default::default()
    };
    let mut e = pilot_entry();
    e.zip_url = "https://github.com/a/x".into();
    let err = download_zip(
        &mut f,
        &e,
        &scratch.path().join("p.zip"),
        &["github.com"],
        &[],
    )
    .unwrap_err();
    assert_eq!(err.code(), "bad-url");
    assert_eq!(f.calls.len(), MAX_REDIRECTS + 1);
    // The index and its signature are redirect-checked too.
    let idx = scratch.path().join("i");
    std::fs::write(&idx, b"{}").unwrap();
    let mut f = FakeFetcher {
        files: vec![("https://github.com/a/index.json.sig".into(), idx)],
        redirects: vec![(
            "https://github.com/a/index.json".into(),
            "https://evil.example/index.json".into(),
        )],
        ..Default::default()
    };
    let err = fetch_index(
        &mut f,
        "https://github.com/a/index.json",
        "https://github.com/a/index.json.sig",
        &["github.com"],
        &[CDN],
    )
    .unwrap_err();
    assert_eq!(err.code(), "bad-url");
    assert_eq!(f.calls.len(), 1);
    // A redirect target may carry a query; the first URL still may not.
    assert!(check_redirect_url("https://a.test/p?x=1", &["a.test"], &[]).is_ok());
    assert!(check_url("https://a.test/p?x=1", &["a.test"]).is_err());
}

// ---- command line ---------------------------------------------------------------------------

#[derive(Default)]
struct Buf {
    out: Vec<String>,
    err: Vec<String>,
}

impl Io for Buf {
    fn out(&mut self, line: &str) {
        self.out.push(line.to_string());
    }
    fn err(&mut self, line: &str) {
        self.err.push(line.to_string());
    }
    fn now_unix(&self) -> i64 {
        now()
    }
}

fn args(a: &[&str]) -> Vec<String> {
    a.iter().map(|s| s.to_string()).collect()
}

#[test]
fn cli_list_and_plan() {
    let scratch = Scratch::new("cli");
    let v = doc(4, vec![entry_for("good-minimal", "first-party")]);
    let bytes = to_bytes(&v);
    let idx = scratch.path().join("index.json");
    let sigf = scratch.path().join("index.json.sig");
    std::fs::write(&idx, &bytes).unwrap();
    std::fs::write(&sigf, sign(&bytes)).unwrap();
    let (idx_s, sig_s) = (idx.to_str().unwrap(), sigf.to_str().unwrap());
    let launcher = scratch.path().join("launcher");
    let launcher_s = launcher.to_str().unwrap();
    let zip = corpus_zip("good-minimal");
    let zip_s = zip.to_str().unwrap();
    let common = ["--sdk", "2.4.1", "--api", "1.1"];

    let mut io = Buf::default();
    let list = [&["list", idx_s, sig_s][..], &common[..]].concat();
    assert_eq!(
        run_with(&args(&list), &mut io, &test_root()),
        0,
        "{:?}",
        io.err
    );
    assert!(
        io.out.iter().any(|l| l.starts_with("pilot 1.0 (native)")),
        "{:?}",
        io.out
    );
    assert!(io.out.iter().any(|l| l.contains("can be installed here")));
    assert!(
        io.out.iter().any(|l| l.contains("Not run in game")),
        "the evidence note is shown"
    );

    let mut io = Buf::default();
    let plan = [
        &["plan", idx_s, sig_s, "pilot", zip_s, "--root", launcher_s][..],
        &common[..],
    ]
    .concat();
    assert_eq!(
        run_with(&args(&plan), &mut io, &test_root()),
        0,
        "{:?}",
        io.err
    );
    assert!(io.out.iter().any(|l| l.contains("starts disabled")));
    assert!(io
        .out
        .iter()
        .any(|l| l.contains("warning") && l.contains("native code")));
    assert_eq!(
        io.out.last().map(String::as_str),
        Some("nothing was written")
    );
    assert!(!launcher.exists(), "plan writes nothing");

    // A local folder with the same id blocks the plan; --replace-local lets it through.
    std::fs::create_dir_all(launcher.join("data").join("plugins").join("pilot")).unwrap();
    let mut io = Buf::default();
    assert_eq!(run_with(&args(&plan), &mut io, &test_root()), 1);
    assert!(
        io.err[0].starts_with("refused (local-clash)"),
        "{:?}",
        io.err
    );
    let mut io = Buf::default();
    let confirmed = [&plan[..], &["--replace-local"][..]].concat();
    assert_eq!(
        run_with(&args(&confirmed), &mut io, &test_root()),
        0,
        "{:?}",
        io.err
    );

    // A higher last sequence is a rollback.
    let mut io = Buf::default();
    let zero = "0".repeat(64);
    let rolled = [
        &list[..],
        &["--last-sequence", "5", "--last-digest", zero.as_str()][..],
    ]
    .concat();
    assert_eq!(run_with(&args(&rolled), &mut io, &test_root()), 1);
    assert!(io.err[0].starts_with("refused (rollback)"));

    // The same sequence with another digest is a replay.
    let mut io = Buf::default();
    let replay = [
        &list[..],
        &["--last-sequence", "4", "--last-digest", zero.as_str()][..],
    ]
    .concat();
    assert_eq!(run_with(&args(&replay), &mut io, &test_root()), 1);
    assert!(
        io.err[0].starts_with("refused (sequence-replay)"),
        "{:?}",
        io.err
    );

    // One of the two flags alone is a usage error.
    let mut io = Buf::default();
    let lone = [&list[..], &["--last-sequence", "4"][..]].concat();
    assert_eq!(run_with(&args(&lone), &mut io, &test_root()), 1);
    assert!(io.err[0].contains("go together"));
}

#[test]
fn cli_refuses_bad_input_with_exit_1() {
    let scratch = Scratch::new("cli-bad");
    let bytes = to_bytes(&doc(4, vec![]));
    let idx = scratch.path().join("index.json");
    let sigf = scratch.path().join("index.json.sig");
    std::fs::write(&idx, &bytes).unwrap();
    std::fs::write(&sigf, sign(&bytes)).unwrap();
    let (idx_s, sig_s) = (idx.to_str().unwrap(), sigf.to_str().unwrap());
    let ok_tail = ["--sdk", "2.4.1", "--api", "1.1"];
    // The compiled-in trust root is empty: refused.
    let mut io = Buf::default();
    let list = [&["list", idx_s, sig_s][..], &ok_tail[..]].concat();
    assert_eq!(scol_catalogue::run(&args(&list), &mut io), 1);
    assert!(io.err[0].contains("no-trust-keys"));
    // Usage errors.
    for bad in [
        vec![],
        vec!["frobnicate"],
        vec!["list"],
        vec!["list", idx_s, sig_s],
        vec!["list", idx_s, sig_s, "--sdk", "x", "--api", "1.1"],
        vec!["list", idx_s, sig_s, "--sdk", "2.4.1", "--api", "1"],
        vec![
            "list", idx_s, sig_s, "--sdk", "2.4.1", "--api", "1.1", "--bogus",
        ],
        vec![
            "plan", idx_s, sig_s, "x", "y.zip", "--sdk", "2.4.1", "--api", "1.1",
        ],
        vec![
            "list",
            "no-such-file",
            sig_s,
            "--sdk",
            "2.4.1",
            "--api",
            "1.1",
        ],
    ] {
        let mut io = Buf::default();
        assert_eq!(run_with(&args(&bad), &mut io, &test_root()), 1, "{bad:?}");
        assert!(!io.err.is_empty(), "{bad:?}");
    }
    // Unknown plugin id.
    let mut io = Buf::default();
    let plan = [
        "plan",
        idx_s,
        sig_s,
        "nothere",
        "x.zip",
        "--root",
        scratch.path().to_str().unwrap(),
        "--sdk",
        "2.4.1",
        "--api",
        "1.1",
    ];
    assert_eq!(run_with(&args(&plan), &mut io, &test_root()), 1);
    assert!(io.err[0].contains("not-in-index"));
}

// ---- no network code ------------------------------------------------------------------------

#[test]
fn the_crate_has_no_network_code() {
    let src = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("src");
    let manifest = text(&PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("Cargo.toml"));
    for word in [
        "reqwest",
        "hyper",
        "ureq",
        "winhttp",
        "curl",
        "tokio",
        "socket",
        "native-tls",
        "rustls",
    ] {
        assert!(
            !manifest.to_lowercase().contains(word),
            "Cargo.toml mentions {word}"
        );
    }
    for entry in std::fs::read_dir(src).unwrap() {
        let p = entry.unwrap().path();
        let body = text(&p);
        for word in ["std::net", "TcpStream", "UdpSocket", "WinHttp", "unsafe "] {
            assert!(!body.contains(word), "{} contains {word}", p.display());
        }
    }
}

// ---- review fixes ---------------------------------------------------------------------------

#[test]
fn index_issued_in_the_future_is_refused() {
    let bytes = to_bytes(&doc(1, vec![]));
    let sig = sign(&bytes);
    let issued = parse_utc(ISSUED).unwrap();
    // One day of skew is allowed, more is not.
    assert!(open_index(&bytes, &sig, &test_root(), issued - 86_400, None).is_ok());
    assert_eq!(
        code(open_index(
            &bytes,
            &sig,
            &test_root(),
            issued - 86_401,
            None
        )),
        "not-yet-valid"
    );
}

#[test]
fn staged_copy_is_hashed_verified_and_independent_of_the_source() {
    let e = pilot_entry();
    let base = std::env::temp_dir().join(format!("scol-catalogue-stage-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&base);
    let src = base.join("download.zip");
    std::fs::create_dir_all(&base).unwrap();
    std::fs::copy(corpus_zip("good-minimal"), &src).unwrap();
    let (staged, report) =
        scol_catalogue::install::stage_download(&src, &e, &base.join("staging").join(&e.id))
            .expect("stage");
    assert_eq!(report.archive.sha256, e.zip_sha256);
    // Swapping the source afterwards does not touch the staged copy.
    std::fs::write(&src, b"swapped").unwrap();
    assert_eq!(
        sha256::hex_digest(&std::fs::read(&staged).unwrap()),
        e.zip_sha256
    );
    let _ = std::fs::remove_dir_all(&base);
}

#[test]
fn staging_a_wrong_file_leaves_nothing_behind() {
    let e = pilot_entry();
    let base = std::env::temp_dir().join(format!("scol-catalogue-stage2-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&base);
    std::fs::create_dir_all(&base).unwrap();
    let src = base.join("bad.zip");
    // Same size as the index says, different bytes.
    std::fs::write(&src, vec![7u8; e.zip_size as usize]).unwrap();
    let dir = base.join("staging");
    assert_eq!(
        code(scol_catalogue::install::stage_download(&src, &e, &dir)),
        "zip-digest"
    );
    assert!(!dir.join(scol_catalogue::install::STAGED_ZIP).exists());
    // A longer file is refused too.
    std::fs::write(&src, vec![7u8; e.zip_size as usize + 10]).unwrap();
    assert_eq!(
        code(scol_catalogue::install::stage_download(&src, &e, &dir)),
        "zip-digest"
    );
    let _ = std::fs::remove_dir_all(&base);
}
