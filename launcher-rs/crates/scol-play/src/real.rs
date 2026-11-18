// SPDX-License-Identifier: GPL-3.0-only
//! The real [`Host`]: it starts the helper (hidden, or elevated with one UAC prompt), creates the named
//! events, starts the game with normal rights and waits for it. No test runs it; tests use
//! [`crate::fake::FakeHost`]. Windows only.
//!
//! The Win32 calls are the only unsafe code in the crate; each one says why it is sound.

#![allow(unsafe_code)]

use crate::helper::helper_main;
use crate::host::{Host, Ready, Signals};
use crate::log::LogOnly;
use crate::stop::{Begin, GameProc, Poll, StopPort};
use scol_install::checks::identify;
use scol_install::paths::{GamePaths, GAME_EXE};
use scol_pcchanges::real::RealSystem;
use scol_pcchanges::{HelperStart, System};
use std::io::Write;
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::IntoRawHandle;
use std::os::windows::process::CommandExt;
use std::process::{Command, Stdio};
use windows_sys::Win32::Foundation::{
    CloseHandle, GetLastError, FILETIME, HANDLE, TRUE, WAIT_OBJECT_0,
};
use windows_sys::Win32::Security::{
    DuplicateTokenEx, GetTokenInformation, SecurityImpersonation, TokenElevation, TokenPrimary,
    TOKEN_ADJUST_DEFAULT, TOKEN_ADJUST_SESSIONID, TOKEN_ASSIGN_PRIMARY, TOKEN_DUPLICATE,
    TOKEN_ELEVATION, TOKEN_QUERY,
};
use windows_sys::Win32::Storage::FileSystem::SYNCHRONIZE;
use windows_sys::Win32::System::Console::{
    GetConsoleMode, GetStdHandle, SetConsoleCtrlHandler, CTRL_BREAK_EVENT, CTRL_C_EVENT,
    STD_INPUT_HANDLE,
};
use windows_sys::Win32::System::Threading::{
    CreateEventW, CreateProcessWithTokenW, GetCurrentProcessId, GetExitCodeProcess,
    GetProcessTimes, OpenEventW, OpenProcess, OpenProcessToken, SetEvent, TerminateProcess,
    WaitForMultipleObjects, WaitForSingleObject, CREATE_UNICODE_ENVIRONMENT, EVENT_MODIFY_STATE,
    INFINITE, PROCESS_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_TERMINATE,
    STARTUPINFOW,
};
use windows_sys::Win32::UI::Shell::{
    ShellExecuteExW, ShellExecuteW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    EnumWindows, GetShellWindow, GetWindowThreadProcessId, IsWindowVisible, PostMessageW, SW_HIDE,
    SW_SHOWNORMAL, WM_CLOSE,
};

const DETACHED_PROCESS: u32 = 0x0000_0008;
const CREATE_NEW_PROCESS_GROUP: u32 = 0x0000_0200;
const STILL_ACTIVE: u32 = 259;
const ERROR_FILE_NOT_FOUND: u32 = 2;

fn wide(s: &str) -> Vec<u16> {
    std::ffi::OsStr::new(s)
        .encode_wide()
        .chain(Some(0))
        .collect()
}

fn last_error() -> u32 {
    // SAFETY: plain thread-local error read, taken right after the failed call.
    unsafe { GetLastError() }
}

fn exit_code_of(h: HANDLE) -> i32 {
    let mut code = 1u32;
    // SAFETY: h is a process handle with query rights; code is valid for the call.
    unsafe { GetExitCodeProcess(h, &mut code) };
    code as i32
}

fn self_exe() -> String {
    std::env::current_exe()
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_default()
}

/// The game process `start_game` started.
enum Game {
    None,
    Child(std::process::Child),
    Handle(HANDLE),
}

pub struct RealHost {
    sys: RealSystem,
    ready: HANDLE,
    closed: HANDLE,
    helper: HANDLE,
    game: Game,
    /// The update check of `play`, set by the caller that owns the updater.
    pub update_hook: Option<UpdateHook>,
}

/// What `play` runs for its automatic update check.
pub type UpdateHook = Box<dyn FnMut(&scol_config::Config, &mut dyn Write)>;

impl Default for RealHost {
    fn default() -> Self {
        Self::new()
    }
}

