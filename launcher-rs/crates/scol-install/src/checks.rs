// SPDX-License-Identifier: GPL-3.0-only
//! The individual read-only checks: `Identify`, `GameBuild`, `EacState`, `PcChangesLeft`,
//! `HostsBlocksEac`, `CountPlugins` and `GameMayBeRunningIn`. Each reads and returns a value.

use crate::paths::{parent_dir, GAME_EXE};
use crate::win::{env_or, eq_nocase, file_identity, from_wide, full_path, is_file, read_all};
use scol_plugins::sha256::hex_digest;
use std::io::Write;
use std::os::windows::fs::MetadataExt;
use std::sync::atomic::{AtomicU32, Ordering};
use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
use windows_sys::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows_sys::Win32::System::Threading::{
    OpenProcess, QueryFullProcessImageNameW, PROCESS_QUERY_LIMITED_INFORMATION,
};

/// `kPrebuiltSha256`: the original author's prebuilt dinput8.dll, still recognised so an old install
/// can be identified.
pub const PREBUILT_SHA256: &str =
    "57e0e5ed2151acc1020fd2ee300944842385f84bb66e216b5ebecb1ee57c10c9";

/// `RecordField` of `contract_text.h`: the value of a `key=value` line in a marker or record, or "".
pub fn field(text: &[u8], key: &str) -> String {
    let k = format!("{key}=");
    let k = k.as_bytes();
    let mut pos = 0usize;
    while pos < text.len() {
        let nl = text[pos..]
            .iter()
            .position(|&c| c == b'\n')
            .map_or(text.len(), |i| pos + i);
        if text[pos..].starts_with(k) {
            let mut v = &text[pos + k.len()..nl];
            while let Some(&last) = v.last() {
                if last == b'\r' || last == b' ' {
                    v = &v[..v.len() - 1];
                } else {
                    break;
                }
            }
            return String::from_utf8_lossy(v).into_owned();
        }
        pos = nl + 1;
    }
    String::new()
}

/// `DllInfo`.
#[derive(Debug, Clone, Default)]
pub struct DllInfo {
    pub present: bool,
    pub sha: String,
    pub what: String,
    pub version: String,
    pub ours: bool,
}

fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || hay.len() < needle.len() {
        return None;
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

/// `Identify`: which dinput8.dll this is. An empty or unreadable file is "not present".
pub fn identify(path: &str) -> DllInfo {
    let mut d = DllInfo::default();
    let Some(bytes) = read_all(path) else {
        return d;
    };
    if bytes.is_empty() {
        return d;
    }
    d.present = true;
    d.sha = hex_digest(&bytes);
    // Ours: any sc-offline build (0.3.0+ says "sc-offline v<ver>"; earlier ones said
    // "ChrisWareOffline ... / sc-offline <ver>"), or the original prebuilt DLL.
    d.ours = find(&bytes, b"ChrisWareOffline").is_some() || find(&bytes, b"sc-offline v").is_some();
    let mut at = find(&bytes, b"sc-offline v");
    let mut skip = 12;
    if at.is_none() {
        at = find(&bytes, b"/ sc-offline ");
        skip = 13;
    }
    if let Some(v) = at {
        let mut e = v + skip;
        while e < bytes.len() && bytes[e] > 0x20 && bytes[e] < 0x7F {
            e += 1;
        }
        d.version = String::from_utf8_lossy(&bytes[v + skip..e]).into_owned();
        d.what = format!("sc-offline {}", d.version);
    } else if d.sha == PREBUILT_SHA256 {
        d.what = "the original author's prebuilt DLL".to_string();
    } else {
        d.what = if d.ours {
            "unknown sc-offline build"
        } else {
            "not sc-offline"
        }
        .to_string();
    }
    d
}

/// `JsonString`: the first string value of `key`, found by plain text search, or "".
fn json_string(text: &[u8], key: &str) -> String {
    let k = format!("\"{key}\"");
    let Some(p) = find(text, k.as_bytes()) else {
        return String::new();
    };
    let Some(colon) = text[p + k.len()..].iter().position(|&c| c == b':') else {
        return String::new();
    };
    let mut p = p + k.len() + colon + 1;
    while p < text.len() && matches!(text[p], b' ' | b'\t' | b'\r' | b'\n') {
        p += 1;
    }
    if p >= text.len() || text[p] != b'"' {
        return String::new();
    }
    match text[p + 1..].iter().position(|&c| c == b'"') {
        Some(e) => String::from_utf8_lossy(&text[p + 1..p + 1 + e]).into_owned(),
        None => String::new(),
    }
}

/// Days since 1970-01-01 to (year, month, day), proleptic Gregorian.
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

/// A FILETIME (100 ns ticks since 1601-01-01 UTC) as `YYYY-MM-DD HH:MM`, what `FileTimeToSystemTime`
/// gives the C++ launcher.
pub fn filetime_minute(ticks: u64) -> String {
    let secs = (ticks / 10_000_000) as i64 - 11_644_473_600;
    let (y, m, d) = civil_from_days(secs.div_euclid(86_400));
    let rest = secs.rem_euclid(86_400);
    format!(
        "{y:04}-{m:02}-{d:02} {:02}:{:02}",
        rest / 3600,
        rest % 3600 / 60
    )
}

/// `GameBuild`: `Version` and `RequestedP4ChangeNum` from `<channel>\build_manifest.id`, else
/// StarCitizen.exe's size and date. "" when neither is there.
pub fn game_build(bin: &str) -> String {
    if let Some(manifest) = read_all(&format!("{}\\build_manifest.id", parent_dir(bin))) {
        let ver = json_string(&manifest, "Version");
        let cl = json_string(&manifest, "RequestedP4ChangeNum");
        if !ver.is_empty() || !cl.is_empty() {
            return if cl.is_empty() {
                ver
            } else {
                format!("{ver} (CL {cl})")
            };
        }
    }
    match std::fs::symlink_metadata(format!("{bin}\\{GAME_EXE}")) {
        Ok(m) => format!(
            "{GAME_EXE} {} bytes, {}",
            m.file_size(),
            filetime_minute(m.last_write_time())
        ),
        Err(_) => String::new(),
    }
}

/// `Eac`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Eac {
    Active,
    Disabled,
    NotInstalled,
}

