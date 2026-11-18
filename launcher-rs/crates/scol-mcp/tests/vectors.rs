// SPDX-License-Identifier: GPL-3.0-only
//! Replays the MCP sessions and stdio framing cases that the C++ server answered
//! (`tools/launcher-mcp/dump.cpp` into `launcher-rs/tests/mcp-vectors/vectors.json`). Every reply line must be
//! byte for byte what `launcher/mcp_server.h` gave. The platform is a fake built from the same description the
//! C++ dump used. Synthetic data only; nothing here runs in game.

use std::cell::RefCell;
use std::collections::BTreeMap;
use std::rc::Rc;

use scol_mcp::json::{parse, serialize};
use scol_mcp::tools::TOOLS;
use scol_mcp::{serve, IniRead, LogRead, LogScan, Platform, PluginFolder, Server, StatusInfo};
use serde_json::Value;

fn vectors() -> Value {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/mcp-vectors/vectors.json");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    serde_json::from_str(&text).expect("vectors.json is JSON")
}

/// Resolves a text value: a string, `{"hex"}`, `{"repeat"}`, `{"lines"}` or an array of those.
fn text(v: &Value) -> Vec<u8> {
    match v {
        Value::String(s) => s.as_bytes().to_vec(),
        Value::Array(a) => a.iter().flat_map(text).collect(),
        Value::Object(o) => {
            if let Some(h) = o.get("hex") {
                let h = h.as_str().unwrap();
                (0..h.len())
                    .step_by(2)
                    .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
                    .collect()
            } else if let Some(r) = o.get("repeat") {
                text(&r["unit"]).repeat(r["count"].as_u64().unwrap() as usize)
            } else if let Some(l) = o.get("lines") {
                let t = text(&l["template"]);
                let mut out = Vec::new();
                for n in l["from"].as_i64().unwrap()..=l["to"].as_i64().unwrap() {
                    out.extend_from_slice(&replace(&t, b"{n}", n.to_string().as_bytes()));
                    out.push(b'\n');
                }
                out
            } else {
                panic!("not a text value: {v}")
            }
        }
        _ => panic!("not a text value: {v}"),
    }
}

fn replace(s: &[u8], what: &[u8], with: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < s.len() {
        if s[i..].starts_with(what) {
            out.extend_from_slice(with);
            i += what.len();
        } else {
            out.push(s[i]);
            i += 1;
        }
    }
    out
}

fn str_or(o: &Value, key: &str) -> Vec<u8> {
    o.get(key).map(text).unwrap_or_default()
}
fn bool_or(o: &Value, key: &str, d: bool) -> bool {
    o.get(key).and_then(|v| v.as_bool()).unwrap_or(d)
}
fn int_or(o: &Value, key: &str, d: i64) -> i64 {
    o.get(key).and_then(|v| v.as_i64()).unwrap_or(d)
}
fn str_list(v: Option<&Value>) -> Vec<Vec<u8>> {
    v.and_then(|v| v.as_array())
        .map(|a| a.iter().map(text).collect())
        .unwrap_or_default()
}

#[derive(Default)]
struct LogSpec {
    path: Vec<u8>,
    error: Vec<u8>,
    text: Vec<u8>,
    exists: bool,
    older: bool,
}

/// What the fake platform serves; an `update` step replaces parts of it between requests.
struct Spec {
    status: Option<StatusInfo>,
    plugins: Option<Vec<PluginFolder>>,
    logs: BTreeMap<String, LogSpec>,
    heads: BTreeMap<String, Vec<u8>>,
    head_size: BTreeMap<String, u64>,
    scan_error: BTreeMap<String, Vec<u8>>,
    chunk: usize,
    settings: Option<IniRead>,
}

const UPDATABLE: [&str; 8] = [
    "status",
    "plugins",
    "logs",
    "heads",
    "head_size",
    "scan_error",
    "scan_chunk",
    "settings",
];