impl RealHost {
    pub fn new() -> Self {
        RealHost {
            sys: RealSystem,
            ready: std::ptr::null_mut(),
            closed: std::ptr::null_mut(),
            helper: std::ptr::null_mut(),
            game: Game::None,
            update_hook: None,
        }
    }

    fn close_handle(h: &mut HANDLE) {
        if !h.is_null() {
            // SAFETY: the handle was opened by this struct and is closed once.
            unsafe { CloseHandle(*h) };
            *h = std::ptr::null_mut();
        }
    }

    /// `StartGame`'s plain path: the game as a child of this process.
    fn plain(&mut self, exe: &str, dir: &str) -> Result<(), u32> {
        let child = Command::new(exe)
            .current_dir(dir)
            .spawn()
            .map_err(|e| e.raw_os_error().unwrap_or(2) as u32)?;
        self.game = Game::Child(child);
        Ok(())
    }

    /// The game with the token of the desktop shell (normal rights). Ok(true) started, Ok(false) it could
    /// not be done (Err inside: the error), shell_elevated says the shell itself is elevated.
    fn start_with_shell_token(&mut self, exe: &str, dir: &str) -> (bool, bool, u32) {
        let mut shell_pid = 0u32;
        // SAFETY: GetShellWindow takes no input; the pid out-pointer is valid.
        unsafe {
            let shell = GetShellWindow();
            if !shell.is_null() {
                GetWindowThreadProcessId(shell, &mut shell_pid);
            }
        }
        let mut started = false;
        let mut shell_elevated = false;
        let mut err = 0u32;
        // SAFETY: handles opened here are closed below; structs are zeroed plain data with sizes set.
        unsafe {
            let shell_proc = if shell_pid != 0 {
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, shell_pid)
            } else {
                std::ptr::null_mut()
            };
            let mut shell_token: HANDLE = std::ptr::null_mut();
            let mut token: HANDLE = std::ptr::null_mut();
            if !shell_proc.is_null()
                && OpenProcessToken(shell_proc, TOKEN_QUERY | TOKEN_DUPLICATE, &mut shell_token)
                    != 0
            {
                let mut e = TOKEN_ELEVATION { TokenIsElevated: 0 };
                let mut n = 0u32;
                shell_elevated = GetTokenInformation(
                    shell_token,
                    TokenElevation,
                    (&mut e as *mut TOKEN_ELEVATION).cast(),
                    std::mem::size_of::<TOKEN_ELEVATION>() as u32,
                    &mut n,
                ) != 0
                    && e.TokenIsElevated != 0;
                if !shell_elevated
                    && DuplicateTokenEx(
                        shell_token,
                        TOKEN_QUERY
                            | TOKEN_DUPLICATE
                            | TOKEN_ASSIGN_PRIMARY
                            | TOKEN_ADJUST_DEFAULT
                            | TOKEN_ADJUST_SESSIONID,
                        std::ptr::null(),
                        SecurityImpersonation,
                        TokenPrimary,
                        &mut token,
                    ) != 0
                {
                    // The token does not carry this process's environment: pass ours (the
                    // SC_OFFLINE_* variables).
                    let mut block: Vec<u16> = Vec::new();
                    for (k, v) in std::env::vars_os() {
                        block.extend(k.encode_wide());
                        block.push(u16::from(b'='));
                        block.extend(v.encode_wide());
                        block.push(0);
                    }
                    block.push(0);
                    if block.len() < 2 {
                        block.push(0);
                    }
                    let exe_w = wide(exe);
                    let mut cmd_w = wide(&format!("\"{exe}\""));
                    let dir_w = wide(dir);
                    let si: STARTUPINFOW = {
                        let mut s: STARTUPINFOW = std::mem::zeroed();
                        s.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
                        s
                    };
                    let mut pi: PROCESS_INFORMATION = std::mem::zeroed();
                    started = CreateProcessWithTokenW(
                        token,
                        0,
                        exe_w.as_ptr(),
                        cmd_w.as_mut_ptr(),
                        CREATE_UNICODE_ENVIRONMENT,
                        block.as_ptr().cast(),
                        dir_w.as_ptr(),
                        &si,
                        &mut pi,
                    ) != 0;
                    if started {
                        CloseHandle(pi.hThread);
                        self.game = Game::Handle(pi.hProcess);
                    }
                }
            }
            if !started {
                err = last_error();
            }
            for h in [token, shell_token, shell_proc] {
                if !h.is_null() {
                    CloseHandle(h);
                }
            }
        }
        (started, shell_elevated, err)
    }
}

