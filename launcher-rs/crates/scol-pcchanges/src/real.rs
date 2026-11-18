// SPDX-License-Identifier: GPL-3.0-only
//! The real [`System`]: it runs netsh and ipconfig, edits the hosts file, renames the EAC exe and
//! writes the record. No test runs it; tests use [`crate::fake::FakeSystem`]. Windows only.
//!
//! The registry, token and clock calls are the only unsafe code in the crate; each one says why
//! it is sound.

#![allow(unsafe_code)]

use crate::system::{ErrorCode, LocalTime, System};
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::os::windows::fs::OpenOptionsExt;
use std::os::windows::process::CommandExt;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};
use windows_sys::Win32::Foundation::{CloseHandle, ERROR_SUCCESS, HANDLE};
use windows_sys::Win32::Security::{
    GetTokenInformation, TokenElevation, TOKEN_ELEVATION, TOKEN_QUERY,
};
use windows_sys::Win32::Storage::FileSystem::GetFullPathNameW;
use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
use windows_sys::Win32::System::Registry::{
    RegCloseKey, RegEnumKeyExW, RegGetValueW, RegOpenKeyExW, HKEY, HKEY_CURRENT_USER,
    HKEY_LOCAL_MACHINE, KEY_READ, RRF_RT_REG_SZ,
};
use windows_sys::Win32::System::SystemInformation::GetLocalTime;
use windows_sys::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};
use windows_sys::Win32::UI::Shell::{
    PathCchAddBackslash, PathCchCanonicalizeEx, PathCchRemoveBackslash, PathCchRemoveFileSpec,
};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;
/// `STILL_ACTIVE`: what `GetExitCodeProcess` gives for a process that did not finish in time.
const STILL_ACTIVE: i32 = 259;
const FILE_ATTRIBUTE_TEMPORARY: u32 = 0x100;
const FILE_FLAG_DELETE_ON_CLOSE: u32 = 0x0400_0000;

pub struct RealSystem;

fn wide(s: &str) -> Vec<u16> {
    std::ffi::OsStr::new(s)
        .encode_wide()
        .chain(Some(0))
        .collect()
}

fn from_wide(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    std::ffi::OsString::from_wide(&buf[..end])
        .to_string_lossy()
        .into_owned()
}

fn code(e: &std::io::Error) -> ErrorCode {
    e.raw_os_error().map(|c| c as u32).unwrap_or(1)
}

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

/// `Trim`: cuts spaces, tabs, CR, LF and quotes from both ends.
fn trim(s: &str) -> &str {
    s.trim_matches([' ', '\t', '\r', '\n', '"'])
}

/// `StripSlashes`.
fn strip_slashes(p: &str) -> String {
    let mut p = p.to_string();
    while p.len() > 3 && (p.ends_with('\\') || p.ends_with('/')) {
        p.pop();
    }
    p
}

impl System for RealSystem {
    fn env(&self, name: &str) -> Option<String> {
        std::env::var_os(name).map(|v| v.to_string_lossy().into_owned())
    }

    fn run(&mut self, exe: &str, args: &str) -> i32 {
        let child = Command::new(exe)
            .raw_arg(args)
            .creation_flags(CREATE_NO_WINDOW)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn();
        let Ok(mut child) = child else {
            return -1;
        };
        let start = Instant::now();
        loop {
            match child.try_wait() {
                Ok(Some(status)) => return status.code().unwrap_or(1),
                Ok(None) if start.elapsed() < Duration::from_secs(60) => {
                    std::thread::sleep(Duration::from_millis(50));
                }
                _ => return STILL_ACTIVE,
            }
        }
    }

    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, ErrorCode> {
        std::fs::read(path).map_err(|e| code(&e))
    }

    fn write_file(&mut self, path: &str, data: &[u8]) -> Result<(), ErrorCode> {
        std::fs::write(path, data).map_err(|e| code(&e))
    }