fn load_spec(j: &Value) -> Spec {
    let status = j.get("status").map(|st| StatusInfo {
        launcher_version: str_or(st, "launcher_version"),
        game_folder_found: bool_or(st, "game_folder_found", false),
        game_folder: str_or(st, "game_folder"),
        game_build: str_or(st, "game_build"),
        mod_installed: bool_or(st, "mod_installed", false),
        eac: String::from_utf8(str_or(st, "eac")).unwrap(),
        network_blocked: bool_or(st, "network_blocked", false),
        eac_rename: bool_or(st, "eac_rename", false),
        plugins_setting_on: bool_or(st, "plugins_setting_on", true),
        // The C++ field is an int: the dump read it with the same truncation.
        plugins_enabled: int_or(st, "plugins_enabled", 0) as i32 as i64,
        plugins_total: int_or(st, "plugins_total", 0) as i32 as i64,
        pc_changes_left: str_or(st, "pc_changes_left"),
        wine: bool_or(st, "wine", false),
        game_running: bool_or(st, "game_running", false),
    });
    let plugins = j.get("plugins").map(|pl| {
        let mut out = Vec::new();
        for item in pl.as_array().unwrap() {
            let count = int_or(item, "count", 0);
            let width = int_or(item, "width", 1) as usize;
            let make = |n: &str| PluginFolder {
                folder: replace(&str_or(item, "folder"), b"{n}", n.as_bytes()),
                ini_text: replace(&str_or(item, "ini"), b"{n}", n.as_bytes()),
                disabled: bool_or(item, "disabled", false),
            };
            if count > 0 {
                for n in 1..=count {
                    out.push(make(&format!("{n:0width$}")));
                }
            } else {
                out.push(make(""));
            }
        }
        out
    });
    let mut logs = BTreeMap::new();
    if let Some(lg) = j.get("logs") {
        for (k, m) in lg.as_object().unwrap() {
            logs.insert(
                k.clone(),
                LogSpec {
                    path: str_or(m, "path"),
                    error: str_or(m, "error"),
                    text: str_or(m, "text"),
                    exists: bool_or(m, "exists", true),
                    older: bool_or(m, "older", false),
                },
            );
        }
    }
    let map_of = |key: &str| -> BTreeMap<String, Vec<u8>> {
        j.get(key)
            .map(|h| {
                h.as_object()
                    .unwrap()
                    .iter()
                    .map(|(k, v)| (k.clone(), text(v)))
                    .collect()
            })
            .unwrap_or_default()
    };
    let head_size = j
        .get("head_size")
        .map(|h| {
            h.as_object()
                .unwrap()
                .iter()
                .map(|(k, v)| (k.clone(), v.as_u64().unwrap()))
                .collect()
        })
        .unwrap_or_default();
    let settings = j.get("settings").map(|st| IniRead {
        path: str_or(st, "path"),
        exists: bool_or(st, "exists", true),
        error: str_or(st, "error"),
        text: str_or(st, "text"),
    });
    Spec {
        status,
        plugins,
        logs,
        heads: map_of("heads"),
        head_size,
        scan_error: map_of("scan_error"),
        chunk: int_or(j, "scan_chunk", 4096) as usize,
        settings,
    }
}

struct Fake {
    spec: Rc<RefCell<Spec>>,
    version: String,
    roots: Vec<Vec<u8>>,
    names: Vec<Vec<u8>>,
    game_log: bool,
    has_status: bool,
    has_plugins: bool,
    has_settings: bool,
}

