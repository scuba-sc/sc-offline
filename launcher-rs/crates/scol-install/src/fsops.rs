// SPDX-License-Identifier: GPL-3.0-only
//! The few file writes the install and uninstall steps make, as small wrappers over the same Win32
//! calls the C++ launcher uses (`CopyFileW`, `MoveFileExW`, `DeleteFileW`, `SHCreateDirectoryExW`,
//! `CreateFileW` with share mode 0). `win.rs` stays read-only; everything that writes is here.
//! A failure is the `GetLastError` value, because the launcher prints it as `(error N)`.

use crate::win::wide;
use std::io::Write;
use std::os::windows::fs::OpenOptionsExt;
use windows_sys::Win32::Foundation::{GetLastError, SYSTEMTIME};
use windows_sys::Win32::Storage::FileSystem::{
    CopyFileW, DeleteFileW, MoveFileExW, FILE_ATTRIBUTE_TEMPORARY, FILE_FLAG_DELETE_ON_CLOSE,
    MOVEFILE_REPLACE_EXISTING,
};
use windows_sys::Win32::System::SystemInformation::GetLocalTime;
use windows_sys::Win32::UI::Shell::SHCreateDirectoryExW;

/// `ERROR_FILE_NOT_FOUND`.
pub const ERROR_FILE_NOT_FOUND: u32 = 2;

fn last_error() -> u32 {
    // SAFETY: plain thread-local error read, taken right after the failed call.
    unsafe { GetLastError() }
}

/// `CopyFileW(src, dst, fail_if_exists)`.
pub fn copy_file(src: &str, dst: &str, fail_if_exists: bool) -> Result<(), u32> {
    let (s, d) = (wide(src), wide(dst));
    // SAFETY: both strings are NUL-terminated and outlive the call.
    if unsafe { CopyFileW(s.as_ptr(), d.as_ptr(), i32::from(fail_if_exists)) } != 0 {
        Ok(())
    } else {
        Err(last_error())
    }
}

/// `MoveFileExW(from, to, flags)` with `flags` 0 (no replace) or `replace` (REPLACE_EXISTING).
pub fn move_file(from: &str, to: &str, replace: bool) -> Result<(), u32> {
    let (f, t) = (wide(from), wide(to));
    let flags = if replace {
        MOVEFILE_REPLACE_EXISTING
    } else {
        0
    };
    // SAFETY: both strings are NUL-terminated and outlive the call.
    if unsafe { MoveFileExW(f.as_ptr(), t.as_ptr(), flags) } != 0 {
        Ok(())
    } else {
        Err(last_error())
    }
}

/// `DeleteFileW(path)`.
pub fn delete_file(path: &str) -> Result<(), u32> {
    let p = wide(path);
    // SAFETY: NUL-terminated and outlives the call.
    if unsafe { DeleteFileW(p.as_ptr()) } != 0 {
        Ok(())
    } else {
        Err(last_error())
    }
}

/// `SHCreateDirectoryExW(nullptr, path, nullptr)`: creates the folder and its parents. The result is
/// ignored by the caller, as in C++ (an existing folder is fine; a failure shows at the next copy).
pub fn create_dir_all(path: &str) {
    let p = wide(path);
    // SAFETY: NUL-terminated and outlives the call; no window and no security attributes.
    unsafe { SHCreateDirectoryExW(std::ptr::null_mut(), p.as_ptr(), std::ptr::null()) };
}

/// `WriteAll`: `CreateFileW(CREATE_ALWAYS)` with share mode 0, then the whole text.
pub fn write_all(path: &str, bytes: &[u8]) -> Result<(), u32> {
    std::fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .share_mode(0)
        .open(path)
        .and_then(|mut f| f.write_all(bytes))
        .map_err(|e| e.raw_os_error().map_or(0, |c| c as u32))
}

/// `CanWriteTo`: a delete-on-close probe file in the folder.
pub fn can_write_to(dir: &str) -> bool {
    std::fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .share_mode(0)
        .attributes(FILE_ATTRIBUTE_TEMPORARY)
        .custom_flags(FILE_FLAG_DELETE_ON_CLOSE)
        .open(format!("{dir}\\sc-offline.write-test"))
        .is_ok()
}

/// `CreateDirectoryW(path)` for one level; the result is ignored.
pub fn create_dir(path: &str) {
    let _ = std::fs::create_dir(path);
}

/// `GetLocalTime`: year, month, day, hour, minute, second.
pub fn local_time() -> (u32, u32, u32, u32, u32, u32) {
    // SAFETY: zeroed plain-data struct that the call fills in.
    let mut t: SYSTEMTIME = unsafe { std::mem::zeroed() };
    unsafe { GetLocalTime(&mut t) };
    (
        u32::from(t.wYear),
        u32::from(t.wMonth),
        u32::from(t.wDay),
        u32::from(t.wHour),
        u32::from(t.wMinute),
        u32::from(t.wSecond),
    )
}