    fn rename(&mut self, from: &str, to: &str, replace: bool) -> Result<(), ErrorCode> {
        if !replace && std::fs::symlink_metadata(to).is_ok() {
            return Err(183); // ERROR_ALREADY_EXISTS
        }
        std::fs::rename(from, to).map_err(|e| code(&e))
    }

    fn delete_file(&mut self, path: &str) {
        let _ = std::fs::remove_file(path);
    }

    fn create_dir(&mut self, path: &str) {
        let _ = std::fs::create_dir(path);
    }

    fn is_file(&mut self, path: &str) -> bool {
        std::fs::metadata(path)
            .map(|m| m.is_file())
            .unwrap_or(false)
    }

    fn can_write_to(&mut self, dir: &str) -> bool {
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

    fn is_elevated(&mut self) -> bool {
        let mut token: HANDLE = std::ptr::null_mut();
        // SAFETY: the pseudo handle of this process is valid; token receives a handle we close.
        if unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) } == 0 {
            return false;
        }
        let mut e = TOKEN_ELEVATION { TokenIsElevated: 0 };
        let mut n = 0u32;
        // SAFETY: e is valid for its size; token is the handle opened above.
        let ok = unsafe {
            GetTokenInformation(
                token,
                TokenElevation,
                (&mut e as *mut TOKEN_ELEVATION).cast(),
                std::mem::size_of::<TOKEN_ELEVATION>() as u32,
                &mut n,
            )
        } != 0;
        // SAFETY: token was opened by OpenProcessToken and is closed once.
        unsafe { CloseHandle(token) };
        ok && e.TokenIsElevated != 0
    }

    fn on_wine(&mut self) -> bool {
        let name = wide("ntdll.dll");
        // SAFETY: NUL-terminated names that outlive the calls.
        unsafe {
            let ntdll = GetModuleHandleW(name.as_ptr());
            !ntdll.is_null() && GetProcAddress(ntdll, c"wine_get_version".as_ptr().cast()).is_some()
        }
    }

    fn local_time(&mut self) -> LocalTime {
        // SAFETY: zeroed plain-data struct that the call fills in.
        let mut t: windows_sys::Win32::Foundation::SYSTEMTIME = unsafe { std::mem::zeroed() };
        // SAFETY: t is a valid SYSTEMTIME.
        unsafe { GetLocalTime(&mut t) };
        LocalTime {
            year: u32::from(t.wYear),
            month: u32::from(t.wMonth),
            day: u32::from(t.wDay),
            hour: u32::from(t.wHour),
            minute: u32::from(t.wMinute),
        }
    }

    fn registry_rsi_launcher(&mut self) -> Option<String> {
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
            if unsafe { RegOpenKeyExW(root, path_w.as_ptr(), 0, KEY_READ, &mut h) } != ERROR_SUCCESS
            {
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
                let Some(name) = reg_string(h, &sub, "DisplayName", 256) else {
                    continue;
                };
                if !name.to_ascii_lowercase().contains("rsi launcher") {
                    continue;
                }
                let Some(loc) = reg_string(h, &sub, "InstallLocation", 520) else {
                    continue;
                };
                let exe = format!("{}\\RSI Launcher.exe", strip_slashes(trim(&loc)));
                if std::fs::metadata(&exe)
                    .map(|m| m.is_file())
                    .unwrap_or(false)
                {
                    // SAFETY: h was opened above and is closed once.
                    unsafe { RegCloseKey(h) };
                    return Some(exe);
                }
            }
            // SAFETY: h was opened above and is closed once.
            unsafe { RegCloseKey(h) };
        }
        None
    }

    fn resolve_dirs(&mut self, bin: &str) -> Option<(String, String)> {
        const CAPACITY: usize = 32768;
        if bin.is_empty() || bin.encode_utf16().count() > CAPACITY - 3 || bin.contains('\0') {
            return None;
        }
        let operational = wide(&format!("{bin}\\."));
        let mut buf = vec![0u16; CAPACITY];
        if bin.starts_with("\\\\?\\") {
            // PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH
            // SAFETY: NUL-terminated input and a buffer of CAPACITY units.
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
        Some((bin_text, from_wide(&buf)))
    }
}