impl Platform for Fake {
    fn version(&self) -> String {
        self.version.clone()
    }
    fn profile_roots(&self) -> Vec<Vec<u8>> {
        self.roots.clone()
    }
    fn account_names(&self) -> Vec<Vec<u8>> {
        self.names.clone()
    }
    fn game_log(&self) -> bool {
        self.game_log
    }
    fn status(&mut self) -> StatusInfo {
        if !self.has_status {
            return StatusInfo::default();
        }
        self.spec.borrow().status.clone().unwrap_or_default()
    }
    fn plugins(&mut self) -> Vec<PluginFolder> {
        if !self.has_plugins {
            return Vec::new();
        }
        self.spec.borrow().plugins.clone().unwrap_or_default()
    }
    fn read_log(&mut self, log: &str, max: usize) -> LogRead {
        let spec = self.spec.borrow();
        let Some(l) = spec.logs.get(log) else {
            return LogRead::default();
        };
        LogRead {
            path: l.path.clone(),
            exists: l.exists,
            error: l.error.clone(),
            bytes: if l.text.len() > max {
                l.text[l.text.len() - max..].to_vec()
            } else {
                l.text.clone()
            },
            skipped_older: l.older || l.text.len() > max,
        }
    }
    fn scan_log(&mut self, log: &str, max: u64, sink: &mut dyn FnMut(&[u8])) -> LogScan {
        let spec = self.spec.borrow();
        let mut r = LogScan::default();
        if let Some(e) = spec.scan_error.get(log) {
            r.exists = true;
            r.error = e.clone();
            return r;
        }
        let Some(text) = spec.heads.get(log) else {
            return r;
        };
        r.exists = true;
        r.size = text.len() as u64;
        if let Some(z) = spec.head_size.get(log) {
            r.size = *z;
        }
        while r.scanned < text.len() as u64 && r.scanned < max {
            let n = (spec.chunk as u64)
                .min(text.len() as u64 - r.scanned)
                .min(max - r.scanned) as usize;
            let at = r.scanned as usize;
            sink(&text[at..at + n]);
            r.scanned += n as u64;
        }
        r
    }
    fn settings(&mut self) -> IniRead {
        if !self.has_settings {
            return IniRead::default();
        }
        self.spec.borrow().settings.clone().unwrap_or_default()
    }
}

fn make_fake(j: &Value) -> (Fake, Rc<RefCell<Spec>>) {
    let spec = Rc::new(RefCell::new(load_spec(j)));
    let fake = Fake {
        spec: spec.clone(),
        version: j
            .get("version")
            .and_then(|v| v.as_str())
            .unwrap_or("2.4.1-test")
            .to_string(),
        roots: str_list(j.get("profile_roots")),
        names: str_list(j.get("account_names")),
        game_log: bool_or(j, "game_log", false),
        has_status: j.get("status").is_some(),
        has_plugins: j.get("plugins").is_some(),
        has_settings: j.get("settings").is_some(),
    };
    (fake, spec)
}

fn show(b: &[u8]) -> String {
    let s = String::from_utf8_lossy(b);
    if s.chars().count() > 400 {
        format!(
            "{}... ({} bytes)",
            s.chars().take(400).collect::<String>(),
            b.len()
        )
    } else {
        s.into_owned()
    }
}

fn first_difference(a: &[u8], b: &[u8]) -> usize {
    a.iter()
        .zip(b)
        .position(|(x, y)| x != y)
        .unwrap_or(a.len().min(b.len()))
}

fn context(b: &[u8], at: usize) -> String {
    let from = at.saturating_sub(120);
    let to = (at + 120).min(b.len());
    String::from_utf8_lossy(&b[from..to]).into_owned()
}

