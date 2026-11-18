// SPDX-License-Identifier: GPL-3.0-only
//! The read-only MCP server: JSON-RPC 2.0 over stdio, one message per line, four tools. Port of the
//! protocol, tool and limit code of `launcher/mcp_server.h`.
//!
//! The server exposes four read-only tools (`get_status`, `list_plugins`, `read_log`, `get_settings`) and
//! nothing else: no command, console, start/stop or settings write. Every string a tool returns is made
//! valid UTF-8 and redacted first. The game's Game.log is served only when the server was started with
//! `--game-log` ([`Platform::game_log`]); without it the file is never opened.
//!
//! The server never touches a file itself. A [`Platform`] supplies the status fields, the plugin folders,
//! the log tails and the `sc-offline.ini` text, so the same code runs over a fake platform in tests.

use scol_redact::{add_learned, learn, Identities, LoginScanner, Redactor, Source, MAX_PRIORITY};

use crate::json::{check_schema, clip_utf8, parse, sanitize_utf8, serialize, Value, MAX_LINE};
use crate::tools::{INSTRUCTIONS, TOOLS};

/// `read_log` scans at most this much of a file's end.
pub const MAX_LOG_BYTES: usize = 2 << 20;
/// The login-line scan reads at most this much of each log, from its start.
pub const MAX_SCAN_BYTES: u64 = 64 << 20;
/// `read_log` returns at most this much text.
pub const MAX_LOG_TEXT: usize = 64 << 10;
/// A longer line is cut before redaction.
pub const MAX_REDACT_LINE: usize = 4 * MAX_LOG_TEXT;
pub const MAX_PLUGINS: usize = 256;
/// A plugin.ini value.
pub const MAX_FIELD_BYTES: usize = 256;
pub const MAX_SETTINGS: usize = 1000;
pub const MAX_SETTING_BYTES: usize = 1024;
pub const DEFAULT_LINES: i64 = 100;

/// What `get_status` reports (`StatusInfo`).
#[derive(Clone, Debug)]
pub struct StatusInfo {
    pub launcher_version: Vec<u8>,
    pub game_folder_found: bool,
    /// Empty when not found.
    pub game_folder: Vec<u8>,
    /// Empty when unknown.
    pub game_build: Vec<u8>,
    pub mod_installed: bool,
    /// active | disabled | not_installed
    pub eac: String,
    pub network_blocked: bool,
    pub eac_rename: bool,
    pub plugins_setting_on: bool,
    pub plugins_enabled: i64,
    pub plugins_total: i64,
    /// Empty when none.
    pub pc_changes_left: Vec<u8>,
    pub wine: bool,
    pub game_running: bool,
}

impl Default for StatusInfo {
    fn default() -> Self {
        StatusInfo {
            launcher_version: Vec::new(),
            game_folder_found: false,
            game_folder: Vec::new(),
            game_build: Vec::new(),
            mod_installed: false,
            eac: String::new(),
            network_blocked: false,
            eac_rename: false,
            plugins_setting_on: true,
            plugins_enabled: 0,
            plugins_total: 0,
            pc_changes_left: Vec::new(),
            wine: false,
            game_running: false,
        }
    }
}

/// One plugin folder that has a plugin.ini.
#[derive(Clone, Debug, Default)]
pub struct PluginFolder {
    pub folder: Vec<u8>,
    /// The plugin.ini bytes.
    pub ini_text: Vec<u8>,
    /// A `disabled` file is in the folder.
    pub disabled: bool,
}

/// The tail of one log (`LogRead`).
#[derive(Clone, Debug, Default)]
pub struct LogRead {
    pub path: Vec<u8>,
    pub exists: bool,
    /// Set when the file exists but could not be read.
    pub error: Vec<u8>,
    /// The file's last bytes (at most the requested amount).
    pub bytes: Vec<u8>,
    /// The file is longer than `bytes`.
    pub skipped_older: bool,
}

/// What a streaming scan of one log delivered (`LogScan`): the platform calls the sink with consecutive chunks
/// of the file, from its start, and stops after `max_bytes`.
#[derive(Clone, Debug, Default)]
pub struct LogScan {
    pub exists: bool,
    /// Set when the file exists but could not be read.
    pub error: Vec<u8>,
    /// The file's size in bytes.
    pub size: u64,
    /// The bytes delivered to the sink.
    pub scanned: u64,
}