impl Drop for RealHost {
    fn drop(&mut self) {
        self.end_session();
        if let Game::Handle(h) = &mut self.game {
            Self::close_handle(h);
        }
    }
}

unsafe extern "system" fn on_ctrl(kind: u32) -> i32 {
    if kind == CTRL_C_EVENT || kind == CTRL_BREAK_EVENT {
        println!("\n[i] Ctrl+C ignored: close the game instead. The mod is removed when it exits.");
        return TRUE;
    }
    0 // window closed / logoff: let it go; the helper removes the mod
}

impl Host for RealHost {
    fn pid(&self) -> u32 {
        // SAFETY: no input.
        unsafe { GetCurrentProcessId() }
    }
    fn env(&self, name: &str) -> Option<String> {
        std::env::var_os(name).map(|v| v.to_string_lossy().into_owned())
    }
    fn set_env(&mut self, name: &str, value: &str) {
        if value.is_empty() {
            std::env::remove_var(name);
        } else {
            std::env::set_var(name, value);
        }
    }
    fn system(&mut self) -> &mut dyn System {
        &mut self.sys
    }
    fn is_elevated(&mut self) -> bool {
        self.sys.is_elevated()
    }
    fn owns_console_input(&mut self) -> bool {
        if std::env::var_os(scol_contract::env::GUI).is_some() {
            return true;
        }
        let mut mode = 0u32;
        // SAFETY: the standard input handle of this process; mode is valid for the call.
        unsafe { GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mut mode) != 0 }
    }
    fn read_line(&mut self) -> Option<String> {
        let mut line = String::new();
        match std::io::stdin().read_line(&mut line) {
            Ok(0) | Err(_) => None,
            Ok(_) => Some(line),
        }
    }

    fn create_session(&mut self, event: &str, with_closed: bool) -> Result<(), u32> {
        let name = wide(event);
        // SAFETY: NUL-terminated name; null attributes mean the default security descriptor.
        self.ready = unsafe { CreateEventW(std::ptr::null(), TRUE, 0, name.as_ptr()) };
        if self.ready.is_null() {
            return Err(last_error());
        }
        if with_closed {
            let closed = wide(&format!("{event}-closed"));
            // SAFETY: as above.
            self.closed = unsafe { CreateEventW(std::ptr::null(), TRUE, 0, closed.as_ptr()) };
            if self.closed.is_null() {
                return Err(last_error());
            }
        }
        Ok(())
    }

    fn start_helper(&mut self, start: &HelperStart) -> Result<(), u32> {
        let me = self_exe();
        if !start.elevated {
            let child = Command::new(&me)
                .raw_arg(&start.args)
                .creation_flags(DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP)
                .stdin(Stdio::null())
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| e.raw_os_error().unwrap_or(2) as u32)?;
            self.helper = child.into_raw_handle();
            return Ok(());
        }
        let file = wide(&me);
        let params = wide(&start.args);
        let verb = wide("runas");
        // SAFETY: zeroed plain-data struct whose size is set; the strings outlive the call.
        let mut sei: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        sei.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = verb.as_ptr();
        sei.lpFile = file.as_ptr();
        sei.lpParameters = params.as_ptr();
        sei.nShow = SW_HIDE;
        // SAFETY: sei is initialised above.
        if unsafe { ShellExecuteExW(&mut sei) } == 0 {
            return Err(last_error());
        }
        self.helper = sei.hProcess;
        Ok(())
    }

    fn wait_ready(&mut self) -> Ready {
        let waits = [self.ready, self.helper];
        // SAFETY: two valid handles.
        let r = unsafe { WaitForMultipleObjects(2, waits.as_ptr(), 0, INFINITE) };
        if r == WAIT_OBJECT_0 {
            Ready::Ready
        } else {
            Ready::HelperGone(exit_code_of(self.helper))
        }
    }
    fn signal_closed(&mut self) {
        if !self.closed.is_null() {
            // SAFETY: an event handle created by create_session.
            unsafe { SetEvent(self.closed) };
        }
    }
    fn wait_helper(&mut self, ms: Option<u32>) -> Option<i32> {
        if self.helper.is_null() {
            return Some(1);
        }
        // SAFETY: a process handle opened by start_helper.
        let r = unsafe { WaitForSingleObject(self.helper, ms.unwrap_or(INFINITE)) };
        (r == WAIT_OBJECT_0).then(|| exit_code_of(self.helper))
    }
    fn helper_finished(&mut self) -> bool {
        // SAFETY: as above.
        self.helper.is_null() || unsafe { WaitForSingleObject(self.helper, 0) } == WAIT_OBJECT_0
    }
    fn end_session(&mut self) {
        Self::close_handle(&mut self.ready);
        Self::close_handle(&mut self.closed);
        Self::close_handle(&mut self.helper);
    }

    fn start_game(&mut self, exe: &str, dir: &str, out: &mut dyn Write) -> Result<(), u32> {
        // Run as administrator (the launcher, or StarCitizen.exe's own compatibility box), Windows treats
        // the game as high integrity, and the Vulkan loader then ignores every environment variable: the
        // game's own "disable this implicit layer" switches stop working, so OBS's and Steam's overlay
        // layers load into it. When this process is elevated, the game is started with the token of the
        // desktop shell (explorer.exe, normal rights) instead. When that cannot be done, the game
        // starts as before, with a warning, rather than not at all.
        if !self.sys.is_elevated() {
            return self.plain(exe, dir);
        }
        let (started, shell_elevated, err) = self.start_with_shell_token(exe, dir);
        if started {
            let _ = write!(
                out,
                "[i] sc-offline was started as administrator; the game was started with normal rights anyway\n    (as administrator, Windows lets overlay layers such as OBS into the game, and they can crash it).\n"
            );
            return Ok(());
        }
        if shell_elevated {
            let _ = write!(
                out,
                "[!] Windows' User Account Control is off, so everything, the game included, runs as administrator.\n    Overlay layers (OBS, Steam) then load into the game and can crash it; close OBS before playing.\n"
            );
        } else {
            let _ = write!(
                out,
                "[!] sc-offline was started as administrator and couldn't start the game with normal rights (error {err});\n    it starts as administrator. If it crashes while loading, start sc-offline.exe normally.\n"
            );
        }
        self.plain(exe, dir)
    }
    fn wait_game(&mut self) {
        match std::mem::replace(&mut self.game, Game::None) {
            Game::None => {}
            Game::Child(mut c) => {
                let _ = c.wait();
            }
            Game::Handle(h) => {
                // SAFETY: the process handle CreateProcessWithTokenW returned; closed once here.
                unsafe {
                    WaitForSingleObject(h, INFINITE);
                    CloseHandle(h);
                }
            }
        }
    }
    fn now_ticks(&mut self) -> u64 {
        crate::now_ticks()
    }
    fn ignore_ctrl_c(&mut self) {
        // SAFETY: on_ctrl has the signature the call wants and lives for the whole process.
        unsafe { SetConsoleCtrlHandler(Some(on_ctrl), TRUE) };
    }
    fn check_update(&mut self, cfg: &scol_config::Config, out: &mut dyn Write) {
        if let Some(hook) = self.update_hook.as_mut() {
            hook(cfg, out);
        }
    }
    fn open_url(&mut self, url: &str) {
        let (verb, file) = (wide("open"), wide(url));
        // SAFETY: NUL-terminated strings that outlive the call.
        unsafe {
            ShellExecuteW(
                std::ptr::null_mut(),
                verb.as_ptr(),
                file.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                SW_SHOWNORMAL,
            )
        };
    }
    fn reveal_file(&mut self, path: &str) {
        let (verb, file, params) = (
            wide("open"),
            wide("explorer.exe"),
            wide(&format!("/select,\"{path}\"")),
        );
        // SAFETY: NUL-terminated strings that outlive the call.
        unsafe {
            ShellExecuteW(
                std::ptr::null_mut(),
                verb.as_ptr(),
                file.as_ptr(),
                params.as_ptr(),
                std::ptr::null(),
                SW_SHOWNORMAL,
            )
        };
    }
    fn delete_logs_elevated(&mut self, channel_dir: &str, since: u64) -> i32 {
        let (verb, file, params) = (
            wide("runas"),
            wide(&self_exe()),
            wide(&format!("--delete-logs \"{channel_dir}\" {since}")),
        );
        // SAFETY: zeroed plain-data struct whose size is set; the strings outlive the call.
        let mut sei: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        sei.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = verb.as_ptr();
        sei.lpFile = file.as_ptr();
        sei.lpParameters = params.as_ptr();
        sei.nShow = SW_HIDE;
        // SAFETY: sei is initialised above; the handle it returns is closed here.
        unsafe {
            if ShellExecuteExW(&mut sei) == 0 || sei.hProcess.is_null() {
                return 1;
            }
            WaitForSingleObject(sei.hProcess, 60_000);
            let code = exit_code_of(sei.hProcess);
            CloseHandle(sei.hProcess);
            code
        }
    }
}

