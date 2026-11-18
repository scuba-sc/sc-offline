// SPDX-License-Identifier: GPL-3.0-only
//! In-place edits of `sc-offline.ini`: drop a key, set a key. Ports of `DropIniKey` and
//! `SetIniValue` in `launcher/launcher.cpp`. Every line that is not touched stays byte for byte,
//! including its line ending; the editors work on bytes and never re-encode the file.

use std::io::ErrorKind;
use std::path::Path;

/// The lines of `text`, each with its line ending.
fn lines(text: &[u8]) -> impl Iterator<Item = &[u8]> {
    let mut pos = 0usize;
    std::iter::from_fn(move || {
        if pos >= text.len() {
            return None;
        }
        let end = text[pos..]
            .iter()
            .position(|&b| b == b'\n')
            .map(|i| pos + i + 1)
            .unwrap_or(text.len());
        let line = &text[pos..end];
        pos = end;
        Some(line)
    })
}

fn is_blank(b: u8) -> bool {
    b == b' ' || b == b'\t'
}

/// True when `line` (without its ending) is an uncommented `key = ...` line for `key`: optional
/// blanks, the key without regard to ASCII case, optional blanks, then `=`.
fn assigns(line: &[u8], key: &str) -> bool {
    let key = key.as_bytes();
    let Some(start) = line.iter().position(|&b| !is_blank(b)) else {
        return false;
    };
    let rest = &line[start..];
    if rest.len() < key.len() || !rest[..key.len()].eq_ignore_ascii_case(key) {
        return false;
    }
    let after = &rest[key.len()..];
    let skip = after.iter().take_while(|&&b| is_blank(b)).count();
    after.get(skip) == Some(&b'=')
}

/// Removes every uncommented `key = ...` line.
pub fn drop_key(text: &[u8], key: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(text.len());
    for line in lines(text) {
        if !assigns(line, key) {
            out.extend_from_slice(line);
        }
    }
    out
}

/// Sets `key = value`: every uncommented line for the key gets the value (the reader keeps the
/// last one, so a duplicate must not outvote it) with its line ending kept, or one line is
/// appended after a blank line.
pub fn set_value(text: &[u8], key: &str, value: &str) -> Vec<u8> {
    let new_line = format!("{key} = {value}");
    let mut out = Vec::with_capacity(text.len() + new_line.len() + 4);
    let mut found = false;
    for line in lines(text) {
        let mut body = line.len();
        if body > 0 && line[body - 1] == b'\n' {
            body -= 1;
        }
        if body > 0 && line[body - 1] == b'\r' {
            body -= 1;
        }
        if assigns(&line[..body], key) {
            out.extend_from_slice(new_line.as_bytes());
            out.extend_from_slice(&line[body..]);
            found = true;
        } else {
            out.extend_from_slice(line);
        }
    }
    if found {
        return out;
    }
    if out.last().is_some_and(|&b| b != b'\n') {
        out.extend_from_slice(b"\r\n");
    }
    out.extend_from_slice(b"\r\n");
    out.extend_from_slice(new_line.as_bytes());
    out.extend_from_slice(b"\r\n");
    out
}

/// File version of [`drop_key`]. False when the file cannot be read or written.
pub fn drop_ini_key(path: &Path, key: &str) -> bool {
    match std::fs::read(path) {
        Ok(text) => std::fs::write(path, drop_key(&text, key)).is_ok(),
        Err(_) => false,
    }
}

/// File version of [`set_value`]. A file that exists but cannot be read is left alone and the
/// call fails; a missing file is created.
pub fn set_ini_value(path: &Path, key: &str, value: &str) -> bool {
    let text = match std::fs::read(path) {
        Ok(t) => t,
        Err(e) if e.kind() == ErrorKind::NotFound => Vec::new(),
        Err(_) => return false,
    };
    std::fs::write(path, set_value(&text, key, value)).is_ok()
}