/// `sc-offline.ini` as read (`IniRead`).
#[derive(Clone, Debug, Default)]
pub struct IniRead {
    pub path: Vec<u8>,
    pub exists: bool,
    pub error: Vec<u8>,
    pub text: Vec<u8>,
}

/// What the launcher provides. Every function only reads. The default of each reads as "nothing there".
pub trait Platform {
    /// `SCO_VERSION`.
    fn version(&self) -> String;
    /// USERPROFILE, for the redactor.
    fn profile_roots(&self) -> Vec<Vec<u8>> {
        Vec::new()
    }
    /// The Windows account name: USERNAME and the last part of USERPROFILE.
    fn account_names(&self) -> Vec<Vec<u8>> {
        Vec::new()
    }
    fn status(&mut self) -> StatusInfo {
        StatusInfo::default()
    }
    /// Folders that have a plugin.ini.
    fn plugins(&mut self) -> Vec<PluginFolder> {
        Vec::new()
    }
    /// `log` is mod, launcher, launcher_previous or game.
    fn read_log(&mut self, _log: &str, _max_bytes: usize) -> LogRead {
        LogRead::default()
    }
    /// The whole file in chunks, for login lines.
    fn scan_log(&mut self, _log: &str, _max_bytes: u64, _sink: &mut dyn FnMut(&[u8])) -> LogScan {
        LogScan::default()
    }
    /// `--game-log`: `read_log` may serve (and scan) Game.log.
    fn game_log(&self) -> bool {
        false
    }
    /// sc-offline.ini next to the exe.
    fn settings(&mut self) -> IniRead {
        IniRead::default()
    }
}

/// What a tool returned (`ToolResult`).
pub struct ToolResult {
    pub is_error: bool,
    /// Matches the tool's output schema (when not an error).
    pub structured: Value,
    /// When an error.
    pub error_text: Vec<u8>,
}

fn failure(text: Vec<u8>) -> ToolResult {
    ToolResult {
        is_error: true,
        structured: Value::null(),
        error_text: text,
    }
}

fn ok_result(v: Value) -> ToolResult {
    ToolResult {
        is_error: false,
        structured: v,
        error_text: Vec::new(),
    }
}

fn cat(parts: &[&[u8]]) -> Vec<u8> {
    parts.concat()
}

/// The order `list_plugins` returns folders in: by name, ASCII-case-insensitive, then exact.
pub fn folder_less(a: &[u8], b: &[u8]) -> bool {
    let x = a.to_ascii_lowercase();
    let y = b.to_ascii_lowercase();
    if x != y {
        x < y
    } else {
        a < b
    }
}

fn trim_space(s: &[u8]) -> &[u8] {
    let is = |c: &u8| matches!(c, b' ' | b'\t' | b'\r' | b'\n');
    let Some(b) = s.iter().position(|c| !is(c)) else {
        return &[];
    };
    let e = s.iter().rposition(|c| !is(c)).unwrap();
    &s[b..=e]
}

/// Lines of an ini-style text: BOM dropped, `\n` or `\r\n` ends, trimmed.
fn ini_lines(text: &[u8]) -> Vec<Vec<u8>> {
    let text = text.strip_prefix(b"\xEF\xBB\xBF").unwrap_or(text);
    text.split(|&c| c == b'\n')
        .map(|l| trim_space(l).to_vec())
        .collect()
}

fn find_byte(s: &[u8], c: u8) -> Option<usize> {
    s.iter().position(|&x| x == c)
}

/// The server over one [`Platform`].
pub struct Server {
    platform: Box<dyn Platform>,
    /// What earlier `read_log` calls learned (the lists; the roots and names are set again on every use).
    session: Identities,
    /// `clean`'s compiled redactor; reset when `session` changes.
    cleaner: Option<Redactor>,
    /// `session` holds the identities scanned from the logs.
    scanned: bool,
}