/// The helper's side over real named events and a real parent process.
#[derive(Default)]
pub struct RealSignals {
    parent: Option<HANDLE>,
}

impl Signals for RealSignals {
    fn open_parent(&mut self, pid: u32) -> bool {
        // SAFETY: query-only access to a process by id.
        let h = unsafe { OpenProcess(SYNCHRONIZE, 0, pid) };
        if h.is_null() {
            return false;
        }
        self.parent = Some(h);
        true
    }
    fn close_parent(&mut self) {
        if let Some(h) = self.parent.take() {
            // SAFETY: opened by open_parent and closed once.
            unsafe { CloseHandle(h) };
        }
    }
    fn signal_ready(&mut self, event: &str) {
        let name = wide(event);
        // SAFETY: NUL-terminated name; the handle is closed after use.
        unsafe {
            let h = OpenEventW(EVENT_MODIFY_STATE, 0, name.as_ptr());
            if !h.is_null() {
                SetEvent(h);
                CloseHandle(h);
            }
        }
    }
    fn wait_session_end(&mut self, event: &str) {
        let name = wide(&format!("{event}-closed"));
        // SAFETY: NUL-terminated name; handles are valid for the waits and the event is closed after.
        unsafe {
            let closed = OpenEventW(SYNCHRONIZE, 0, name.as_ptr());
            let mut waits: Vec<HANDLE> = self.parent.iter().copied().collect();
            if !closed.is_null() {
                waits.push(closed);
            }
            if !waits.is_empty() {
                WaitForMultipleObjects(waits.len() as u32, waits.as_ptr(), 0, INFINITE);
            }
            if !closed.is_null() {
                CloseHandle(closed);
            }
        }
    }
}

