// SPDX-License-Identifier: GPL-3.0-only
//! Path text rules of the C++ launcher: `Trim`, `StripSlashes`, `ToBin64`, `ResolveDirectories` and
//! `ParentDir`. Paths are kept as text with backslashes, exactly as the C++ code builds them, because
//! the status output prints them.

use crate::win::{from_wide, is_file, wide, wlen};
use scol_config::config::trim;
use windows_sys::Win32::Storage::FileSystem::GetFullPathNameW;
use windows_sys::Win32::UI::Shell::{
    PathCchAddBackslash, PathCchCanonicalizeEx, PathCchRemoveBackslash, PathCchRemoveFileSpec,
};

/// `kGameExe`.
pub const GAME_EXE: &str = "StarCitizen.exe";

/// `StripSlashes`: drops trailing `\` and `/` while more than 3 characters remain (so `C:\` stays).
pub fn strip_slashes(p: &str) -> String {
    let mut s = p.to_string();
    while wlen(&s) > 3 && (s.ends_with('\\') || s.ends_with('/')) {
        s.pop();
    }
    s
}

/// `ToBin64`: accepts the Bin64 folder itself, the channel folder (LIVE), the StarCitizen folder, the
/// Roberts Space Industries folder, or a library folder holding `Star Citizen\StarCitizen`. Returns
/// Bin64, or empty when no StarCitizen.exe is there.
pub fn to_bin64(p: &str, channel: &str) -> String {
    let p = strip_slashes(trim(p));
    if p.is_empty() {
        return String::new();
    }
    let tries = [
        p.clone(),
        format!("{p}\\Bin64"),
        format!("{p}\\{channel}\\Bin64"),
        format!("{p}\\StarCitizen\\{channel}\\Bin64"),
        format!("{p}\\Star Citizen\\{channel}\\Bin64"),
        format!("{p}\\Star Citizen\\StarCitizen\\{channel}\\Bin64"),
        format!("{p}\\Roberts Space Industries\\StarCitizen\\{channel}\\Bin64"),
    ];
    for t in tries {
        if is_file(&format!("{t}\\{GAME_EXE}")) {
            return t;
        }
    }
    String::new()
}

/// What `ResolveDirectories` of `install_identity_win32.h` returns: Bin64 and its channel folder,
/// normalised.
pub struct InstallDirectories {
    pub bin: String,
    pub channel: String,
}

/// `ResolveDirectories`: `bin\.` through `GetFullPathNameW` (or `PathCchCanonicalizeEx` for an
/// extended-length path), then the trailing backslash dropped, then the last component split off.
pub fn resolve_directories(bin: &str) -> Option<InstallDirectories> {
    const CAPACITY: usize = 32768;
    if bin.is_empty() || wlen(bin) > CAPACITY - 3 || bin.contains('\0') {
        return None;
    }
    let operational = wide(&format!("{bin}\\."));
    let mut buf = vec![0u16; CAPACITY];
    if bin.starts_with("\\\\?\\") {
        // PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH
        // SAFETY: valid NUL-terminated input and a buffer of CAPACITY units.
        let hr = unsafe {
            PathCchCanonicalizeEx(buf.as_mut_ptr(), CAPACITY, operational.as_ptr(), 0x10)
        };
        if hr < 0 {
            return None;
        }
    } else {
        // SAFETY: as above.
        let n = unsafe {
            GetFullPathNameW(
                operational.as_ptr(),
                CAPACITY as u32,
                buf.as_mut_ptr(),
                std::ptr::null_mut(),
            )
        };
        if n == 0 || n as usize >= CAPACITY {
            return None;
        }
    }
    // SAFETY: buf holds a NUL-terminated string and has CAPACITY units.
    unsafe {
        if PathCchAddBackslash(buf.as_mut_ptr(), CAPACITY) < 0
            || PathCchRemoveBackslash(buf.as_mut_ptr(), CAPACITY) < 0
        {
            return None;
        }
    }
    let bin_text = from_wide(&buf);
    // SAFETY: as above.
    if unsafe { PathCchRemoveFileSpec(buf.as_mut_ptr(), CAPACITY) } < 0 {
        return None;
    }
    Some(InstallDirectories {
        bin: bin_text,
        channel: from_wide(&buf),
    })
}

/// `ParentDir` (`ParentForSuffix`): `...\LIVE\Bin64` -> `...\LIVE`.
pub fn parent_dir(path: &str) -> String {
    if let Some(mut d) = resolve_directories(path) {
        if d.channel.ends_with('\\') {
            d.channel.pop();
        }
        return d.channel;
    }
    match path.rfind(['\\', '/']) {
        Some(i) => path[..i].to_string(),
        None => path.to_string(),
    }
}

/// `GamePaths`: the files of an install that the checks, `install` and `uninstall` look at.
pub struct GamePaths {
    pub bin: String,
    pub dll: String,
    /// `dinput8.dll.sc-offline-backup`: another mod's DLL, set aside while sc-offline's is in.
    pub dll_backup: String,
    pub marker: String,
    /// `<channel>\user\client\0`.
    pub user_dir: String,
    /// `default_1.xml` in `user_dir`.
    pub xml: String,
    pub xml_backup: String,
}

impl GamePaths {
    pub fn new(bin: &str) -> Self {
        let dll = format!("{bin}\\{}", scol_contract::files::MOD_DLL);
        let user_dir = format!("{}\\{}", parent_dir(bin), scol_contract::files::USER_DIR);
        let xml = format!("{user_dir}\\{}", scol_contract::files::DEFAULT_XML);
        Self {
            bin: bin.to_string(),
            dll_backup: format!("{dll}{}", scol_contract::files::BACKUP_SUFFIX),
            dll,
            marker: format!("{bin}\\{}", scol_contract::files::MARKER),
            xml_backup: format!("{xml}{}", scol_contract::files::BACKUP_SUFFIX),
            xml,
            user_dir,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strip_slashes_keeps_drive_root() {
        assert_eq!(strip_slashes("C:\\"), "C:\\");
        assert_eq!(strip_slashes("C:\\a\\\\/"), "C:\\a");
        assert_eq!(strip_slashes("ab/"), "ab/");
    }

    #[test]
    fn resolve_splits_channel() {
        let d = resolve_directories("C:\\scol-test\\x\\LIVE\\Bin64\\").unwrap();
        assert_eq!(d.bin, "C:\\scol-test\\x\\LIVE\\Bin64");
        assert_eq!(d.channel, "C:\\scol-test\\x\\LIVE");
        assert_eq!(
            parent_dir("C:\\scol-test\\x\\.\\LIVE\\Bin64"),
            "C:\\scol-test\\x\\LIVE"
        );
        assert_eq!(
            parent_dir("C:/scol-test/x/LIVE/Bin64"),
            "C:\\scol-test\\x\\LIVE"
        );
    }

    #[test]
    fn to_bin64_empty_for_nothing() {
        assert_eq!(to_bin64("  \"\" ", "LIVE"), "");
        assert_eq!(
            to_bin64("C:\\scol-test\\no-such-folder-for-scol", "LIVE"),
            ""
        );
    }
}