/// `EacExe`.
pub fn eac_exe() -> String {
    format!(
        "{}\\EasyAntiCheat_EOS\\EasyAntiCheat_EOS.exe",
        env_or("ProgramFiles(x86)", "C:\\Program Files (x86)")
    )
}

/// `EacState`.
pub fn eac_state() -> Eac {
    let exe = eac_exe();
    if is_file(&exe) {
        return Eac::Active;
    }
    if is_file(&format!("{exe}.bak")) {
        Eac::Disabled
    } else {
        Eac::NotInstalled
    }
}

/// `kEacHost`.
pub const EAC_HOST: &str = "modules-cdn.eac-prod.on.epicgames.com";

/// `HostsPath`.
pub fn hosts_path() -> String {
    format!(
        "{}\\System32\\drivers\\etc\\hosts",
        env_or("SystemRoot", "C:\\Windows")
    )
}

/// `HostsBlocksEac`: a hosts line (comments cut off) that names the EAC download server.
pub fn hosts_blocks_eac() -> bool {
    let Some(hosts) = read_all(&hosts_path()) else {
        return false;
    };
    hosts.split(|&c| c == b'\n').any(|line| {
        let line = match line.iter().position(|&c| c == b'#') {
            Some(h) => &line[..h],
            None => line,
        };
        find(line, EAC_HOST.as_bytes()).is_some()
    })
}

/// `PcChangesPath`.
pub fn pc_changes_path() -> String {
    format!(
        "{}\\sc-offline\\{}",
        env_or("ProgramData", "C:\\ProgramData"),
        scol_contract::files::PC_CHANGES
    )
}

/// `PcChangesLeft`: what a previous helper recorded and has not undone yet (empty = nothing).
pub fn pc_changes_left() -> Vec<u8> {
    read_all(&pc_changes_path()).unwrap_or_default()
}

/// `kFwRules`: the record key and the Windows Firewall rule name behind it.
pub const FW_RULES: [(&str, &str); 4] = [
    ("firewall", "sc-offline: block StarCitizen.exe"),
    ("fw_launcher", "sc-offline: block RSI Launcher.exe"),
    ("fw_crash", "sc-offline: block CrashHandler.exe"),
    (
        "fw_mp",
        "sc-offline: allow StarCitizen.exe multiplayer (LAN)",
    ),
];

/// `DescribePcChanges`: one line per change the record lists.
pub fn describe_pc_changes(rec: &[u8], out: &mut dyn Write) {
    for (key, name) in FW_RULES {
        if field(rec, key) == "added" {
            let _ = writeln!(out, "          - firewall rule \"{name}\"");
        }
    }
    if field(rec, "hosts") == "added" {
        let _ = writeln!(out, "          - hosts line for {EAC_HOST}");
    }
    if field(rec, "eac") == "renamed" {
        let _ = writeln!(out, "          - EasyAntiCheat_EOS.exe renamed to .bak");
    }
}