/// `--helper <op> <Bin64> <launcher pid> <event> <flags> [<allow>]` on this PC. `args` are the arguments
/// after the exe name; `here` is the folder of the exe. Returns the helper's exit code.
pub fn run_helper(args: &[String], here: &str, out: &mut dyn Write) -> i32 {
    let mut sys = RealSystem;
    let mut signals = RealSignals::default();
    helper_main(args, here, &mut sys, &mut signals, out)
}

/// `--delete-logs <channel folder> <since>` (elevated half of the session-log clean-up).
pub fn run_delete_logs(channel_dir: &str, since: &str) -> i32 {
    crate::post::delete_session_logs(channel_dir, since.parse().unwrap_or(0))
}

/// `play`, `install` or `uninstall` on this PC.
pub fn run_command(args: &crate::flow::PlayArgs, here: &str, out: &mut dyn Write) -> i32 {
    run_command_with(args, here, out, None)
}

/// [`run_command`] with the update check of `play` supplied by the caller.
pub fn run_command_with(
    args: &crate::flow::PlayArgs,
    here: &str,
    out: &mut dyn Write,
    update_hook: Option<UpdateHook>,
) -> i32 {
    let mut host = RealHost::new();
    host.update_hook = update_hook;
    crate::flow::run_play(args, &mut host, here, out)
}

fn proc_times(h: HANDLE) -> Option<u64> {
    let (mut c, mut e, mut k, mut u) = (
        FILETIME {
            dwLowDateTime: 0,
            dwHighDateTime: 0,
        },
        FILETIME {
            dwLowDateTime: 0,
            dwHighDateTime: 0,
        },
        FILETIME {
            dwLowDateTime: 0,
            dwHighDateTime: 0,
        },
        FILETIME {
            dwLowDateTime: 0,
            dwHighDateTime: 0,
        },
    );
    // SAFETY: h has query rights; the four out-pointers are valid.
    if unsafe { GetProcessTimes(h, &mut c, &mut e, &mut k, &mut u) } == 0 {
        return None;
    }
    Some((u64::from(c.dwHighDateTime) << 32) | u64::from(c.dwLowDateTime))
}

