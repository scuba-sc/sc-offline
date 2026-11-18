// SPDX-License-Identifier: GPL-3.0-only
//! The JSON model of the MCP server: a strict RFC 8259 parser, a one-line serializer and the small schema
//! check. Port of the JSON part of `launcher/mcp_server.h`.
//!
//! This is written by hand, not with serde_json, for three reasons the protocol tests depend on: a number
//! keeps its token (so an id is echoed exactly), an object keeps insertion order and replaces a duplicate
//! key in place, and the parser's error sentences and byte offsets are part of the replies. Text is bytes,
//! as in the C++: a string is made valid UTF-8 only when it is serialized.

/// Longest accepted request line.
pub const MAX_LINE: usize = 1 << 20;
/// Deepest JSON nesting accepted.
pub const MAX_DEPTH: i32 = 64;
/// Most members one JSON object may hold.
pub const MAX_MEMBERS: usize = 1024;
/// Most JSON values one message may hold.
pub const MAX_VALUES: usize = 100_000;

/// The type of a [`Value`].
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

/// One JSON value.
#[derive(Clone, Debug)]
pub struct Value {
    ty: Type,
    b: bool,
    num: f64,
    /// String content, or a number's token.
    text: Vec<u8>,
    items: Vec<Value>,
    members: Vec<(Vec<u8>, Value)>,
}

impl Default for Value {
    fn default() -> Self {
        Value {
            ty: Type::Null,
            b: false,
            num: 0.0,
            text: Vec::new(),
            items: Vec::new(),
            members: Vec::new(),
        }
    }
}

impl Value {
    pub fn null() -> Value {
        Value::default()
    }
    pub fn bool(b: bool) -> Value {
        Value {
            ty: Type::Bool,
            b,
            ..Value::default()
        }
    }
    pub fn int(n: i64) -> Value {
        Value {
            ty: Type::Number,
            num: n as f64,
            text: n.to_string().into_bytes(),
            ..Value::default()
        }
    }
    pub fn str(s: impl Into<Vec<u8>>) -> Value {
        Value {
            ty: Type::String,
            text: s.into(),
            ..Value::default()
        }
    }
    pub fn array() -> Value {
        Value {
            ty: Type::Array,
            ..Value::default()
        }
    }
    pub fn object() -> Value {
        Value {
            ty: Type::Object,
            ..Value::default()
        }
    }
    /// A number as the parser read it: the token is kept so an id is echoed exactly.
    pub fn number(d: f64, raw: Vec<u8>) -> Value {
        Value {
            ty: Type::Number,
            num: d,
            text: raw,
            ..Value::default()
        }
    }

    pub fn ty(&self) -> Type {
        self.ty
    }
    pub fn is_null(&self) -> bool {
        self.ty == Type::Null
    }
    pub fn is_bool(&self) -> bool {
        self.ty == Type::Bool
    }
    pub fn is_number(&self) -> bool {
        self.ty == Type::Number
    }
    pub fn is_string(&self) -> bool {
        self.ty == Type::String
    }
    pub fn is_array(&self) -> bool {
        self.ty == Type::Array
    }
    pub fn is_object(&self) -> bool {
        self.ty == Type::Object
    }

    pub fn as_bool(&self) -> bool {
        self.b
    }
    pub fn as_f64(&self) -> f64 {
        self.num
    }
    /// String content, or a number's token.
    pub fn as_bytes(&self) -> &[u8] {
        &self.text
    }
    pub fn items(&self) -> &[Value] {
        &self.items
    }
    pub fn members(&self) -> &[(Vec<u8>, Value)] {
        &self.members
    }

    /// An integral number within +-2^53.
    pub fn as_integer(&self) -> Option<i64> {
        if self.ty != Type::Number
            || !self.num.is_finite()
            || self.num != self.num.floor()
            || self.num.abs() > 9_007_199_254_740_992.0
        {
            return None;
        }
        Some(self.num as i64)
    }

    pub fn push(&mut self, v: Value) -> &mut Value {
        self.items.push(v);
        self
    }

    /// Replaces an existing key (the last duplicate wins), else appends: members keep insertion order.
    pub fn set(&mut self, key: impl Into<Vec<u8>>, v: Value) -> &mut Value {
        let key = key.into();
        for m in &mut self.members {
            if m.0 == key {
                m.1 = v;
                return self;
            }
        }
        self.members.push((key, v));
        self
    }

    pub fn find(&self, key: &[u8]) -> Option<&Value> {
        self.members.iter().find(|m| m.0 == key).map(|m| &m.1)
    }
}