/// `CountPlugins`: the plugin folders the host would look at (a `plugin.ini` inside) and how many of
/// them are not switched off. Returns (enabled, total).
pub fn count_plugins(here: &str) -> (usize, usize) {
    let dir = format!("{here}\\data\\plugins");
    let (mut enabled, mut total) = (0, 0);
    let Ok(entries) = std::fs::read_dir(&dir) else {
        return (0, 0);
    };
    for entry in entries.flatten() {
        let Ok(meta) = entry.metadata() else { continue };
        if meta.file_attributes() & 0x10 == 0 {
            continue;
        }
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.starts_with('.') {
            continue;
        }
        let folder = format!("{dir}\\{name}");
        if !is_file(&format!("{folder}\\plugin.ini")) {
            continue;
        }
        total += 1;
        if std::fs::symlink_metadata(format!(
            "{folder}\\{}",
            scol_contract::files::PLUGIN_DISABLED_MARKER
        ))
        .is_err()
        {
            enabled += 1;
        }
    }
    (enabled, total)
}

#[derive(PartialEq, Eq)]
enum ImageMatch {
    Yes,
    No,
    Unknown,
}

/// `MatchImage`: does the process run `<bin>\StarCitizen.exe`? Same path text, else the same file.
/// No = certainly another file (or no such exe in bin); Unknown = could not identify.
fn match_image(proc: windows_sys::Win32::Foundation::HANDLE, bin: &str) -> ImageMatch {
    if bin.is_empty() {
        return ImageMatch::No;
    }
    let mut path = [0u16; 520];
    let mut n = path.len() as u32;
    // SAFETY: path is valid for n units.
    if unsafe { QueryFullProcessImageNameW(proc, 0, path.as_mut_ptr(), &mut n) } == 0 {
        return ImageMatch::Unknown;
    }
    let image = full_path(&from_wide(&path[..n as usize]));
    let target = full_path(&format!("{bin}\\{GAME_EXE}"));
    if eq_nocase(&image, &target) {
        return ImageMatch::Yes;
    }
    let wanted = file_identity(&target);
    let b = match wanted {
        Ok(None) => return ImageMatch::No,
        Ok(Some(b)) => b,
        Err(()) => return ImageMatch::Unknown,
    };
    match file_identity(&image) {
        Ok(Some(a)) if a == b => ImageMatch::Yes,
        Ok(Some(_)) => ImageMatch::No,
        _ => ImageMatch::Unknown,
    }
}

static LAST_NOTED: AtomicU32 = AtomicU32::new(0);

