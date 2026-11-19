// SPDX-License-Identifier: GPL-3.0-only
//! Stand-in for StarCitizen.exe in tests. It never is the game and touches nothing of it.
//!
//! Default: it sleeps for `SCOL_STANDIN_MS` milliseconds (default 300) and exits with `SCOL_STANDIN_EXIT`
//! (default 0), opening no window.
//!
//! With `SCOL_STANDIN_STOPPABLE=1` it behaves like the game with the mod loaded, as far as the launcher's Stop
//! command can tell: it creates the two named events of the stop channel for its own process id
//! (`Local\sc-offline-stop-<pid>` and `Local\sc-offline-stopped-<pid>`), opens one small visible window, and
//! waits. When the stop event is set it does its "exit work" (writes `standin-stopped.txt` beside the exe, so a
//! test can see that the mod's save ran before the window was closed), then sets the stopped event. When the
//! window gets WM_CLOSE it exits with code 0. `SCOL_STANDIN_NO_ACK=1` leaves the stopped event unset (a mod that
//! hangs); `SCOL_STANDIN_IGNORE_CLOSE=1` ignores WM_CLOSE (a game that does not quit, so the End step has work to
//! do). `SCOL_STANDIN_MS` still bounds the run (default for this mode: 10 minutes).

#[cfg(windows)]
mod stoppable {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
    use windows_sys::Win32::System::Threading::{CreateEventW, SetEvent, WaitForSingleObject};
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, PeekMessageW,
        PostQuitMessage, RegisterClassW, TranslateMessage, MSG, PM_REMOVE, WM_CLOSE, WM_DESTROY,
        WNDCLASSW, WS_OVERLAPPEDWINDOW, WS_VISIBLE,
    };

    fn wide(s: &str) -> Vec<u16> {
        std::ffi::OsStr::new(s)
            .encode_wide()
            .chain(Some(0))
            .collect()
    }

    fn flag(name: &str) -> bool {
        std::env::var(name).is_ok_and(|v| v == "1")
    }

    unsafe extern "system" fn proc(hwnd: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
        // SAFETY: plain Win32 window procedure calls with the arguments the system gave.
        unsafe {
            match msg {
                WM_CLOSE => {
                    if !flag("SCOL_STANDIN_IGNORE_CLOSE") {
                        DestroyWindow(hwnd);
                    }
                    0
                }
                WM_DESTROY => {
                    PostQuitMessage(0);
                    0
                }
                _ => DefWindowProcW(hwnd, msg, wp, lp),
            }
        }
    }

    pub fn run(ms: u64, code: i32) -> ! {
        let pid = std::process::id();
        let (stop_name, stopped_name) = (
            wide(&format!("Local\\sc-offline-stop-{pid}")),
            wide(&format!("Local\\sc-offline-stopped-{pid}")),
        );
        let here = std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|d| d.to_path_buf()))
            .unwrap_or_default();
        // SAFETY: NUL-terminated names and class text that outlive the calls; the handles live as long as the
        // process; the window class and window are the stand-in's own.
        unsafe {
            let stop = CreateEventW(std::ptr::null(), 1, 0, stop_name.as_ptr());
            let stopped = CreateEventW(std::ptr::null(), 1, 0, stopped_name.as_ptr());
            let class = wide("scol_standin_window");
            let wc = WNDCLASSW {
                style: 0,
                lpfnWndProc: Some(proc),
                cbClsExtra: 0,
                cbWndExtra: 0,
                hInstance: std::ptr::null_mut(),
                hIcon: std::ptr::null_mut(),
                hCursor: std::ptr::null_mut(),
                hbrBackground: std::ptr::null_mut(),
                lpszMenuName: std::ptr::null(),
                lpszClassName: class.as_ptr(),
            };
            RegisterClassW(&wc);
            let title = wide("stand-in game (test)");
            CreateWindowExW(
                0,
                class.as_ptr(),
                title.as_ptr(),
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                100,
                100,
                240,
                80,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null(),
            );
            let started = std::time::Instant::now();
            let mut acked = false;
            let mut msg: MSG = std::mem::zeroed();
            loop {
                while PeekMessageW(&mut msg, std::ptr::null_mut(), 0, 0, PM_REMOVE) != 0 {
                    if msg.message == windows_sys::Win32::UI::WindowsAndMessaging::WM_QUIT {
                        std::process::exit(0);
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if !acked && WaitForSingleObject(stop, 0) == 0 {
                    acked = true;
                    let _ = std::fs::write(here.join("standin-stopped.txt"), b"exit work ran\n");
                    if !flag("SCOL_STANDIN_NO_ACK") {
                        SetEvent(stopped);
                    }
                }
                if started.elapsed().as_millis() as u64 >= ms {
                    std::process::exit(code);
                }
                std::thread::sleep(std::time::Duration::from_millis(10));
            }
        }
    }
}

fn main() {
    let stoppable = std::env::var("SCOL_STANDIN_STOPPABLE").is_ok_and(|v| v == "1");
    let ms: u64 = std::env::var("SCOL_STANDIN_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(if stoppable { 600_000 } else { 300 });
    let code: i32 = std::env::var("SCOL_STANDIN_EXIT")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    #[cfg(windows)]
    if stoppable {
        stoppable::run(ms, code);
    }
    std::thread::sleep(std::time::Duration::from_millis(ms));
    std::process::exit(code);
}
