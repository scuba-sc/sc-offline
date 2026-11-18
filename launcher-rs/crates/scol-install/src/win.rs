// SPDX-License-Identifier: GPL-3.0-only
//! Small safe wrappers over the Windows calls this crate needs. Every call here only reads.

use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::os::windows::fs::MetadataExt;
use windows_sys::Win32::Foundation::{CloseHandle, GetLastError, HANDLE, INVALID_HANDLE_VALUE};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, GetFileInformationByHandle, GetFullPathNameW, BY_HANDLE_FILE_INFORMATION,
    FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_DELETE, FILE_SHARE_READ,
    FILE_SHARE_WRITE, OPEN_EXISTING,
};

/// NUL-terminated UTF-16.
pub fn wide(s: &str) -> Vec<u16> {
    std::ffi::OsStr::new(s)
        .encode_wide()
        .chain(Some(0))
        .collect()
}

/// Text up to the first NUL of a UTF-16 buffer.
pub fn from_wide(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    std::ffi::OsString::from_wide(&buf[..end])
        .to_string_lossy()
        .into_owned()
}

/// Length in UTF-16 units, which is what the C++ launcher's `wstring::size()` counts.
pub fn wlen(s: &str) -> usize {
    s.encode_utf16().count()
}

/// `IsFile`: the path exists and is not a directory. Like `GetFileAttributesW` it does not follow a
/// link, it looks at the entry itself.
pub fn is_file(path: &str) -> bool {
    match std::fs::symlink_metadata(path) {
        Ok(m) => m.file_attributes() & FILE_ATTRIBUTE_DIRECTORY == 0,
        Err(_) => false,
    }
}

/// `ReadAll`: the whole file (shared for reading and writing like the C++ launcher), or None.
pub fn read_all(path: &str) -> Option<Vec<u8>> {
    std::fs::read(path).ok()
}

/// `EnvOr`: the variable when it is set, not empty and shorter than MAX_PATH, else the fallback.
pub fn env_or(name: &str, fallback: &str) -> String {
    match std::env::var_os(name) {
        Some(v) => {
            let s = v.to_string_lossy().into_owned();
            if s.is_empty() || wlen(&s) >= 260 {
                fallback.to_string()
            } else {
                s
            }
        }
        None => fallback.to_string(),
    }
}

/// `FullPath`: forward slashes and `..` resolved; the input when that fails.
pub fn full_path(p: &str) -> String {
    let src = wide(p);
    let mut buf = vec![0u16; 520];
    // SAFETY: both buffers are valid for the sizes passed.
    let n = unsafe {
        GetFullPathNameW(
            src.as_ptr(),
            buf.len() as u32,
            buf.as_mut_ptr(),
            std::ptr::null_mut(),
        )
    };
    if n != 0 && (n as usize) < buf.len() {
        from_wide(&buf[..n as usize])
    } else {
        p.to_string()
    }
}

/// `FileIdentity`: volume serial and file index. Ok(Some) identified, Ok(None) no such file,
/// Err(()) could not tell.
#[allow(clippy::result_unit_err)]
pub fn file_identity(path: &str) -> Result<Option<(u32, u32, u32)>, ()> {
    let p = wide(path);
    // SAFETY: p is NUL-terminated and outlives the call.
    let h: HANDLE = unsafe {
        CreateFileW(
            p.as_ptr(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            std::ptr::null(),
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            std::ptr::null_mut(),
        )
    };
    if h == INVALID_HANDLE_VALUE {
        // SAFETY: plain thread-local error read.
        let e = unsafe { GetLastError() };
        return if e == windows_sys::Win32::Foundation::ERROR_FILE_NOT_FOUND
            || e == windows_sys::Win32::Foundation::ERROR_PATH_NOT_FOUND
        {
            Ok(None)
        } else {
            Err(())
        };
    }
    // SAFETY: zeroed plain-data struct, valid handle.
    let mut info: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
    let ok = unsafe { GetFileInformationByHandle(h, &mut info) } != 0;
    // SAFETY: h is a handle this function opened.
    unsafe { CloseHandle(h) };
    if !ok {
        return Err(());
    }
    Ok(Some((
        info.dwVolumeSerialNumber,
        info.nFileIndexHigh,
        info.nFileIndexLow,
    )))
}

/// `OnWine`: ntdll exports `wine_get_version` there.
pub fn on_wine() -> bool {
    use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
    let name = wide("ntdll.dll");
    // SAFETY: NUL-terminated names that outlive the calls.
    unsafe {
        let ntdll = GetModuleHandleW(name.as_ptr());
        !ntdll.is_null() && GetProcAddress(ntdll, c"wine_get_version".as_ptr().cast()).is_some()
    }
}

/// `_wcsicmp(a, b) == 0` for the ASCII letters Windows paths and names use.
pub fn eq_nocase(a: &str, b: &str) -> bool {
    a.eq_ignore_ascii_case(b)
}
