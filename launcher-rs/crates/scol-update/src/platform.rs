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
    /// Can the player be asked a question (a console to answer in, or the window)? When not, the update
    /// is offered but not asked for (`(not asking: no console to answer in)` in the C++ launcher).
    fn can_ask(&mut self) -> bool {
        true
    }
    /// Runs after an update was swapped in, with the arguments this run was started with. `Some(code)` ends
    /// the command with that code: a console run starts the new exe with the same arguments and returns its
    /// code, a window child returns 10 so the window restarts itself. `None` (the default) just ends the
    /// command.
    fn after_update(
        &mut self,
        here: &Path,
        args: &[String],
        out: &mut dyn std::io::Write,
    ) -> Option<i32> {
        let _ = (here, args, out);
        None
    }
}

/// The real machine.
#[derive(Default)]
pub struct System;

/// How the child of [`run_self_test_as`] was started.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SelfTestRun {
    /// It runs with a "normal user" token made by Software Restriction Policies (`Safer`), not with this
    /// process's own (elevated) one.
    pub restricted_token: bool,
}

/// D5: `<here>\sc-offline.exe --self-test <tag>`, waiting up to 30 s. With `normal_rights` (this process
/// is elevated) the child gets a normal-user token like the C++ launcher's `SelfTestPasses`; when that
/// token cannot be made or used, it starts with this process's rights, as the C++ launcher does.
#[cfg(windows)]
pub fn run_self_test_as(
    here: &Path,
    tag: &str,
    normal_rights: bool,
) -> Result<SelfTestRun, String> {
    sys::run_self_test_as(here, tag, normal_rights)
}

/// True when this process runs with an elevated token (UAC "Run as administrator").
#[cfg(windows)]
pub fn is_elevated() -> bool {
    sys::is_elevated()
}