impl Server {
    pub fn new(platform: Box<dyn Platform>) -> Self {
        Server {
            platform,
            session: Identities::default(),
            cleaner: None,
            scanned: false,
        }
    }

    /// One request line in. `Some(reply)` (no newline) when a reply line was produced.
    pub fn handle_line(&mut self, mut line: Vec<u8>) -> Option<Vec<u8>> {
        while line.last() == Some(&b'\r') {
            line.pop();
        }
        if trim_space(&line).is_empty() {
            return None;
        }
        if line.len() > MAX_LINE {
            return Some(error_line(
                &Value::null(),
                -32600,
                b"Request line is longer than 1 MiB",
            ));
        }
        let p = parse(&line);
        if !p.ok {
            return Some(error_line(
                &Value::null(),
                -32700,
                &cat(&[b"Parse error: ", p.error.as_bytes()]),
            ));
        }
        let msg = &p.value;
        if msg.is_array() {
            return Some(error_line(
                &Value::null(),
                -32600,
                b"Batch requests are not supported",
            ));
        }
        if !msg.is_object() {
            return Some(error_line(
                &Value::null(),
                -32600,
                b"A request must be a JSON object",
            ));
        }

        let id = msg.find(b"id");
        let has_id = id.is_some();
        if let Some(i) = id {
            if !(i.is_string() || i.is_number() || i.is_null()) {
                return Some(error_line(
                    &Value::null(),
                    -32600,
                    b"The id must be a string, a number or null",
                ));
            }
        }
        let method = msg.find(b"method");
        if method.is_none() && (msg.find(b"result").is_some() || msg.find(b"error").is_some()) {
            return None; // a response from the client: nothing is awaiting it
        }
        let version = msg.find(b"jsonrpc");
        let valid = matches!(version, Some(v) if v.is_string() && v.as_bytes() == b"2.0")
            && matches!(method, Some(m) if m.is_string());
        if !valid {
            let echo = id.cloned().unwrap_or_default();
            return Some(error_line(
                &echo,
                -32600,
                b"Invalid request: jsonrpc must be \"2.0\" and method a string",
            ));
        }
        if !has_id {
            return None; // a notification gets no reply
        }
        let id = id.unwrap();
        let params = msg.find(b"params");
        if let Some(p) = params {
            if !p.is_object() {
                return Some(error_line(id, -32602, b"params must be an object"));
            }
        }
        Some(self.dispatch(id, method.unwrap().as_bytes(), params))
    }

    /// The reply for a request line that was longer than 1 MiB.
    pub fn oversize_reply(&self) -> Vec<u8> {
        error_line(&Value::null(), -32600, b"Request line is longer than 1 MiB")
    }

    /// Runs one tool; the same result `tools/call` wraps.
    pub fn run_tool(&mut self, name: &str, args: &Value) -> ToolResult {
        match name {
            "get_status" => self.get_status(),
            "list_plugins" => self.list_plugins(),
            "read_log" => self.read_log(args),
            _ => self.get_settings(),
        }
    }

    /// Every string the server returns goes through here: profile roots, account names and what earlier
    /// `read_log` calls learned (a handle that sits in a folder name is hidden there too). The compiled
    /// redactor is kept until the learned lists change.
    fn clean(&mut self, s: &[u8]) -> Vec<u8> {
        if self.cleaner.is_none() {
            // The first string of a session already knows the handles the logs announce, so a handle in a folder
            // name is hidden in get_status and get_settings before any read_log call.
            if !self.scanned {
                let mut ignored = String::new();
                self.session = self.scan_identities(&mut ignored);
                self.scanned = true;
            }
            let mut id = self.session.clone();
            id.profile_roots = self.platform.profile_roots();
            id.account_names = self.platform.account_names();
            self.cleaner = Some(Redactor::new(&id));
        }
        self.cleaner.as_ref().unwrap().apply(&sanitize_utf8(s))
    }

