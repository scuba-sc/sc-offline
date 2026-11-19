// SPDX-License-Identifier: GPL-3.0-only
//! The few Win32 calls the menu window needs, with the same focus and cursor rules as the ImGui
//! menu (`ShowMenu`, `KeepCursorInMenu` in `src/menu.cpp`): the cursor is clipped to the window while
//! it is in front, released while the window is moved or resized, on hide and on focus loss; hiding
//! gives the foreground back to the game window. Every function is a no-op off Windows and returns
//! a harmless default when a call fails. Nothing here touches another process.
#![allow(clippy::missing_safety_doc)]

/// A window rectangle in physical pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub fn width(&self) -> i32 {
        self.right - self.left
    }
    pub fn height(&self) -> i32 {
        self.bottom - self.top
    }
}

#[cfg(windows)]
mod imp {
    use super::Rect;
    use std::sync::atomic::{AtomicIsize, Ordering};
    use windows_sys::Win32::Foundation::{HWND, LPARAM, RECT};
    use windows_sys::Win32::Graphics::Dwm::DwmSetWindowAttribute;
    use windows_sys::Win32::Graphics::Gdi::{
        GetMonitorInfoW, MonitorFromWindow, MONITORINFO, MONITOR_DEFAULTTONEAREST,
    };
    use windows_sys::Win32::System::Threading::GetCurrentThreadId;
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{GetAsyncKeyState, VK_LBUTTON};
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        ClipCursor, EnumThreadWindows, GetForegroundWindow, GetWindowRect, GetWindowTextW,
        IsWindow, IsWindowVisible, SetCursorPos, SetForegroundWindow, SetWindowPos, HWND_TOPMOST,
        SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE,
    };

    /// The menu window, found once on the menu thread by its title.
    static MENU: AtomicIsize = AtomicIsize::new(0);
    static CLIPPED: AtomicIsize = AtomicIsize::new(0);

    unsafe extern "system" fn each(hwnd: HWND, param: LPARAM) -> i32 {
        let want = &*(param as *const (Vec<u16>, AtomicIsize));
        let mut buf = [0u16; 128];
        let n = GetWindowTextW(hwnd, buf.as_mut_ptr(), buf.len() as i32);
        if n > 0 && buf[..n as usize] == want.0[..] {
            want.1.store(hwnd as isize, Ordering::Relaxed);
            return 0;
        }
        1
    }

    /// Finds this thread's window with `title`. Call on the menu thread.
    pub fn find_menu(title: &str) -> bool {
        let found = (
            title.encode_utf16().collect::<Vec<u16>>(),
            AtomicIsize::new(0),
        );
        // SAFETY: the callback only reads the tuple behind `param`, which outlives the call.
        unsafe {
            EnumThreadWindows(
                GetCurrentThreadId(),
                Some(each),
                &found as *const (Vec<u16>, AtomicIsize) as LPARAM,
            );
        }
        let h = found.1.load(Ordering::Relaxed);
        if h != 0 {
            MENU.store(h, Ordering::Relaxed);
        }
        h != 0
    }

    pub fn menu_hwnd() -> isize {
        MENU.load(Ordering::Relaxed)
    }

    fn rect_of(h: HWND) -> Option<Rect> {
        let mut r = RECT {
            left: 0,
            top: 0,
            right: 0,
            bottom: 0,
        };
        // SAFETY: `r` is a valid out parameter; a stale handle makes the call fail.
        let ok = unsafe { IsWindow(h) != 0 && GetWindowRect(h, &mut r) != 0 };
        ok.then_some(Rect {
            left: r.left,
            top: r.top,
            right: r.right,
            bottom: r.bottom,
        })
    }

    pub fn window_rect(hwnd: isize) -> Option<Rect> {
        if hwnd == 0 {
            return None;
        }
        rect_of(hwnd as HWND)
    }

    /// Work area of the monitor `hwnd` is on, in physical pixels.
    pub fn work_area(hwnd: isize) -> Option<Rect> {
        if hwnd == 0 {
            return None;
        }
        // SAFETY: plain queries; MONITORINFO is initialised with its size.
        unsafe {
            let m = MonitorFromWindow(hwnd as HWND, MONITOR_DEFAULTTONEAREST);
            let mut info: MONITORINFO = std::mem::zeroed();
            info.cbSize = std::mem::size_of::<MONITORINFO>() as u32;
            if GetMonitorInfoW(m, &mut info) == 0 {
                return None;
            }
            let r = info.rcWork;
            Some(Rect {
                left: r.left,
                top: r.top,
                right: r.right,
                bottom: r.bottom,
            })
        }
    }

    pub fn is_foreground(hwnd: isize) -> bool {
        // SAFETY: no pointers.
        hwnd != 0
            && unsafe {
                GetForegroundWindow() as isize == hwnd && IsWindowVisible(hwnd as HWND) != 0
            }
    }

    pub fn left_button_down() -> bool {
        // SAFETY: no pointers.
        unsafe { GetAsyncKeyState(VK_LBUTTON as i32) as u16 & 0x8000 != 0 }
    }

    /// Clips the cursor to the menu window (only when it moved or resized since the last clip).
    pub fn clip_to_menu() {
        let h = menu_hwnd();
        let Some(r) = rect_of(h as HWND) else { return };
        let packed = ((r.left as i64 & 0xFFFF)
            | ((r.top as i64 & 0xFFFF) << 16)
            | ((r.right as i64 & 0xFFFF) << 32)
            | ((r.bottom as i64 & 0xFFFF) << 48)) as isize
            | 1;
        if CLIPPED.load(Ordering::Relaxed) == packed {
            return;
        }
        let rc = RECT {
            left: r.left,
            top: r.top,
            right: r.right,
            bottom: r.bottom,
        };
        // SAFETY: `rc` is a valid rectangle for the call.
        unsafe { ClipCursor(&rc) };
        CLIPPED.store(packed, Ordering::Relaxed);
    }

    pub fn release_clip() {
        if CLIPPED.swap(0, Ordering::Relaxed) != 0 {
            // SAFETY: NULL releases the clip.
            unsafe { ClipCursor(std::ptr::null()) };
        }
    }

    pub fn is_clipped() -> bool {
        CLIPPED.load(Ordering::Relaxed) != 0
    }

    /// Brings the menu window to the front with the cursor in its middle.
    pub fn bring_to_front() {
        let h = menu_hwnd() as HWND;
        let Some(r) = rect_of(h) else { return };
        // SAFETY: handle checked by `rect_of`.
        unsafe {
            SetWindowPos(
                h,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
            );
            SetForegroundWindow(h);
            SetCursorPos(r.left + r.width() / 2, r.top + r.height() / 2);
        }
    }

    pub fn focus(hwnd: isize) {
        if hwnd != 0 {
            // SAFETY: a stale handle makes the call fail.
            unsafe {
                if IsWindow(hwnd as HWND) != 0 {
                    SetForegroundWindow(hwnd as HWND);
                }
            }
        }
    }

    /// Same frame attributes as `StyleMenuFrame` in menu.cpp: dark title area, square corners, the
    /// theme border green. Windows 10 and Wine do not know them; every failure is ignored.
    pub fn style_frame() {
        let h = menu_hwnd() as HWND;
        if h.is_null() {
            return;
        }
        let dark: i32 = 1;
        let corner: i32 = 1; // DWMWCP_DONOTROUND
        let border: u32 = 0x0051_7F4F; // COLORREF of 0x4F7F51
                                       // SAFETY: each pointer is to a local of the stated size.
        unsafe {
            DwmSetWindowAttribute(h, 20, &dark as *const i32 as *const _, 4);
            DwmSetWindowAttribute(h, 33, &corner as *const i32 as *const _, 4);
            DwmSetWindowAttribute(h, 34, &border as *const u32 as *const _, 4);
        }
    }
}

#[cfg(not(windows))]
mod imp {
    use super::Rect;
    pub fn find_menu(_t: &str) -> bool {
        false
    }
    pub fn menu_hwnd() -> isize {
        0
    }
    pub fn window_rect(_h: isize) -> Option<Rect> {
        None
    }
    pub fn work_area(_h: isize) -> Option<Rect> {
        None
    }
    pub fn is_foreground(_h: isize) -> bool {
        false
    }
    pub fn left_button_down() -> bool {
        false
    }
    pub fn clip_to_menu() {}
    pub fn release_clip() {}
    pub fn is_clipped() -> bool {
        false
    }
    pub fn bring_to_front() {}
    pub fn focus(_h: isize) {}
    pub fn style_frame() {}
}

pub use imp::*;
