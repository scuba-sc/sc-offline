// SPDX-License-Identifier: GPL-3.0-only
//! Finding the game, in the order of `DetectBin64` of the C++ launcher: the folder remembered from
//! the last run (`data\game-path.txt`), the RSI Launcher's uninstall entry, the paths in
//! `%APPDATA%\rsilauncher`, the usual folders on every fixed drive, and only when none of those found
//! anything, a bounded search of every fixed drive. Every candidate is accepted only when
//! `<channel>\Bin64\StarCitizen.exe` exists.
//!
//! Read-only. The C++ launcher's folder picker is not here: `status` never opens it.

use crate::identity::{add_found, Found};
use crate::paths::{strip_slashes, to_bin64, GAME_EXE};
use crate::win::{from_wide, is_file, read_all, wide, wlen};
use scol_config::config::trim;
use std::io::Write;
use std::os::windows::fs::MetadataExt;
use windows_sys::Win32::Foundation::ERROR_SUCCESS;
use windows_sys::Win32::Storage::FileSystem::{
    GetDriveTypeW, GetLogicalDrives, FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_REPARSE_POINT,
};
use windows_sys::Win32::System::Registry::{
    RegCloseKey, RegEnumKeyExW, RegGetValueW, RegOpenKeyExW, HKEY, HKEY_CURRENT_USER,
    HKEY_LOCAL_MACHINE, KEY_READ, RRF_RT_REG_SZ,
};

/// `DRIVE_FIXED`.
const DRIVE_FIXED: u32 = 3;

fn remembered_path_file(here: &str) -> String {
    format!("{here}\\data\\game-path.txt")
}

fn from_remembered(here: &str, channel: &str, out: &mut Vec<Found>) {
    let Some(bytes) = read_all(&remembered_path_file(here)) else {
        return;
    };
    let text = String::from_utf8_lossy(&bytes);
    add_found(
        out,
        &to_bin64(trim(&text), channel),
        "remembered from your last run",
    );
}

/// `ResolveUpward`: a path the RSI Launcher wrote may point anywhere inside the install. Walk up a few
/// levels and take the first that resolves.
fn resolve_upward(p: &str, channel: &str) -> String {
    let mut p = strip_slashes(p);
    for _ in 0..6 {
        if wlen(&p) <= 3 {
            break;
        }
        let bin = to_bin64(&p, channel);
        if !bin.is_empty() {
            return bin;
        }
        let Some(cut) = p.rfind(['\\', '/']) else {
            break;
        };
        if wlen(&p[..cut]) < 2 {
            break;
        }
        p.truncate(cut);
    }
    String::new()
}

/// Case-insensitive substring, as `StrStrIW` for the ASCII names involved.
fn contains_nocase(hay: &str, needle: &str) -> bool {
    hay.to_ascii_lowercase()
        .contains(&needle.to_ascii_lowercase())
}