    fn dispatch(&mut self, id: &Value, method: &[u8], params: Option<&Value>) -> Vec<u8> {
        match method {
            b"initialize" => {
                let mut chosen: Vec<u8> = b"2025-11-25".to_vec();
                if let Some(asked) = params.and_then(|p| p.find(b"protocolVersion")) {
                    if !asked.is_string() {
                        return error_line(id, -32602, b"protocolVersion must be a string");
                    }
                    if supported(asked.as_bytes()) {
                        chosen = asked.as_bytes().to_vec();
                    }
                }
                let mut result = Value::object();
                let mut caps = Value::object();
                let mut tools = Value::object();
                let mut info = Value::object();
                tools.set("listChanged", Value::bool(false));
                caps.set("tools", tools);
                info.set("name", Value::str("sc-offline"));
                info.set("title", Value::str("sc-offline"));
                info.set("version", Value::str(self.platform.version()));
                result.set("protocolVersion", Value::str(chosen));
                result.set("capabilities", caps);
                result.set("serverInfo", info);
                result.set("instructions", Value::str(INSTRUCTIONS));
                result_line(id, result)
            }
            b"ping" => result_line(id, Value::object()),
            b"tools/list" => result_line(id, list_tools()),
            b"tools/call" => self.call_tool(id, params),
            _ => error_line(id, -32601, &cat(&[b"Method not found: ", method])),
        }
    }

    fn call_tool(&mut self, id: &Value, params: Option<&Value>) -> Vec<u8> {
        let name = params.and_then(|p| p.find(b"name"));
        let Some(name) = name.filter(|n| n.is_string()) else {
            return error_line(id, -32602, b"tools/call needs a string 'name'");
        };
        let params = params.unwrap();
        let Some(def) = TOOLS.iter().find(|t| t.name.as_bytes() == name.as_bytes()) else {
            let shown = self.clean(name.as_bytes());
            return error_line(id, -32602, &cat(&[b"Unknown tool '", &shown, b"'"]));
        };
        let mut args = Value::object();
        if let Some(given) = params.find(b"arguments") {
            if !given.is_object() {
                return error_line(
                    id,
                    -32602,
                    &cat(&[
                        b"Invalid arguments for ",
                        def.name.as_bytes(),
                        b": arguments must be an object",
                    ]),
                );
            }
            args = given.clone();
        }
        let problem = check_schema(&parse_or_null(def.input_schema), &args, b"arguments");
        if !problem.is_empty() {
            let shown = self.clean(&problem);
            return error_line(
                id,
                -32602,
                &cat(&[
                    b"Invalid arguments for ",
                    def.name.as_bytes(),
                    b": ",
                    &shown,
                ]),
            );
        }

        let r = self.run_tool(def.name, &args);
        let mut result = Value::object();
        let mut content = Value::array();
        let mut text = Value::object();
        text.set("type", Value::str("text"));
        text.set(
            "text",
            Value::str(if r.is_error {
                r.error_text.clone()
            } else {
                serialize(&r.structured)
            }),
        );
        content.push(text);
        result.set("content", content);
        if !r.is_error {
            result.set("structuredContent", r.structured.clone());
        }
        result.set("isError", Value::bool(r.is_error));
        result_line(id, result)
    }

    fn get_status(&mut self) -> ToolResult {
        let s = self.platform.status();
        let mut o = Value::object();
        let version = if s.launcher_version.is_empty() {
            self.platform.version().into_bytes()
        } else {
            s.launcher_version.clone()
        };
        let v = self.clean(&version);
        o.set("launcher_version", Value::str(v));
        o.set("game_folder_found", Value::bool(s.game_folder_found));
        let folder = if s.game_folder_found {
            self.clean(&s.game_folder)
        } else {
            Vec::new()
        };
        o.set("game_folder", Value::str(folder));
        let build = self.clean(&s.game_build);
        o.set("game_build", Value::str(build));
        o.set("mod_installed", Value::bool(s.mod_installed));
        let eac = if s.eac == "active" || s.eac == "disabled" {
            s.eac.clone()
        } else {
            "not_installed".to_string()
        };
        o.set("eac", Value::str(eac));
        o.set("network_blocked", Value::bool(s.network_blocked));
        o.set("eac_rename", Value::bool(s.eac_rename));
        o.set("plugins_setting_on", Value::bool(s.plugins_setting_on));
        o.set("plugins_enabled", Value::int(s.plugins_enabled.max(0)));
        o.set("plugins_total", Value::int(s.plugins_total.max(0)));
        let left = self.clean(&s.pc_changes_left);
        o.set("pc_changes_left", Value::str(left));
        o.set("wine", Value::bool(s.wine));
        o.set("game_running", Value::bool(s.game_running));
        ok_result(o)
    }