struct CloseRequest {
    pids: Vec<u32>,
    posted: usize,
}

unsafe extern "system" fn post_close_cb(w: HANDLE, lp: isize) -> i32 {
    // SAFETY: lp is the CloseRequest pointer EnumWindows was given, alive for the whole call.
    let req = unsafe { &mut *(lp as *mut CloseRequest) };
    let mut pid = 0u32;
    // SAFETY: w is a window handle EnumWindows gave; pid is valid.
    unsafe { GetWindowThreadProcessId(w, &mut pid) };
    // SAFETY: as above.
    if unsafe { IsWindowVisible(w) } == 0 {
        return TRUE;
    }
    if req.pids.contains(&pid) && unsafe { PostMessageW(w, WM_CLOSE, 0, 0) } != 0 {
        req.posted += 1;
    }
    TRUE
}

/// The open handles of one target between `begin` and `release`.
#[derive(Default)]
struct Waiter {
    stopped: Option<HANDLE>,
    process: Option<HANDLE>,
}

/// The stop request on this PC (`StopGame`, `EndTargets`): the waiters by pid.
#[derive(Default)]
pub struct RealStop {
    waiters: std::collections::BTreeMap<u32, Waiter>,
    started: Option<std::time::Instant>,
}

impl StopPort for RealStop {
    fn find_game_procs(&mut self, bin: &str) -> Vec<GameProc> {
        use windows_sys::Win32::System::Diagnostics::ToolHelp::{
            CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
            TH32CS_SNAPPROCESS,
        };
        let mut found = Vec::new();
        // SAFETY: plain snapshot request; entry struct is zeroed with its size set.
        unsafe {
            let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if snap as isize == -1 {
                return found;
            }
            let mut pe: PROCESSENTRY32W = std::mem::zeroed();
            pe.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
            let want = format!("{bin}\\{GAME_EXE}");
            let mut ok = Process32FirstW(snap, &mut pe) != 0;
            while ok {
                let end = pe
                    .szExeFile
                    .iter()
                    .position(|&c| c == 0)
                    .unwrap_or(pe.szExeFile.len());
                let name = String::from_utf16_lossy(&pe.szExeFile[..end]);
                if name.eq_ignore_ascii_case(GAME_EXE) {
                    let h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pe.th32ProcessID);
                    if !h.is_null() {
                        if image_is(h, &want) {
                            if let Some(created) = proc_times(h) {
                                found.push(GameProc {
                                    pid: pe.th32ProcessID,
                                    created,
                                });
                            }
                        }
                        CloseHandle(h);
                    }
                }
                ok = Process32NextW(snap, &mut pe) != 0;
            }
            CloseHandle(snap);
        }
        found
    }

    fn begin(&mut self, t: &GameProc) -> Begin {
        let (stop, stopped) = crate::stop::event_names(t.pid);
        let (stop_w, stopped_w) = (wide(&stop), wide(&stopped));
        // SAFETY: NUL-terminated names; every handle opened here is closed here or in release.
        unsafe {
            let h_stop = OpenEventW(EVENT_MODIFY_STATE, 0, stop_w.as_ptr());
            if h_stop.is_null() {
                let e = last_error();
                return if e == ERROR_FILE_NOT_FOUND {
                    Begin::NotPresent
                } else {
                    Begin::Denied(e)
                };
            }
            let h_stopped = OpenEventW(SYNCHRONIZE, 0, stopped_w.as_ptr());
            if h_stopped.is_null() {
                let e = last_error();
                CloseHandle(h_stop);
                return Begin::Denied(e);
            }
            // The process handle ends the wait if the game dies; a reused id (other creation time) counts
            // as gone.
            let mut p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, 0, t.pid);
            if !p.is_null() && proc_times(p) != Some(t.created) {
                CloseHandle(p);
                p = std::ptr::null_mut();
            }
            self.started.get_or_insert_with(std::time::Instant::now);
            SetEvent(h_stop);
            CloseHandle(h_stop);
            self.waiters.insert(
                t.pid,
                Waiter {
                    stopped: Some(h_stopped),
                    process: (!p.is_null()).then_some(p),
                },
            );
        }
        Begin::Signalled
    }

    fn poll(&mut self, t: &GameProc, slice_ms: u32) -> Poll {
        let Some(w) = self.waiters.get(&t.pid) else {
            return Poll::ProcessExited;
        };
        let mut handles: Vec<HANDLE> = Vec::new();
        if let Some(s) = w.stopped {
            handles.push(s);
        }
        if let Some(p) = w.process {
            handles.push(p);
        }
        if handles.is_empty() {
            return Poll::ProcessExited;
        }
        // SAFETY: valid handles owned by this waiter.
        let r =
            unsafe { WaitForMultipleObjects(handles.len() as u32, handles.as_ptr(), 0, slice_ms) };
        let idx = r.wrapping_sub(WAIT_OBJECT_0) as usize;
        if idx >= handles.len() {
            Poll::Pending
        } else if idx == 0 && w.stopped.is_some() {
            Poll::Acknowledged
        } else {
            Poll::ProcessExited
        }
    }

    fn release(&mut self, t: &GameProc) {
        if let Some(w) = self.waiters.remove(&t.pid) {
            for h in [w.stopped, w.process].into_iter().flatten() {
                // SAFETY: opened by begin and closed once.
                unsafe { CloseHandle(h) };
            }
        }
    }

    fn post_close(&mut self, targets: &[GameProc]) -> usize {
        let mut req = CloseRequest {
            pids: targets.iter().map(|t| t.pid).collect(),
            posted: 0,
        };
        // SAFETY: the callback and the request outlive the call.
        unsafe {
            EnumWindows(
                Some(post_close_cb),
                (&mut req as *mut CloseRequest) as isize,
            )
        };
        req.posted
    }

    fn terminate(&mut self, t: &GameProc, bin: &str) -> Result<bool, String> {
        // SAFETY: handle opened here and closed on every path.
        unsafe {
            let h = OpenProcess(
                PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                0,
                t.pid,
            );
            if h.is_null() {
                return Err(format!("cannot open it, error {}", last_error()));
            }
            let mut code = 0u32;
            if GetExitCodeProcess(h, &mut code) != 0 && code != STILL_ACTIVE {
                CloseHandle(h);
                return Ok(false);
            }
            let same = t.created != 0
                && proc_times(h) == Some(t.created)
                && image_is(h, &format!("{bin}\\{GAME_EXE}"));
            let r = if same {
                if TerminateProcess(h, 1) != 0 {
                    Ok(true)
                } else {
                    Err("terminate failed".to_string())
                }
            } else {
                Err("creation time or image no longer matches".to_string())
            };
            CloseHandle(h);
            r
        }
    }

    fn mod_expected(&mut self, bin: &str) -> bool {
        let g = GamePaths::new(bin);
        std::path::Path::new(&g.marker).is_file() || identify(&g.dll).ours
    }

    fn now_ms(&mut self) -> u64 {
        self.started
            .get_or_insert_with(std::time::Instant::now)
            .elapsed()
            .as_millis() as u64
    }

    fn sleep(&mut self, ms: u64) {
        std::thread::sleep(std::time::Duration::from_millis(ms));
    }
}

