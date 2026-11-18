// SPDX-License-Identifier: GPL-3.0-only
//! Everything the update flow needs from the machine, behind one trait so tests can drive it:
//! is the game running, run the new exe's `--self-test`, can this folder be written, run the swap
//! elevated, free disk space, waiting. [`System`] is the real one.

use std::path::{Path, PathBuf};

pub trait Platform {
    /// D4: is any StarCitizen.exe running, wherever it lives?
    fn game_running(&mut self) -> bool;
    /// D5: run `<here>\sc-offline.exe --self-test <tag>`, wait up to 30 s. Err is the message.
    fn run_self_test(&mut self, here: &Path, tag: &str) -> Result<(), String>;
    /// Can a file be created in `dir`?
    fn can_write(&mut self, dir: &Path) -> bool;
    /// Run `sc-offline.exe --elevated-update <tag> <staged>` as administrator and wait.
    /// Ok(exit code), or Err when elevation was refused or could not start.
    fn run_elevated(&mut self, here: &Path, tag: &str, staged: &Path) -> Result<i32, String>;
    /// Free bytes on the volume of `dir`, when known.
    fn free_space(&mut self, dir: &Path) -> Option<u64>;
    /// `%LOCALAPPDATA%`, when set.
    fn local_app_data(&mut self) -> Option<PathBuf>;
    /// Wait (the swap's retry back-off).
    fn sleep_ms(&mut self, ms: u64);
}

/// The real machine.
#[derive(Default)]
pub struct System;

/// Write a throw-away file in `dir` and remove it.
pub fn dir_writable(dir: &Path) -> bool {
    let p = dir.join(format!(".scol-write-test-{}", std::process::id()));
    match std::fs::File::create(&p) {
        Ok(_) => {
            let _ = std::fs::remove_file(&p);
            true
        }
        Err(_) => false,
    }
}

#[cfg(windows)]
mod sys {
    use super::*;
    use std::os::windows::ffi::OsStrExt;
    use std::os::windows::process::CommandExt;
    use std::time::{Duration, Instant};
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
        TH32CS_SNAPPROCESS,
    };
    use windows_sys::Win32::System::Threading::{
        GetExitCodeProcess, WaitForSingleObject, CREATE_NO_WINDOW, INFINITE,
    };
    use windows_sys::Win32::UI::Shell::{
        ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW,
    };

    fn wide(s: &std::ffi::OsStr) -> Vec<u16> {
        s.encode_wide().chain(std::iter::once(0)).collect()
    }

    /// The C++ `GameRunning`: a process named StarCitizen.exe, wherever it runs. A failed
    /// snapshot counts as running (the safe side).
    fn game_running() -> bool {
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
            let n = pe
                .szExeFile
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(pe.szExeFile.len());
            hit = String::from_utf16_lossy(&pe.szExeFile[..n])
                .eq_ignore_ascii_case("StarCitizen.exe");
            // SAFETY: as above.
            ok = unsafe { Process32NextW(snap, &mut pe) } != 0;
        }
        // SAFETY: snap was created above.
        unsafe { CloseHandle(snap) };
        hit
    }

    impl Platform for System {
        fn game_running(&mut self) -> bool {
            game_running()
        }

        fn run_self_test(&mut self, here: &Path, tag: &str) -> Result<(), String> {
            let exe = here.join("sc-offline.exe");
            let mut child = std::process::Command::new(&exe)
                .arg("--self-test")
                .arg(tag)
                .current_dir(here)
                .creation_flags(CREATE_NO_WINDOW)
                .stdin(std::process::Stdio::null())
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .spawn()
                .map_err(|e| {
                    format!(
                        "couldn't start the new sc-offline.exe (error {})",
                        e.raw_os_error().unwrap_or(0)
                    )
                })?;
            let start = Instant::now();
            loop {
                match child.try_wait() {
                    Ok(Some(st)) => {
                        return match st.code() {
                            Some(0) => Ok(()),
                            Some(c) => {
                                Err(format!("the new sc-offline.exe --self-test exited {c}"))
                            }
                            None => Err("the new sc-offline.exe --self-test was killed".into()),
                        }
                    }
                    Ok(None) if start.elapsed() > Duration::from_secs(30) => {
                        let _ = child.kill();
                        let _ = child.wait();
                        return Err("the new sc-offline.exe hung in its self-test".into());
                    }
                    Ok(None) => std::thread::sleep(Duration::from_millis(50)),
                    Err(e) => return Err(format!("waiting for the self-test failed ({e})")),
                }
            }
        }

        fn can_write(&mut self, dir: &Path) -> bool {
            dir_writable(dir)
        }

        fn run_elevated(&mut self, here: &Path, tag: &str, staged: &Path) -> Result<i32, String> {
            let exe = std::env::current_exe().map_err(|e| e.to_string())?;
            let params = format!("--elevated-update {tag} \"{}\"", staged.display());
            let (verb, file, par, dir) = (
                wide("runas".as_ref()),
                wide(exe.as_os_str()),
                wide(params.as_ref()),
                wide(here.as_os_str()),
            );
            // SAFETY: zeroed plain-data struct; every pointer outlives the call.
            let mut sei: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
            sei.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = verb.as_ptr();
            sei.lpFile = file.as_ptr();
            sei.lpParameters = par.as_ptr();
            sei.lpDirectory = dir.as_ptr();
            sei.nShow = 1;
            // SAFETY: sei is initialised and its strings are NUL-terminated.
            if unsafe { ShellExecuteExW(&mut sei) } == 0 || sei.hProcess.is_null() {
                return Err("administrator rights refused".into());
            }
            let mut code = 1u32;
            // SAFETY: hProcess is a valid process handle returned above.
            unsafe {
                WaitForSingleObject(sei.hProcess, INFINITE);
                GetExitCodeProcess(sei.hProcess, &mut code);
                CloseHandle(sei.hProcess);
            }
            Ok(code as i32)
        }

        fn free_space(&mut self, dir: &Path) -> Option<u64> {
            let w = wide(dir.as_os_str());
            let mut free = 0u64;
            // SAFETY: w is NUL-terminated; free is a valid out pointer; the others may be null.
            let ok = unsafe {
                GetDiskFreeSpaceExW(
                    w.as_ptr(),
                    &mut free,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            };
            (ok != 0).then_some(free)
        }

        fn local_app_data(&mut self) -> Option<PathBuf> {
            std::env::var_os("LOCALAPPDATA").map(PathBuf::from)
        }

        fn sleep_ms(&mut self, ms: u64) {
            std::thread::sleep(Duration::from_millis(ms));
        }
    }
}

#[cfg(not(windows))]
impl Platform for System {
    fn game_running(&mut self) -> bool {
        false
    }
    fn run_self_test(&mut self, _: &Path, _: &str) -> Result<(), String> {
        Err("self-test needs Windows".into())
    }
    fn can_write(&mut self, dir: &Path) -> bool {
        dir_writable(dir)
    }
    fn run_elevated(&mut self, _: &Path, _: &str, _: &Path) -> Result<i32, String> {
        Err("elevation needs Windows".into())
    }
    fn free_space(&mut self, _: &Path) -> Option<u64> {
        None
    }
    fn local_app_data(&mut self) -> Option<PathBuf> {
        None
    }
    fn sleep_ms(&mut self, ms: u64) {
        std::thread::sleep(std::time::Duration::from_millis(ms));
    }
}