/// `GameMayBeRunningIn`: the conservative guard. Any StarCitizen.exe that cannot be opened or whose
/// image cannot be identified counts as running; only a process certainly of another file does not.
/// A failed process snapshot counts as running too. Only reads the process list.
pub fn game_may_be_running_in(bin: &str, out: &mut dyn Write) -> bool {
    // SAFETY: plain snapshot request.
    let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    if snap == INVALID_HANDLE_VALUE {
        return true;
    }
    // SAFETY: zeroed plain-data struct whose size field is set before use.
    let mut pe: PROCESSENTRY32W = unsafe { std::mem::zeroed() };
    pe.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
    let mut hit = false;
    // SAFETY: snap is a valid snapshot handle; pe is initialised.
    let mut ok = unsafe { Process32FirstW(snap, &mut pe) } != 0;
    while ok && !hit {
        if eq_nocase(&from_wide(&pe.szExeFile), GAME_EXE) {
            // SAFETY: query-only access to a process by id.
            let h = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pe.th32ProcessID) };
            let m = if h.is_null() {
                ImageMatch::Unknown
            } else {
                match_image(h, bin)
            };
            if !h.is_null() {
                // SAFETY: h was opened above.
                unsafe { CloseHandle(h) };
            }
            if m != ImageMatch::No {
                hit = true;
                if m == ImageMatch::Unknown
                    && LAST_NOTED.swap(pe.th32ProcessID, Ordering::Relaxed) != pe.th32ProcessID
                {
                    let _ = writeln!(
                        out,
                        "[i] a {GAME_EXE} (process {}) could not be identified; counting the game as running",
                        pe.th32ProcessID
                    );
                }
            }
        }
        // SAFETY: as above.
        ok = unsafe { Process32NextW(snap, &mut pe) } != 0;
    }
    // SAFETY: snap was created above.
    unsafe { CloseHandle(snap) };
    hit
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn field_reads_last_cr_and_spaces() {
        let rec = b"version=1\r\ntime=2026-11-17 09:05  \nfirewall=added\n";
        assert_eq!(field(rec, "time"), "2026-11-17 09:05");
        assert_eq!(field(rec, "version"), "1");
        assert_eq!(field(rec, "firewall"), "added");
        assert_eq!(field(rec, "hosts"), "");
        assert_eq!(field(b"xtime=1\n", "time"), "");
    }

    #[test]
    fn filetime_known_dates() {
        // 1970-01-01 00:00 UTC
        assert_eq!(filetime_minute(116_444_736_000_000_000), "1970-01-01 00:00");
        // 2000-02-29 12:34:56 UTC = 951827696
        assert_eq!(
            filetime_minute((951_827_696 + 11_644_473_600) * 10_000_000),
            "2000-02-29 12:34"
        );
    }

    #[test]
    fn json_string_rules() {
        let m = b"{ \"Version\" : \"4.0.2\",\n \"RequestedP4ChangeNum\": \"9776\" }";
        assert_eq!(json_string(m, "Version"), "4.0.2");
        assert_eq!(json_string(m, "RequestedP4ChangeNum"), "9776");
        assert_eq!(json_string(b"{\"Version\": 4}", "Version"), "");
        assert_eq!(json_string(b"{\"Version\": \"open", "Version"), "");
    }

    #[test]
    fn game_build_prefers_manifest() {
        let dir = std::env::temp_dir().join(format!("scol-install-gb-{}", std::process::id()));
        let bin = dir.join("LIVE").join("Bin64");
        std::fs::create_dir_all(&bin).unwrap();
        std::fs::write(bin.join("StarCitizen.exe"), b"x").unwrap();
        let bin_s = bin.to_string_lossy().into_owned();
        assert!(game_build(&bin_s).starts_with("StarCitizen.exe 1 bytes, "));
        std::fs::write(
            dir.join("LIVE").join("build_manifest.id"),
            b"{\"Data\":{\"Version\":\"4.1\",\"RequestedP4ChangeNum\":\"77\"}}",
        )
        .unwrap();
        assert_eq!(game_build(&bin_s), "4.1 (CL 77)");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn identify_rules() {
        let dir = std::env::temp_dir().join(format!("scol-install-id-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let p = |n: &str| dir.join(n).to_string_lossy().into_owned();
        assert!(!identify(&p("none.dll")).present);
        std::fs::write(p("empty.dll"), b"").unwrap();
        assert!(!identify(&p("empty.dll")).present);
        std::fs::write(p("a.dll"), b"MZ..sc-offline v0.8.0-rc.1\0tail").unwrap();
        let a = identify(&p("a.dll"));
        assert!(a.present && a.ours);
        assert_eq!(a.version, "0.8.0-rc.1");
        assert_eq!(a.what, "sc-offline 0.8.0-rc.1");
        std::fs::write(
            p("b.dll"),
            b"ChrisWareOffline 0.9.0 / sc-offline 0.2.0-rc4 end",
        )
        .unwrap();
        assert_eq!(identify(&p("b.dll")).what, "sc-offline 0.2.0-rc4");
        std::fs::write(p("c.dll"), b"ChrisWareOffline only").unwrap();
        assert_eq!(identify(&p("c.dll")).what, "unknown sc-offline build");
        std::fs::write(p("d.dll"), b"another mod").unwrap();
        let d = identify(&p("d.dll"));
        assert!(!d.ours);
        assert_eq!(d.what, "not sc-offline");
        assert_eq!(d.sha.len(), 64);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn plugin_counts() {
        let dir = std::env::temp_dir().join(format!("scol-install-pl-{}", std::process::id()));
        let plugins = dir.join("data").join("plugins");
        for (name, ini, disabled) in [
            ("a", true, false),
            ("b", true, true),
            ("c", false, false),
            (".hidden", true, false),
        ] {
            let f = plugins.join(name);
            std::fs::create_dir_all(&f).unwrap();
            if ini {
                std::fs::write(f.join("plugin.ini"), b"").unwrap();
            }
            if disabled {
                std::fs::write(f.join("disabled"), b"").unwrap();
            }
        }
        std::fs::write(plugins.join("loose.txt"), b"").unwrap();
        assert_eq!(count_plugins(&dir.to_string_lossy()), (1, 2));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