/// True when the process runs exactly `want` (the full path of the exe).
fn image_is(h: HANDLE, want: &str) -> bool {
    use windows_sys::Win32::System::Threading::QueryFullProcessImageNameW;
    let mut buf = vec![0u16; 1024];
    let mut n = buf.len() as u32;
    // SAFETY: buf is valid for n units; h has query rights.
    if unsafe { QueryFullProcessImageNameW(h, 0, buf.as_mut_ptr(), &mut n) } == 0 {
        return false;
    }
    let got = String::from_utf16_lossy(&buf[..n as usize]).replace('/', "\\");
    got.eq_ignore_ascii_case(&want.replace('/', "\\"))
}

/// Appends one `[stop hh:mm:ss.mmm] text` line to `data\launcher.log` (`StopLog`).
pub fn stop_log(here: &str, text: &str) {
    // The file is opened for append and closed again each time (the play helper appends to it too);
    // without a data folder nothing is written.
    let Ok(f) = std::fs::OpenOptions::new()
        .append(true)
        .open(format!("{here}\\data\\launcher.log"))
    else {
        return;
    };
    let mut w = LogOnly(Some(f));
    let (_, _, _, h, mi, s) = scol_install::fsops::local_time();
    let ms = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.subsec_millis())
        .unwrap_or(0);
    let _ = writeln!(w, "[stop {h:02}:{mi:02}:{s:02}.{ms:03}] {text}");
}