/// The new exe with the arguments this run had, waiting for it: what a console run does after an
/// update so the new version carries on with the same command. Returns its exit code.
#[cfg(windows)]
pub fn relaunch(here: &Path, args: &[String]) -> Result<i32, String> {
    let exe = here.join("sc-offline.exe");
    let status = std::process::Command::new(&exe)
        .args(args)
        .current_dir(here)
        .status()
        .map_err(|e| format!("couldn't start the new launcher ({e})"))?;
    Ok(status.code().unwrap_or(1))
}

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
    use windows_sys::Win32::Foundation::{
        CloseHandle, HANDLE, INVALID_HANDLE_VALUE, WAIT_OBJECT_0,
    };
    use windows_sys::Win32::Security::AppLocker::{
        SaferCloseLevel, SaferComputeTokenFromLevel, SaferCreateLevel, SAFER_LEVELID_NORMALUSER,
        SAFER_LEVEL_OPEN, SAFER_SCOPEID_USER,
    };
    use windows_sys::Win32::Security::{
        GetTokenInformation, TokenElevation, TOKEN_ELEVATION, TOKEN_QUERY,
    };
    use windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
        TH32CS_SNAPPROCESS,
    };
    use windows_sys::Win32::System::Threading::{
        CreateProcessAsUserW, GetCurrentProcess, GetExitCodeProcess, OpenProcessToken,
        TerminateProcess, WaitForSingleObject, CREATE_NO_WINDOW, INFINITE, PROCESS_INFORMATION,
        STARTUPINFOW,
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

    pub(super) fn is_elevated() -> bool {
        let mut token: HANDLE = std::ptr::null_mut();
        // SAFETY: the pseudo handle of this process; token is a valid out pointer.
        if unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) } == 0 {
            return false;
        }
        let mut e = TOKEN_ELEVATION { TokenIsElevated: 0 };
        let mut got = 0u32;
        // SAFETY: token has TOKEN_QUERY; e is TOKEN_ELEVATION-sized and valid for the call.
        let ok = unsafe {
            GetTokenInformation(
                token,
                TokenElevation,
                (&mut e as *mut TOKEN_ELEVATION).cast(),
                std::mem::size_of::<TOKEN_ELEVATION>() as u32,
                &mut got,
            )
        };
        // SAFETY: token was opened above.
        unsafe { CloseHandle(token) };
        ok != 0 && e.TokenIsElevated != 0
    }

    /// Starts `exe` with a normal-user token (`Safer`), hidden. None when the token cannot be made or
    /// used (the caller then starts it with its own rights).
    fn spawn_restricted(exe: &Path, cmd: &str, dir: &Path) -> Option<PROCESS_INFORMATION> {
        let mut level = std::ptr::null_mut();
        // SAFETY: level is a valid out pointer; a null reserved argument is what the call wants.
        if unsafe {
            SaferCreateLevel(
                SAFER_SCOPEID_USER,
                SAFER_LEVELID_NORMALUSER,
                SAFER_LEVEL_OPEN,
                &mut level,
                std::ptr::null(),
            )
        } == 0
        {
            return None;
        }
        let mut token: HANDLE = std::ptr::null_mut();
        // SAFETY: level was made above; token is a valid out pointer.
        let made = unsafe {
            SaferComputeTokenFromLevel(
                level,
                std::ptr::null_mut(),
                &mut token,
                0,
                std::ptr::null_mut(),
            )
        } != 0;
        let mut started = None;
        if made {
            let exe_w = wide(exe.as_os_str());
            let mut cmd_w = wide(std::ffi::OsStr::new(cmd));
            let dir_w = wide(dir.as_os_str());
            // SAFETY: zeroed plain-data structs, sizes set; every string is NUL-terminated and outlives the call.
            unsafe {
                let mut si: STARTUPINFOW = std::mem::zeroed();
                si.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
                let mut pi: PROCESS_INFORMATION = std::mem::zeroed();
                if CreateProcessAsUserW(
                    token,
                    exe_w.as_ptr(),
                    cmd_w.as_mut_ptr(),
                    std::ptr::null(),
                    std::ptr::null(),
                    0,
                    CREATE_NO_WINDOW,
                    std::ptr::null(),
                    dir_w.as_ptr(),
                    &si,
                    &mut pi,
                ) != 0
                {
                    started = Some(pi);
                }
            }
        }
        // SAFETY: both were made above and are closed once.
        unsafe {
            if !token.is_null() {
                CloseHandle(token);
            }
            SaferCloseLevel(level);
        }
        started
    }

    /// Waits up to 30 s for a process started by [`spawn_restricted`]; ends it when it hangs.
    fn wait_restricted(pi: PROCESS_INFORMATION) -> Result<(), String> {
        let mut code = 1u32;
        // SAFETY: the handles come from CreateProcessAsUserW and are closed once, here.
        let hung = unsafe {
            let hung = WaitForSingleObject(pi.hProcess, 30_000) != WAIT_OBJECT_0;
            if hung {
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 5_000);
            } else {
                GetExitCodeProcess(pi.hProcess, &mut code);
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            hung
        };
        if hung {
            return Err("the new sc-offline.exe hung in its self-test".into());
        }
        if code != 0 {
            return Err(format!("the new sc-offline.exe --self-test exited {code}"));
        }
        Ok(())
    }

    pub(super) fn run_self_test_as(
        here: &Path,
        tag: &str,
        normal_rights: bool,
    ) -> Result<SelfTestRun, String> {
        let exe = here.join("sc-offline.exe");
        if normal_rights {
            let cmd = format!("\"{}\" --self-test {tag}", exe.display());
            if let Some(pi) = spawn_restricted(&exe, &cmd, here) {
                return wait_restricted(pi).map(|()| SelfTestRun {
                    restricted_token: true,
                });
            }
        }
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
                        Some(0) => Ok(SelfTestRun {
                            restricted_token: false,
                        }),
                        Some(c) => Err(format!("the new sc-offline.exe --self-test exited {c}")),
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

    impl Platform for System {
        fn game_running(&mut self) -> bool {
            game_running()
        }

        fn run_self_test(&mut self, here: &Path, tag: &str) -> Result<(), String> {
            run_self_test_as(here, tag, is_elevated()).map(|_| ())
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