/// HKLM and HKCU `...\Uninstall\*`: the RSI Launcher's InstallLocation is usually
/// `<library>\RSI Launcher`, and the game sits beside it in `<library>\StarCitizen`.
fn from_uninstall_keys(channel: &str, out: &mut Vec<Found>) {
    const KEYS: [(HKEY, &str); 3] = [
        (
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        ),
        (
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        ),
        (
            HKEY_CURRENT_USER,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        ),
    ];
    for (root, path) in KEYS {
        let path_w = wide(path);
        let mut h: HKEY = std::ptr::null_mut();
        // SAFETY: NUL-terminated path; h receives the key.
        if unsafe { RegOpenKeyExW(root, path_w.as_ptr(), 0, KEY_READ, &mut h) } != ERROR_SUCCESS {
            continue;
        }
        let mut index = 0u32;
        loop {
            let mut sub = [0u16; 256];
            let mut n = sub.len() as u32;
            // SAFETY: sub is valid for n units.
            let rc = unsafe {
                RegEnumKeyExW(
                    h,
                    index,
                    sub.as_mut_ptr(),
                    &mut n,
                    std::ptr::null(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            };
            if rc != ERROR_SUCCESS {
                break;
            }
            index += 1;
            let name = reg_string(h, &sub, "DisplayName", 256);
            let Some(name) = name else { continue };
            if !contains_nocase(&name, "RSI Launcher") && !contains_nocase(&name, "Star Citizen") {
                continue;
            }
            let Some(loc) = reg_string(h, &sub, "InstallLocation", 520) else {
                continue;
            };
            let l = strip_slashes(trim(&loc));
            if l.is_empty() {
                continue;
            }
            let mut bin = to_bin64(&l, channel);
            if bin.is_empty() {
                if let Some(cut) = l.rfind(['\\', '/']) {
                    bin = to_bin64(&l[..cut], channel);
                }
            }
            add_found(out, &bin, "from the RSI Launcher's install entry");
        }
        // SAFETY: h was opened above.
        unsafe { RegCloseKey(h) };
    }
}

/// `RegGetValueW(..., RRF_RT_REG_SZ)` into a fixed buffer of `units` UTF-16 units, as the C++ launcher
/// does; a longer value fails there and is skipped here.
fn reg_string(key: HKEY, sub: &[u16], value: &str, units: usize) -> Option<String> {
    let value_w = wide(value);
    let mut buf = vec![0u16; units];
    let mut cb = (units * 2) as u32;
    // SAFETY: sub and value_w are NUL-terminated; buf is valid for cb bytes.
    let rc = unsafe {
        RegGetValueW(
            key,
            sub.as_ptr(),
            value_w.as_ptr(),
            RRF_RT_REG_SZ,
            std::ptr::null_mut(),
            buf.as_mut_ptr().cast(),
            &mut cb,
        )
    };
    (rc == ERROR_SUCCESS).then(|| from_wide(&buf))
}

fn is_alnum(c: u8) -> bool {
    c.is_ascii_alphanumeric()
}

/// `PathsInText`: pull every drive-letter path out of a settings or log file and keep the ones that
/// resolve to a game folder.
fn paths_in_text(text: &[u8], channel: &str, out: &mut Vec<Found>) {
    let mut tried = 0;
    let mut i = 0usize;
    while i + 3 < text.len() && tried < 400 {
        let c = text[i];
        let starts_path = c.is_ascii_alphabetic()
            && text[i + 1] == b':'
            && (text[i + 2] == b'\\' || text[i + 2] == b'/')
            && !(i > 0 && is_alnum(text[i - 1]));
        if !starts_path {
            i += 1;
            continue;
        }
        let mut e = i + 2;
        while e < text.len()
            && !matches!(
                text[e],
                b'"' | b'\'' | b'\n' | b'\r' | b',' | b')' | b'>' | b'<' | b'|'
            )
            && e - i < 400
        {
            e += 1;
        }
        let p = &text[i..e];
        // JSON escapes "\\" and some logs use "/".
        let mut clean: Vec<u8> = Vec::with_capacity(p.len());
        let mut k = 0usize;
        while k < p.len() {
            if p[k] == b'/' {
                clean.push(b'\\');
                k += 1;
                continue;
            }
            if p[k] == b'\\' && k + 1 < p.len() && p[k + 1] == b'\\' {
                k += 1;
            }
            clean.push(p[k]);
            k += 1;
        }
        while matches!(clean.last(), Some(b' ') | Some(b'.')) {
            clean.pop();
        }
        i = e;
        tried += 1;
        let w = String::from_utf8_lossy(&clean).into_owned();
        add_found(
            out,
            &resolve_upward(&w, channel),
            "from the RSI Launcher's settings or logs",
        );
        i += 1;
    }
}

fn from_rsi_launcher_files(channel: &str, out: &mut Vec<Found>) {
    let appdata = match std::env::var_os("APPDATA") {
        Some(v) if !v.is_empty() => v.to_string_lossy().into_owned(),
        _ => return,
    };
    let base = format!("{appdata}\\rsilauncher");
    for (dir, ext) in [
        (base.clone(), ".json"),
        (format!("{base}\\logs"), ".log"),
        (base.clone(), ".log"),
    ] {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let Ok(meta) = entry.metadata() else { continue };
            if meta.file_attributes() & FILE_ATTRIBUTE_DIRECTORY != 0 {
                continue;
            }
            if meta.len() > 8 * 1024 * 1024 {
                continue; // skip huge logs
            }
            let name = entry.file_name().to_string_lossy().into_owned();
            if !name.to_ascii_lowercase().ends_with(ext) {
                continue;
            }
            if let Some(text) = read_all(&format!("{dir}\\{name}")) {
                paths_in_text(&text, channel, out);
            }
        }
    }
}

/// `FixedDrives`: never a disc or a network share.
pub fn fixed_drives() -> Vec<String> {
    let mut roots = Vec::new();
    // SAFETY: plain query.
    let drives = unsafe { GetLogicalDrives() };
    for d in 0..26u32 {
        if drives & (1 << d) == 0 {
            continue;
        }
        let root = format!("{}:\\", (b'A' + d as u8) as char);
        let root_w = wide(&root);
        // SAFETY: NUL-terminated root.
        if unsafe { GetDriveTypeW(root_w.as_ptr()) } == DRIVE_FIXED {
            roots.push(root);
        }
    }
    roots
}

fn from_usual_folders(channel: &str, out: &mut Vec<Found>) {
    const BASES: [&str; 11] = [
        "Program Files\\Roberts Space Industries",
        "Roberts Space Industries",
        "Games\\Roberts Space Industries",
        "Game\\Roberts Space Industries",
        "Program Files (x86)\\Roberts Space Industries",
        "StarCitizen",
        "Star Citizen",
        "Games\\StarCitizen",
        "Games\\Star Citizen",
        "Game\\StarCitizen",
        "Game\\Star Citizen",
    ];
    for root in fixed_drives() {
        for b in BASES {
            add_found(
                out,
                &to_bin64(&format!("{root}{b}"), channel),
                "in a usual install folder",
            );
        }
    }
}

/// `FromDriveSearch`: breadth first over every fixed drive, a few folders deep, skipping system
/// folders and links. Bounded by depth and by the number of folders looked at.
fn from_drive_search(channel: &str, out: &mut Vec<Found>) {
    search_roots(&fixed_drives(), channel, 40000, out);
}

/// The search itself, over the given roots with a folder budget. Split out so a test can run it on a
/// synthetic tree instead of this PC's drives.
fn search_roots(roots: &[String], channel: &str, budget: i32, out: &mut Vec<Found>) {
    const SKIP: [&str; 12] = [
        "Windows",
        "$Recycle.Bin",
        "System Volume Information",
        "Recovery",
        "PerfLogs",
        "$WinREAgent",
        "AppData",
        "node_modules",
        "WindowsApps",
        "Microsoft",
        "WinSxS",
        ".git",
    ];
    const MAX_DEPTH: u32 = 4;
    let mut budget = budget;
    for root in roots {
        let mut queue: Vec<(String, u32)> = vec![(strip_slashes(root), 0)];
        let mut qi = 0;
        while qi < queue.len() && budget > 0 {
            let (dir, depth) = queue[qi].clone();
            qi += 1;
            if depth != 0 && is_file(&format!("{dir}\\{channel}\\Bin64\\{GAME_EXE}")) {
                add_found(
                    out,
                    &format!("{dir}\\{channel}\\Bin64"),
                    "by searching your drives",
                );
                continue;
            }
            if depth >= MAX_DEPTH {
                continue;
            }
            let Ok(entries) = std::fs::read_dir(&dir) else {
                continue;
            };
            for entry in entries.flatten() {
                let Ok(meta) = entry.metadata() else { continue };
                let attrs = meta.file_attributes();
                if attrs & FILE_ATTRIBUTE_DIRECTORY == 0
                    || attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0
                {
                    continue;
                }
                let name = entry.file_name().to_string_lossy().into_owned();
                if SKIP.iter().any(|k| name.eq_ignore_ascii_case(k)) {
                    continue;
                }
                budget -= 1;
                queue.push((format!("{dir}\\{name}"), depth + 1));
                if budget <= 0 {
                    break;
                }
            }
        }
    }
}

/// `DetectBin64`: automatic detection, cheapest and most reliable first. The drive search only runs
/// when nothing cheaper found the game, and says so on `out` first.
pub fn detect_bin64(here: &str, channel: &str, out: &mut dyn Write) -> Vec<Found> {
    let mut found = Vec::new();
    from_remembered(here, channel, &mut found);
    from_uninstall_keys(channel, &mut found);
    from_rsi_launcher_files(channel, &mut found);
    from_usual_folders(channel, &mut found);
    if found.is_empty() {
        let _ = writeln!(
            out,
            "[i] searching your drives for StarCitizen\\{channel}\\Bin64 (a few seconds)..."
        );
        let _ = out.flush();
        from_drive_search(channel, &mut found);
    }
    found
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths_in_text_finds_nothing_in_plain_text() {
        let mut out = Vec::new();
        paths_in_text(b"no paths here, only text: 1:2 and a:b", "LIVE", &mut out);
        assert!(out.is_empty());
    }

    #[test]
    fn resolve_upward_of_nothing_is_empty() {
        assert_eq!(
            resolve_upward("C:\\scol-test\\no\\such\\scol\\folder", "LIVE"),
            ""
        );
    }

    fn tree(tag: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("scol-install-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn game(dir: &std::path::Path) {
        std::fs::create_dir_all(dir.join("LIVE").join("Bin64")).unwrap();
        std::fs::write(dir.join("LIVE").join("Bin64").join("StarCitizen.exe"), b"x").unwrap();
    }

    #[test]
    fn drive_search_depth_skips_and_budget() {
        let root = tree("search");
        game(&root.join("a").join("b").join("found-deep")); // depth 3: found
        game(
            &root
                .join("a")
                .join("b")
                .join("c")
                .join("d")
                .join("too-deep"),
        ); // depth 5: not looked at
        game(&root.join("node_modules").join("skipped")); // skipped name
        game(&root.join("Windows").join("skipped-too")); // skipped name, any case
        let roots = vec![root.to_string_lossy().into_owned()];
        let mut out = Vec::new();
        search_roots(&roots, "LIVE", 40000, &mut out);
        let bins: Vec<&str> = out.iter().map(|f| f.bin.as_str()).collect();
        assert_eq!(bins.len(), 1, "{bins:?}");
        assert!(bins[0].ends_with(r"found-deep\LIVE\Bin64"), "{bins:?}");
        assert_eq!(out[0].how, "by searching your drives");
        // a budget of one folder looks at one folder, and finds nothing
        let mut none = Vec::new();
        search_roots(&roots, "LIVE", 1, &mut none);
        assert!(none.is_empty());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn remembered_and_rsi_files_find_the_synthetic_game() {
        let root = tree("found");
        let here = root.join("here");
        let lib = root.join("lib").join("StarCitizen");
        game(&lib);
        std::fs::create_dir_all(here.join("data")).unwrap();
        let bin = lib
            .join("LIVE")
            .join("Bin64")
            .to_string_lossy()
            .into_owned();
        std::fs::write(
            here.join("data").join("game-path.txt"),
            format!("  \"{bin}\"\r\n"),
        )
        .unwrap();
        let mut out = Vec::new();
        from_remembered(&here.to_string_lossy(), "LIVE", &mut out);
        assert_eq!(out.len(), 1);
        assert_eq!(out[0].bin, bin);
        assert_eq!(out[0].how, "remembered from your last run");
        // the same install in a settings text, JSON-escaped, with a trailing dot and a comma after
        let json = format!("{{\"p\":\"{}\",\"q\":1}}", bin.replace('\\', r"\\"));
        let mut again = out.clone();
        paths_in_text(json.as_bytes(), "LIVE", &mut again);
        assert_eq!(again.len(), 1, "same install is listed once");
        let mut fresh = Vec::new();
        paths_in_text(json.as_bytes(), "LIVE", &mut fresh);
        assert_eq!(fresh.len(), 1);
        assert_eq!(fresh[0].how, "from the RSI Launcher's settings or logs");
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn registry_scan_does_not_panic() {
        let mut out = Vec::new();
        from_uninstall_keys("NoSuchChannelForScolTests", &mut out);
        assert!(out.is_empty());
    }
}