    fn list_plugins(&mut self) -> ToolResult {
        let mut folders = self.platform.plugins();
        folders.sort_by(|a, b| {
            let x = a.folder.to_ascii_lowercase();
            let y = b.folder.to_ascii_lowercase();
            x.cmp(&y).then_with(|| a.folder.cmp(&b.folder))
        });
        let cut = folders.len() > MAX_PLUGINS;
        if cut {
            folders.truncate(MAX_PLUGINS);
        }
        const KEYS: [&str; 8] = [
            "id",
            "name",
            "version",
            "author",
            "api",
            "kind",
            "entry",
            "description",
        ];
        let mut list = Value::array();
        for f in &folders {
            let fields = plugin_fields(&f.ini_text);
            let mut p = Value::object();
            let mut warnings = Value::array();
            let folder = self.clean(&f.folder);
            p.set("folder", Value::str(folder.clone()));
            p.set("enabled", Value::bool(!f.disabled));
            let mut id: Vec<u8> = Vec::new();
            for key in KEYS {
                let mut value: Vec<u8> = Vec::new();
                for (k, v) in &fields {
                    if k.as_slice() == key.as_bytes() {
                        value = v.clone(); // the last one wins
                    }
                }
                value = self.clean(&value);
                if value.len() > MAX_FIELD_BYTES {
                    value = clip_utf8(&value, MAX_FIELD_BYTES);
                    warnings.push(Value::str(format!(
                        "plugin.ini {key} is longer than 256 bytes and was shortened"
                    )));
                }
                if key == "id" {
                    id = value.clone();
                }
                p.set(key, Value::str(value));
            }
            if id.is_empty() {
                warnings.push(Value::str("plugin.ini has no id"));
            } else if id != folder {
                warnings.push(Value::str(cat(&[
                    b"plugin.ini id '",
                    &id,
                    b"' does not match the folder name '",
                    &folder,
                    b"'",
                ])));
            }
            for key in ["name", "version"] {
                if p.find(key.as_bytes()).unwrap().as_bytes().is_empty() {
                    warnings.push(Value::str(format!("plugin.ini has no {key}")));
                }
            }
            p.set("warnings", warnings);
            list.push(p);
        }
        let mut o = Value::object();
        o.set("count", Value::int(list.items().len() as i64));
        o.set("truncated", Value::bool(cut));
        o.set("plugins", list);
        ok_result(o)
    }

    /// Reads the login lines of every log from its start (in chunks, login lines only, at most `MAX_SCAN_BYTES`
    /// of each; Game.log only with the flag) into fresh identities. `notice` collects what could not be read
    /// in full.
    fn scan_identities(&mut self, notice: &mut String) -> Identities {
        let mut identities = Identities {
            profile_roots: self.platform.profile_roots(),
            account_names: self.platform.account_names(),
            ..Identities::default()
        };
        for name in ["mod", "launcher", "launcher_previous", "game"] {
            if name == "game" && !self.platform.game_log() {
                continue; // Game.log is not even opened without the flag
            }
            let mut scanner = LoginScanner::new(&mut identities, Some(sanitize_for_scanner));
            let scan = self
                .platform
                .scan_log(name, MAX_SCAN_BYTES, &mut |data| scanner.feed(data));
            scanner.finish();
            let (capped, cut_login) = (scanner.capped(), scanner.cut_login());
            let sep = |n: &String| if n.is_empty() { "" } else { " " };
            if capped {
                let lead = sep(notice);
                notice.push_str(&format!(
                    "{lead}The {name} log has more login lines than were read (at most 4096 lines or 4 MiB of them per log), \
                     so a handle or name that only a later login line announces may not be hidden outside its own line."
                ));
            }
            if cut_login {
                let lead = sep(notice);
                notice.push_str(&format!(
                    "{lead}A login line in the {name} log is longer than 16 KiB: only its first 16 KiB were read, \
                     so a name that comes later in that line may not be hidden elsewhere."
                ));
            }
            if !scan.error.is_empty() {
                let lead = sep(notice);
                notice.push_str(&format!(
                    "{lead}The {name} log could not be read for login lines, so a name only it announces may not be hidden."
                ));
            } else if scan.exists && scan.size > scan.scanned {
                let lead = sep(notice);
                notice.push_str(&format!(
                    "{lead}The {name} log is larger than 64 MiB: only its first 64 MiB were scanned for login lines, \
                     so a handle or name that only a later login line announces may not be hidden outside its own line."
                ));
            }
        }
        identities
    }

