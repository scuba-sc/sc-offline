// SPDX-License-Identifier: GPL-3.0-only
//! `install_discovery.h` and the identity half of `install_identity_win32.h`: two paths are one
//! install when their channel and Bin64 folders have the same volume and file id, so a junction, a
//! subst drive or an 8.3 spelling is not listed twice.

use crate::paths::resolve_directories;
use crate::win::{eq_nocase, wide};
use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FileIdInfo, FileStandardInfo, GetFileInformationByHandleEx,
    FILE_FLAG_BACKUP_SEMANTICS, FILE_ID_INFO, FILE_SHARE_DELETE, FILE_SHARE_READ, FILE_SHARE_WRITE,
    FILE_STANDARD_INFO, OPEN_EXISTING,
};

#[derive(Clone, Copy, Default, Debug)]
pub struct DirectoryIdentity {
    pub valid: bool,
    pub volume: u64,
    pub file: [u8; 16],
}

#[derive(Clone, Copy, Default, Debug)]
pub struct InstallIdentity {
    pub channel: DirectoryIdentity,
    pub bin: DirectoryIdentity,
}

/// One game folder found by discovery and the words the status output uses for how.
#[derive(Clone, Debug)]
pub struct Found {
    pub bin: String,
    pub how: &'static str,
}

fn equal(a: &DirectoryIdentity, b: &DirectoryIdentity) -> bool {
    a.volume == b.volume && a.file == b.file
}

/// `SameInstall`.
pub fn same_install(a: &str, x: &InstallIdentity, b: &str, y: &InstallIdentity) -> bool {
    if a.is_empty() || b.is_empty() {
        return false;
    }
    if x.channel.valid && y.channel.valid && !equal(&x.channel, &y.channel) {
        return false;
    }
    if x.bin.valid && y.bin.valid && !equal(&x.bin, &y.bin) {
        return false;
    }
    if x.channel.valid && y.channel.valid && x.bin.valid && y.bin.valid {
        return true;
    }
    eq_nocase(a, b)
}

/// `ReadDirectoryIdentity`.
pub fn read_directory_identity(path: &str) -> DirectoryIdentity {
    let mut result = DirectoryIdentity::default();
    let p = wide(path);
    // SAFETY: NUL-terminated path that outlives the call; a zero access mask only asks for a handle.
    let h = unsafe {
        CreateFileW(
            p.as_ptr(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            std::ptr::null(),
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            std::ptr::null_mut(),
        )
    };
    if h == INVALID_HANDLE_VALUE {
        return result;
    }
    // SAFETY: zeroed plain-data structs; the sizes passed match the structs; h is valid.
    let known = unsafe {
        let mut standard: FILE_STANDARD_INFO = std::mem::zeroed();
        let mut id: FILE_ID_INFO = std::mem::zeroed();
        let ok = GetFileInformationByHandleEx(
            h,
            FileStandardInfo,
            (&mut standard as *mut FILE_STANDARD_INFO).cast(),
            std::mem::size_of::<FILE_STANDARD_INFO>() as u32,
        ) != 0
            && standard.Directory
            && GetFileInformationByHandleEx(
                h,
                FileIdInfo,
                (&mut id as *mut FILE_ID_INFO).cast(),
                std::mem::size_of::<FILE_ID_INFO>() as u32,
            ) != 0;
        if ok {
            result.volume = id.VolumeSerialNumber;
            result.file = id.FileId.Identifier;
        }
        ok
    };
    // SAFETY: h is a handle this function opened.
    unsafe { CloseHandle(h) };
    result.valid = known;
    result
}

/// `ReadInstallIdentity`.
pub fn read_install_identity(bin: &str) -> InstallIdentity {
    match resolve_directories(bin) {
        Some(d) => InstallIdentity {
            channel: read_directory_identity(&d.channel),
            bin: read_directory_identity(&d.bin),
        },
        None => InstallIdentity::default(),
    }
}

/// `AddFound` / `AppendUnique`: adds the folder unless the list already holds the same install.
pub fn add_found(out: &mut Vec<Found>, bin: &str, how: &'static str) {
    if bin.is_empty() {
        return;
    }
    let identity = read_install_identity(bin);
    for existing in out.iter() {
        if same_install(
            &existing.bin,
            &read_install_identity(&existing.bin),
            bin,
            &identity,
        ) {
            return;
        }
    }
    out.push(Found {
        bin: bin.to_string(),
        how,
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir(volume: u64, tag: u8) -> DirectoryIdentity {
        let mut file = [0u8; 16];
        file[0] = tag;
        DirectoryIdentity {
            valid: true,
            volume,
            file,
        }
    }

    #[test]
    fn same_install_rules() {
        let a = InstallIdentity {
            channel: dir(1, 1),
            bin: dir(1, 2),
        };
        let b = a;
        let c = InstallIdentity {
            channel: dir(1, 9),
            bin: dir(1, 2),
        };
        assert!(same_install("A", &a, "B", &b));
        assert!(!same_install("A", &a, "A", &c));
        assert!(!same_install("", &a, "B", &b));
        let none = InstallIdentity::default();
        assert!(same_install("d:\\x", &none, "D:\\X", &none));
        assert!(!same_install("d:\\x", &none, "D:\\Y", &none));
    }
}