/// Length of the well-formed UTF-8 sequence at `s[i]` (no overlong forms, surrogates or > U+10FFFF), else 0.
pub fn utf8_sequence_length(s: &[u8], i: usize) -> usize {
    let b = |k: usize| -> u32 { s.get(i + k).map_or(0, |&c| c as u32) };
    let cont = |k: usize| (b(k) & 0xC0) == 0x80;
    let c = b(0);
    if c < 0x80 {
        return 1;
    }
    if (0xC2..=0xDF).contains(&c) {
        return if cont(1) { 2 } else { 0 };
    }
    if (0xE0..=0xEF).contains(&c) {
        if !cont(1) || !cont(2) {
            return 0;
        }
        if c == 0xE0 && b(1) < 0xA0 {
            return 0;
        }
        if c == 0xED && b(1) > 0x9F {
            return 0;
        }
        return 3;
    }
    if (0xF0..=0xF4).contains(&c) {
        if !cont(1) || !cont(2) || !cont(3) {
            return 0;
        }
        if c == 0xF0 && b(1) < 0x90 {
            return 0;
        }
        if c == 0xF4 && b(1) > 0x8F {
            return 0;
        }
        return 4;
    }
    0
}

/// Replaces every invalid byte with U+FFFD so the text is valid UTF-8 (and so valid inside JSON).
pub fn sanitize_utf8(s: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        let n = utf8_sequence_length(s, i);
        if n == 0 {
            out.extend_from_slice(b"\xEF\xBF\xBD");
            i += 1;
        } else {
            out.extend_from_slice(&s[i..i + n]);
            i += n;
        }
    }
    out
}

/// The longest prefix of at most `max` bytes that does not cut a UTF-8 sequence (`s` is valid UTF-8).
pub fn clip_utf8(s: &[u8], max: usize) -> Vec<u8> {
    if s.len() <= max {
        return s.to_vec();
    }
    let mut n = max;
    while n > 0 && (s[n] & 0xC0) == 0x80 {
        n -= 1;
    }
    s[..n].to_vec()
}

/// Number of code points of a valid UTF-8 text.
pub fn count_code_points(s: &[u8]) -> usize {
    s.iter().filter(|&&c| (c & 0xC0) != 0x80).count()
}

/// The result of [`parse`].
pub struct ParseResult {
    pub ok: bool,
    pub value: Value,
    /// Set when not `ok`.
    pub error: String,
}

/// Strict RFC 8259: one value and nothing after it, UTF-8 text, `\uXXXX` escapes with paired surrogates, no
/// leading zeros, nesting at most `MAX_DEPTH`, at most `MAX_MEMBERS` members per object and `MAX_VALUES`
/// values.
pub fn parse(text: &[u8]) -> ParseResult {
    let mut p = Parser {
        s: text,
        i: 0,
        error: String::new(),
        values: 0,
    };
    let mut r = ParseResult {
        ok: false,
        value: Value::null(),
        error: String::new(),
    };
    p.ws();
    let mut v = Value::null();
    if !p.parse_value(&mut v, 0) {
        r.error = p.error;
        return r;
    }
    p.ws();
    if p.i != text.len() {
        r.error = p.describe("unexpected text after the JSON value");
        return r;
    }
    r.ok = true;
    r.value = v;
    r
}

struct Parser<'a> {
    s: &'a [u8],
    i: usize,
    error: String,
    /// Values parsed so far in this message.
    values: usize,
}