    fn read_log(&mut self, args: &Value) -> ToolResult {
        let log = args.find(b"log").unwrap().as_bytes().to_vec();
        let log_name = String::from_utf8_lossy(&log).into_owned();
        let mut want = DEFAULT_LINES;
        if let Some(n) = args.find(b"lines") {
            if let Some(v) = n.as_integer() {
                want = v;
            }
        }
        let needle: Vec<u8> = args
            .find(b"contains")
            .map(|c| c.as_bytes().to_ascii_lowercase())
            .unwrap_or_default();

        if log_name == "game" && !self.platform.game_log() {
            return failure(
                b"Game.log is not enabled. Start the server as `sc-offline.exe mcp --game-log` to let read_log serve it. \
It is off by default because Game.log can contain account names, handles and ids; the other logs (mod, launcher, launcher_previous) are available. \
With the flag, the same redaction applies, but a name that no key or login line announces can still show."
                    .to_vec(),
            );
        }

        let read = self.platform.read_log(&log_name, MAX_LOG_BYTES);
        if !read.error.is_empty() {
            let shown = self.clean(&read.error);
            return failure(cat(&[b"Could not read the ", &log, b" log: ", &shown]));
        }

        // Split the tail into lines. When older bytes were skipped the first piece may be cut: drop it.
        let bytes = &read.bytes;
        let mut pos = 0usize;
        if read.skipped_older {
            pos = match find_byte(bytes, b'\n') {
                Some(nl) => nl + 1,
                None => bytes.len(),
            };
        }
        // Learn handles, nicknames, character names and ids first (scan_identities), then the part that is shown
        // in full, so a name that one login line announces is hidden on every other line and in free text too.
        // The lists are rebuilt on every call, so values from earlier calls cannot fill them up.
        let mut notice = String::new();
        let mut identities = self.scan_identities(&mut notice);
        // What earlier calls learned from ordinary lines (the second list) is kept for the session; the
        // login-line list is not.
        let earlier: Vec<Vec<u8>> = self.session.learned.clone();
        for v in &earlier {
            add_learned(&mut identities, v, Source::Other, true, MAX_PRIORITY);
        }
        learn(&mut identities, &sanitize_utf8(bytes));
        if identities.priority_dropped != 0 {
            if !notice.is_empty() {
                notice.push(' ');
            }
            notice.push_str(
                "More identity values were found in login lines than the lists hold (at most 64 per log and 384 in all), \
                 so a handle or name that comes later may not be hidden outside its own line.",
            );
        }
        self.session = identities.clone();
        self.scanned = true;
        self.cleaner = None;
        let redactor = Redactor::new(&identities);

        let mut matched: Vec<Vec<u8>> = Vec::new();
        while pos < bytes.len() {
            let nl = find_byte(&bytes[pos..], b'\n').map(|p| p + pos);
            let end = nl.unwrap_or(bytes.len());
            let mut line = bytes[pos..end].to_vec();
            pos = end + 1;
            while line.last() == Some(&b'\r') {
                line.pop();
            }
            // Bound the work per line: a hostile line cannot make redaction slow. The final clip comes after
            // redaction.
            if line.len() > MAX_REDACT_LINE {
                line = clip_utf8(&line, MAX_REDACT_LINE);
            }
            let line = redactor.apply(&sanitize_utf8(&line));
            // The filter runs on the redacted text, so it cannot be used to probe for an account name.
            if !needle.is_empty() && !contains_sub(&line.to_ascii_lowercase(), &needle) {
                continue;
            }
            matched.push(line);
        }

        let matched_count = matched.len();
        let want_n = want as usize;
        let mut first = matched_count.saturating_sub(want_n);
        let mut truncated = read.skipped_older || first > 0;
        // 64 KiB of text in total (each line counts its newline): the oldest lines go first.
        let mut used = 0usize;
        let mut keep_from = matched_count;
        while keep_from > first {
            let line = &mut matched[keep_from - 1];
            if line.len() + 1 > MAX_LOG_TEXT {
                *line = clip_utf8(line, MAX_LOG_TEXT - 1);
                truncated = true;
            }
            if used + line.len() + 1 > MAX_LOG_TEXT {
                break;
            }
            used += line.len() + 1;
            keep_from -= 1;
        }
        if keep_from > first {
            first = keep_from;
            truncated = true;
        }

        let mut lines = Value::array();
        for l in &matched[first..matched_count] {
            lines.push(Value::str(l.clone()));
        }
        let mut o = Value::object();
        o.set("log", Value::str(log.clone()));
        // with what was learned: a handle in a folder name too
        o.set(
            "path",
            Value::str(redactor.apply(&sanitize_utf8(&read.path))),
        );
        o.set("exists", Value::bool(read.exists));
        o.set("lines", lines);
        o.set("matched_lines", Value::int(matched_count as i64));
        o.set("truncated", Value::bool(truncated));
        o.set("identity_scan_complete", Value::bool(notice.is_empty()));
        o.set("notice", Value::str(notice));
        ok_result(o)
    }