#[test]
fn sessions_match_the_cpp_server() {
    let v = vectors();
    let sessions = v["sessions"].as_array().unwrap();
    assert!(
        sessions.len() >= 20,
        "the vector file has {} sessions",
        sessions.len()
    );
    let mut failures = Vec::new();
    let mut requests = 0usize;
    for sc in sessions {
        let name = sc["name"].as_str().unwrap();
        let mut platform = sc["platform"].clone();
        let (fake, spec) = make_fake(&platform);
        let mut server = Server::new(Box::new(fake));
        for (n, step) in sc["steps"].as_array().unwrap().iter().enumerate() {
            if let Some(up) = step.get("update") {
                for (k, val) in up.as_object().unwrap() {
                    assert!(
                        UPDATABLE.contains(&k.as_str()),
                        "{name}: an update step cannot change {k}"
                    );
                    platform[k] = val.clone();
                }
                *spec.borrow_mut() = load_spec(&platform);
                continue;
            }
            requests += 1;
            let request = text(&step["request"]);
            let got = server.handle_line(request.clone());
            let want = match &step["reply"] {
                Value::Null => None,
                r => Some(text(r)),
            };
            if got != want {
                let detail = match (&got, &want) {
                    (Some(g), Some(w)) => {
                        let at = first_difference(g, w);
                        format!(
                            "first difference at byte {at}\n  rust: ...{}\n  cpp:  ...{}",
                            context(g, at),
                            context(w, at)
                        )
                    }
                    (g, w) => format!(
                        "rust {} a reply, C++ {} one",
                        if g.is_some() { "gave" } else { "gave no" },
                        if w.is_some() { "gave" } else { "gave no" }
                    ),
                };
                failures.push(format!(
                    "{name} step {n} request {}:\n  {detail}",
                    show(&request)
                ));
            }
        }
    }
    assert!(requests > 250, "only {requests} requests were replayed");
    if !failures.is_empty() {
        panic!(
            "{} of {requests} replies differ from the C++ server:\n{}",
            failures.len(),
            failures.into_iter().take(6).collect::<Vec<_>>().join("\n")
        );
    }
}

#[test]
fn stdio_framing_matches_the_cpp_server() {
    let v = vectors();
    let cases = v["serve"].as_array().unwrap();
    assert!(cases.len() >= 15);
    let mut failures = Vec::new();
    for sc in cases {
        let name = sc["name"].as_str().unwrap();
        let (fake, _spec) = make_fake(&sc["platform"]);
        let mut server = Server::new(Box::new(fake));
        let input = text(&sc["input"]);
        let chunk = sc["chunk"].as_u64().unwrap() as usize;
        let stop_after = sc.get("stop_after").and_then(|s| s.as_i64()).unwrap_or(-1);
        let mut at = 0usize;
        let mut replies: Vec<Vec<u8>> = Vec::new();
        serve(
            &mut server,
            &mut |buf: &mut [u8]| {
                let take = chunk.min(buf.len()).min(input.len() - at);
                buf[..take].copy_from_slice(&input[at..at + take]);
                at += take;
                take
            },
            &mut |line: &[u8]| {
                replies.push(line.to_vec());
                stop_after < 0 || (replies.len() as i64) < stop_after
            },
        );
        let want: Vec<Vec<u8>> = sc["output"].as_array().unwrap().iter().map(text).collect();
        if replies != want {
            failures.push(format!(
                "{name}: {} replies in Rust, {} in C++",
                replies.len(),
                want.len()
            ));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
fn tool_definitions_are_valid_json_and_complete() {
    for t in &TOOLS {
        for schema in [t.input_schema, t.output_schema] {
            let p = parse(schema.as_bytes());
            assert!(p.ok, "{}: {}", t.name, p.error);
            assert_eq!(p.value.find(b"type").unwrap().as_bytes(), b"object");
        }
        assert!(!t.title.is_empty() && !t.description.is_empty());
    }
    let names: Vec<&str> = TOOLS.iter().map(|t| t.name).collect();
    assert_eq!(
        names,
        ["get_status", "list_plugins", "read_log", "get_settings"]
    );
}

#[test]
fn numbers_keep_their_token_and_objects_keep_order() {
    let p = parse(br#"{"b":1.50,"a":[1E400,-0],"b":2}"#);
    assert!(p.ok);
    assert_eq!(serialize(&p.value), br#"{"b":2,"a":[1E400,-0]}"#.to_vec());
}