impl Parser<'_> {
    fn describe(&self, what: &str) -> String {
        format!("{what} at byte {}", self.i)
    }
    fn fail(&mut self, what: &str) -> bool {
        self.error = self.describe(what);
        false
    }
    fn ws(&mut self) {
        while self.i < self.s.len() && matches!(self.s[self.i], b' ' | b'\t' | b'\n' | b'\r') {
            self.i += 1;
        }
    }
    fn literal(&mut self, word: &[u8]) -> bool {
        if self.s.len() >= self.i && self.s[self.i..].starts_with(word) {
            self.i += word.len();
            true
        } else {
            false
        }
    }

    fn parse_value(&mut self, out: &mut Value, depth: i32) -> bool {
        if self.i >= self.s.len() {
            return self.fail("unexpected end of input");
        }
        self.values += 1;
        if self.values > MAX_VALUES {
            return self.fail("too many values");
        }
        let c = self.s[self.i];
        match c {
            b'{' => self.parse_object(out, depth + 1),
            b'[' => self.parse_array(out, depth + 1),
            b'"' => {
                let mut t = Vec::new();
                if !self.parse_string(&mut t) {
                    return false;
                }
                *out = Value::str(t);
                true
            }
            b't' => {
                if !self.literal(b"true") {
                    return self.fail("invalid literal");
                }
                *out = Value::bool(true);
                true
            }
            b'f' => {
                if !self.literal(b"false") {
                    return self.fail("invalid literal");
                }
                *out = Value::bool(false);
                true
            }
            b'n' => {
                if !self.literal(b"null") {
                    return self.fail("invalid literal");
                }
                *out = Value::null();
                true
            }
            b'-' | b'0'..=b'9' => self.parse_number(out),
            _ => self.fail("unexpected character"),
        }
    }

    fn digit(&self) -> bool {
        self.i < self.s.len() && self.s[self.i].is_ascii_digit()
    }

    fn parse_number(&mut self, out: &mut Value) -> bool {
        let start = self.i;
        if self.s[self.i] == b'-' {
            self.i += 1;
        }
        if !self.digit() {
            return self.fail("invalid number");
        }
        if self.s[self.i] == b'0' {
            self.i += 1;
        } else {
            while self.digit() {
                self.i += 1;
            }
        }
        if self.i < self.s.len() && self.s[self.i] == b'.' {
            self.i += 1;
            if !self.digit() {
                return self.fail("invalid number");
            }
            while self.digit() {
                self.i += 1;
            }
        }
        if self.i < self.s.len() && (self.s[self.i] == b'e' || self.s[self.i] == b'E') {
            self.i += 1;
            if self.i < self.s.len() && (self.s[self.i] == b'+' || self.s[self.i] == b'-') {
                self.i += 1;
            }
            if !self.digit() {
                return self.fail("invalid number");
            }
            while self.digit() {
                self.i += 1;
            }
        }
        let raw = self.s[start..self.i].to_vec();
        // The token matched the JSON number grammar, so it is also valid Rust float syntax.
        let d = std::str::from_utf8(&raw)
            .ok()
            .and_then(|t| t.parse::<f64>().ok())
            .unwrap_or(0.0);
        *out = Value::number(d, raw);
        true
    }

    fn hex4(&mut self) -> Option<u32> {
        if self.i + 4 > self.s.len() {
            self.fail("bad \\u escape");
            return None;
        }
        let mut v = 0u32;
        for k in 0..4 {
            let c = self.s[self.i + k];
            let d = match c {
                b'0'..=b'9' => (c - b'0') as u32,
                b'a'..=b'f' => (c - b'a' + 10) as u32,
                b'A'..=b'F' => (c - b'A' + 10) as u32,
                _ => {
                    self.fail("bad \\u escape");
                    return None;
                }
            };
            v = v * 16 + d;
        }
        self.i += 4;
        Some(v)
    }

    fn append_utf8(out: &mut Vec<u8>, cp: u32) {
        if cp < 0x80 {
            out.push(cp as u8);
        } else if cp < 0x800 {
            out.push((0xC0 | (cp >> 6)) as u8);
            out.push((0x80 | (cp & 0x3F)) as u8);
        } else if cp < 0x10000 {
            out.push((0xE0 | (cp >> 12)) as u8);
            out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
            out.push((0x80 | (cp & 0x3F)) as u8);
        } else {
            out.push((0xF0 | (cp >> 18)) as u8);
            out.push((0x80 | ((cp >> 12) & 0x3F)) as u8);
            out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
            out.push((0x80 | (cp & 0x3F)) as u8);
        }
    }

    fn parse_string(&mut self, out: &mut Vec<u8>) -> bool {
        self.i += 1; // opening quote
        loop {
            if self.i >= self.s.len() {
                return self.fail("unterminated string");
            }
            let c = self.s[self.i];
            if c == b'"' {
                self.i += 1;
                return true;
            }
            if c < 0x20 {
                return self.fail("control character in string");
            }
            if c != b'\\' {
                let n = utf8_sequence_length(self.s, self.i);
                if n == 0 {
                    return self.fail("invalid UTF-8");
                }
                out.extend_from_slice(&self.s[self.i..self.i + n]);
                self.i += n;
                continue;
            }
            self.i += 1;
            if self.i >= self.s.len() {
                return self.fail("unterminated string");
            }
            let e = self.s[self.i];
            self.i += 1;
            match e {
                b'"' => out.push(b'"'),
                b'\\' => out.push(b'\\'),
                b'/' => out.push(b'/'),
                b'b' => out.push(8),
                b'f' => out.push(12),
                b'n' => out.push(b'\n'),
                b'r' => out.push(b'\r'),
                b't' => out.push(b'\t'),
                b'u' => {
                    let Some(mut cp) = self.hex4() else {
                        return false;
                    };
                    if (0xDC00..=0xDFFF).contains(&cp) {
                        return self.fail("lone low surrogate");
                    }
                    if (0xD800..=0xDBFF).contains(&cp) {
                        if self.i + 2 > self.s.len()
                            || self.s[self.i] != b'\\'
                            || self.s[self.i + 1] != b'u'
                        {
                            return self.fail("lone high surrogate");
                        }
                        self.i += 2;
                        let Some(lo) = self.hex4() else { return false };
                        if !(0xDC00..=0xDFFF).contains(&lo) {
                            return self.fail("lone high surrogate");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    Self::append_utf8(out, cp);
                }
                _ => {
                    self.i -= 1;
                    return self.fail("bad escape");
                }
            }
        }
    }

    fn parse_array(&mut self, out: &mut Value, depth: i32) -> bool {
        if depth > MAX_DEPTH {
            return self.fail("nesting too deep");
        }
        self.i += 1;
        *out = Value::array();
        self.ws();
        if self.i < self.s.len() && self.s[self.i] == b']' {
            self.i += 1;
            return true;
        }
        loop {
            let mut item = Value::null();
            self.ws();
            if !self.parse_value(&mut item, depth) {
                return false;
            }
            out.push(item);
            self.ws();
            if self.i >= self.s.len() {
                return self.fail("unterminated array");
            }
            if self.s[self.i] == b',' {
                self.i += 1;
                continue;
            }
            if self.s[self.i] == b']' {
                self.i += 1;
                return true;
            }
            return self.fail("expected , or ] in array");
        }
    }

    fn parse_object(&mut self, out: &mut Value, depth: i32) -> bool {
        if depth > MAX_DEPTH {
            return self.fail("nesting too deep");
        }
        self.i += 1;
        *out = Value::object();
        self.ws();
        if self.i < self.s.len() && self.s[self.i] == b'}' {
            self.i += 1;
            return true;
        }
        let mut members = 0usize;
        loop {
            self.ws();
            if self.i >= self.s.len() || self.s[self.i] != b'"' {
                return self.fail("expected a string key");
            }
            members += 1;
            if members > MAX_MEMBERS {
                return self.fail("too many members");
            }
            let mut key = Vec::new();
            if !self.parse_string(&mut key) {
                return false;
            }
            self.ws();
            if self.i >= self.s.len() || self.s[self.i] != b':' {
                return self.fail("expected : after key");
            }
            self.i += 1;
            self.ws();
            let mut v = Value::null();
            if !self.parse_value(&mut v, depth) {
                return false;
            }
            out.set(key, v);
            self.ws();
            if self.i >= self.s.len() {
                return self.fail("unterminated object");
            }
            if self.s[self.i] == b',' {
                self.i += 1;
                continue;
            }
            if self.s[self.i] == b'}' {
                self.i += 1;
                return true;
            }
            return self.fail("expected , or } in object");
        }
    }
}

/// Serializes one value onto `out`: control characters are escaped, UTF-8 is kept as is.
pub fn serialize_to(v: &Value, out: &mut Vec<u8>) {
    match v.ty() {
        Type::Null => out.extend_from_slice(b"null"),
        Type::Bool => out.extend_from_slice(if v.as_bool() { b"true" } else { b"false" }),
        Type::Number => out.extend_from_slice(v.as_bytes()),
        Type::String => {
            const HEX: &[u8; 16] = b"0123456789abcdef";
            out.push(b'"');
            for c in sanitize_utf8(v.as_bytes()) {
                match c {
                    b'"' => out.extend_from_slice(b"\\\""),
                    b'\\' => out.extend_from_slice(b"\\\\"),
                    8 => out.extend_from_slice(b"\\b"),
                    12 => out.extend_from_slice(b"\\f"),
                    b'\n' => out.extend_from_slice(b"\\n"),
                    b'\r' => out.extend_from_slice(b"\\r"),
                    b'\t' => out.extend_from_slice(b"\\t"),
                    c if c < 0x20 => {
                        out.extend_from_slice(b"\\u00");
                        out.push(HEX[(c >> 4) as usize]);
                        out.push(HEX[(c & 15) as usize]);
                    }
                    c => out.push(c),
                }
            }
            out.push(b'"');
        }
        Type::Array => {
            out.push(b'[');
            let mut first = true;
            for item in v.items() {
                if !first {
                    out.push(b',');
                }
                first = false;
                serialize_to(item, out);
            }
            out.push(b']');
        }
        Type::Object => {
            out.push(b'{');
            let mut first = true;
            for (k, m) in v.members() {
                if !first {
                    out.push(b',');
                }
                first = false;
                serialize_to(&Value::str(k.clone()), out);
                out.push(b':');
                serialize_to(m, out);
            }
            out.push(b'}');
        }
    }
}

/// One line: control characters are escaped, UTF-8 is kept as is.
pub fn serialize(v: &Value) -> Vec<u8> {
    let mut s = Vec::new();
    serialize_to(v, &mut s);
    s
}

fn cat(parts: &[&[u8]]) -> Vec<u8> {
    parts.concat()
}

/// The subset of JSON Schema the tool schemas use: type (object, string, integer, boolean, array), properties,
/// required, additionalProperties:false, enum, minimum, maximum, minLength, maxLength, items. Returns an empty
/// vector when the value fits, else a sentence naming the problem and where (`path`, for example `arguments`).
pub fn check_schema(schema: &Value, v: &Value, path: &[u8]) -> Vec<u8> {
    let ty = schema.find(b"type");
    let t: &[u8] = match ty {
        Some(t) if t.is_string() => t.as_bytes(),
        _ => b"",
    };
    if t == b"object" {
        if !v.is_object() {
            return cat(&[path, b" must be an object"]);
        }
        let props = schema.find(b"properties");
        for (name, value) in v.members() {
            let sub = props.and_then(|p| p.find(name));
            let Some(sub) = sub else {
                if let Some(extra) = schema.find(b"additionalProperties") {
                    if extra.is_bool() && !extra.as_bool() {
                        return cat(&[path, b" has an unknown property '", name, b"'"]);
                    }
                }
                continue;
            };
            let e = check_schema(sub, value, &cat(&[path, b".", name]));
            if !e.is_empty() {
                return e;
            }
        }
        if let Some(req) = schema.find(b"required") {
            for name in req.items() {
                if v.find(name.as_bytes()).is_none() {
                    return cat(&[
                        path,
                        b" is missing the required property '",
                        name.as_bytes(),
                        b"'",
                    ]);
                }
            }
        }
        return Vec::new();
    }
    if t == b"string" {
        if !v.is_string() {
            return cat(&[path, b" must be a string"]);
        }
        if let Some(en) = schema.find(b"enum") {
            let mut hit = false;
            let mut list: Vec<u8> = Vec::new();
            for item in en.items() {
                hit = hit || item.as_bytes() == v.as_bytes();
                if !list.is_empty() {
                    list.extend_from_slice(b", ");
                }
                list.extend_from_slice(item.as_bytes());
            }
            if !hit {
                return cat(&[path, b" must be one of: ", &list]);
            }
        }
        if let Some(n) = schema.find(b"minLength").and_then(|m| m.as_integer()) {
            if (count_code_points(v.as_bytes()) as i64) < n {
                return cat(&[
                    path,
                    b" must be at least ",
                    n.to_string().as_bytes(),
                    b" characters",
                ]);
            }
        }
        if let Some(n) = schema.find(b"maxLength").and_then(|m| m.as_integer()) {
            if (count_code_points(v.as_bytes()) as i64) > n {
                return cat(&[
                    path,
                    b" must be at most ",
                    n.to_string().as_bytes(),
                    b" characters",
                ]);
            }
        }
        return Vec::new();
    }
    if t == b"integer" {
        let Some(n) = v.as_integer() else {
            return cat(&[path, b" must be an integer"]);
        };
        if let Some(bound) = schema.find(b"minimum").and_then(|m| m.as_integer()) {
            if n < bound {
                return cat(&[path, b" must be at least ", bound.to_string().as_bytes()]);
            }
        }
        if let Some(bound) = schema.find(b"maximum").and_then(|m| m.as_integer()) {
            if n > bound {
                return cat(&[path, b" must be at most ", bound.to_string().as_bytes()]);
            }
        }
        return Vec::new();
    }
    if t == b"boolean" {
        return if v.is_bool() {
            Vec::new()
        } else {
            cat(&[path, b" must be true or false"])
        };
    }
    if t == b"array" {
        if !v.is_array() {
            return cat(&[path, b" must be an array"]);
        }
        if let Some(items) = schema.find(b"items") {
            for (k, item) in v.items().iter().enumerate() {
                let e = check_schema(
                    items,
                    item,
                    &cat(&[path, b"[", k.to_string().as_bytes(), b"]"]),
                );
                if !e.is_empty() {
                    return e;
                }
            }
        }
        return Vec::new();
    }
    Vec::new()
}