    fn get_settings(&mut self) -> ToolResult {
        let ini = self.platform.settings();
        if !ini.error.is_empty() {
            let shown = self.clean(&ini.error);
            return failure(cat(&[b"Could not read sc-offline.ini: ", &shown]));
        }
        let mut list = Value::array();
        let mut cut = false;
        let mut section: Vec<u8> = Vec::new();
        for line in ini_lines(&ini.text) {
            if line.is_empty() || line[0] == b'#' || line[0] == b';' {
                continue;
            }
            if line[0] == b'[' {
                section = match find_byte(&line, b']') {
                    Some(close) => trim_space(&line[1..close.max(1)]).to_vec(),
                    None => trim_space(&line[1..]).to_vec(),
                };
                continue;
            }
            let Some(eq) = find_byte(&line, b'=') else {
                continue;
            };
            if list.items().len() >= MAX_SETTINGS {
                cut = true;
                break;
            }
            let mut s = Value::object();
            let cs = self.clean(&section);
            s.set("section", Value::str(clip_utf8(&cs, MAX_FIELD_BYTES)));
            let key = trim_space(&line[..eq]).to_vec();
            let ck = self.clean(&key);
            s.set("key", Value::str(clip_utf8(&ck, MAX_FIELD_BYTES)));
            // The value is redacted together with its key, so an identity key (nickname = ..., geid = ...)
            // hides its value as it would in a log line; the key part is then cut off again.
            let rhs = trim_space(&line[eq + 1..]).to_vec();
            let joined = self.clean(&cat(&[&key, b"=", &rhs]));
            let cut_at = find_byte(&joined, b'=');
            let alone = self.clean(&rhs);
            let with_key = match cut_at {
                None => joined.clone(),
                Some(c) => joined[c + 1..].to_vec(),
            };
            // Whichever form hid more wins (an e-mail or path is caught either way).
            let chosen = if find_byte(&with_key, b'<').is_some() {
                with_key
            } else {
                alone
            };
            s.set("value", Value::str(clip_utf8(&chosen, MAX_SETTING_BYTES)));
            list.push(s);
        }
        let mut o = Value::object();
        let path = self.clean(&ini.path);
        o.set("path", Value::str(path));
        o.set("exists", Value::bool(ini.exists));
        o.set("truncated", Value::bool(cut));
        o.set("settings", list);
        ok_result(o)
    }
}

fn sanitize_for_scanner(line: &[u8]) -> Vec<u8> {
    sanitize_utf8(line)
}

fn contains_sub(hay: &[u8], needle: &[u8]) -> bool {
    needle.is_empty() || hay.windows(needle.len()).any(|w| w == needle)
}

fn supported(v: &[u8]) -> bool {
    matches!(
        v,
        b"2025-11-25" | b"2025-06-18" | b"2025-03-26" | b"2024-11-05"
    )
}

fn envelope(id: &Value) -> Value {
    let mut v = Value::object();
    v.set("jsonrpc", Value::str("2.0"));
    v.set("id", id.clone());
    v
}

fn error_line(id: &Value, code: i64, message: &[u8]) -> Vec<u8> {
    let mut v = envelope(id);
    let mut e = Value::object();
    e.set("code", Value::int(code));
    e.set("message", Value::str(message.to_vec()));
    v.set("error", e);
    serialize(&v)
}

fn result_line(id: &Value, result: Value) -> Vec<u8> {
    let mut v = envelope(id);
    v.set("result", result);
    serialize(&v)
}

fn parse_or_null(json: &str) -> Value {
    let r = parse(json.as_bytes());
    if r.ok {
        r.value
    } else {
        Value::null()
    }
}

fn list_tools() -> Value {
    let mut list = Value::array();
    for t in &TOOLS {
        let mut tool = Value::object();
        let mut ann = Value::object();
        ann.set("readOnlyHint", Value::bool(true));
        ann.set("destructiveHint", Value::bool(false));
        ann.set("idempotentHint", Value::bool(true));
        ann.set("openWorldHint", Value::bool(false));
        tool.set("name", Value::str(t.name));
        tool.set("title", Value::str(t.title));
        tool.set("description", Value::str(t.description));
        tool.set("inputSchema", parse_or_null(t.input_schema));
        tool.set("outputSchema", parse_or_null(t.output_schema));
        tool.set("annotations", ann);
        list.push(tool);
    }
    let mut result = Value::object();
    result.set("tools", list);
    result
}

/// Top-level `key = value` lines of a plugin.ini (a `[section]` ends the part that describes the plugin).
fn plugin_fields(ini: &[u8]) -> Vec<(Vec<u8>, Vec<u8>)> {
    let mut fields = Vec::new();
    for line in ini_lines(ini) {
        if line.is_empty() || line[0] == b';' || line[0] == b'#' {
            continue;
        }
        if line[0] == b'[' {
            break;
        }
        let Some(eq) = find_byte(&line, b'=') else {
            continue;
        };
        fields.push((
            trim_space(&line[..eq]).to_ascii_lowercase(),
            trim_space(&line[eq + 1..]).to_vec(),
        ));
    }
    fields
}

/// Reads request lines with `read` (returns 0 at the end of input), answers each through `write` (one line, the
/// newline is the writer's to add; false stops the loop). A line over `MAX_LINE` is dropped unbuffered and
/// answered with -32600. Returns at the end of input.
pub fn serve(
    server: &mut Server,
    read: &mut dyn FnMut(&mut [u8]) -> usize,
    write: &mut dyn FnMut(&[u8]) -> bool,
) {
    let mut line: Vec<u8> = Vec::new();
    let mut over = false;
    let mut chunk = vec![0u8; 65536];
    let finish = |server: &mut Server,
                  line: &mut Vec<u8>,
                  over: &mut bool,
                  write: &mut dyn FnMut(&[u8]) -> bool|
     -> bool {
        let answer = if *over {
            Some(server.oversize_reply())
        } else {
            server.handle_line(std::mem::take(line))
        };
        line.clear();
        *over = false;
        match answer {
            None => true,
            Some(reply) => write(&reply),
        }
    };
    loop {
        let got = read(&mut chunk);
        if got == 0 {
            break;
        }
        let mut at = 0;
        while at < got {
            let nl = find_byte(&chunk[at..got], b'\n').map(|p| p + at);
            let end = nl.unwrap_or(got);
            if !over {
                line.extend_from_slice(&chunk[at..end]);
                if line.len() > MAX_LINE {
                    over = true;
                    line = Vec::new();
                }
            }
            at = end;
            if nl.is_some() {
                at += 1;
                if !finish(server, &mut line, &mut over, write) {
                    return;
                }
            }
        }
    }
    if over || !line.is_empty() {
        finish(server, &mut line, &mut over, write); // a last line without a newline
    }
}
