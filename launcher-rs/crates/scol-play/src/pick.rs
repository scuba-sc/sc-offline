// SPDX-License-Identifier: GPL-3.0-only
//! The game folder picker: Windows' own folder dialog (`IFileOpenDialog` with `FOS_PICKFOLDERS`), the last
//! resort when no game was found (`PickFolder` in `launcher/launcher.cpp`). The play flow reaches it through
//! [`crate::host::Host::pick_folder`], so tests script the answer; this file is the real dialog and nothing
//! calls it in a test.
//!
//! `windows-sys` has the Win32 functions but not the COM interfaces, so the two interfaces used here
//! (`IFileOpenDialog`, `IShellItem`) are declared as their v-tables, with the slot order of `shobjidl_core.h`.
//! Each call says why it is sound. Windows only.

#![allow(unsafe_code)]

use std::ffi::c_void;
use windows_sys::core::{GUID, HRESULT};
use windows_sys::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, CoTaskMemFree, CoUninitialize, CLSCTX_INPROC_SERVER,
    COINIT_APARTMENTTHREADED, COINIT_DISABLE_OLE1DDE,
};
use windows_sys::Win32::UI::Shell::{
    FOS_FORCEFILESYSTEM, FOS_PATHMUSTEXIST, FOS_PICKFOLDERS, SIGDN_FILESYSPATH,
};

/// `CLSID_FileOpenDialog`
const CLSID_FILE_OPEN_DIALOG: GUID = GUID::from_u128(0xDC1C5A9C_E88A_4DDE_A5A1_60F82A20AEF7);
/// `IID_IFileOpenDialog`
const IID_IFILE_OPEN_DIALOG: GUID = GUID::from_u128(0xD57C7288_D4AD_4768_BE02_9D969532D960);

/// The title of the dialog, as the C++ launcher words it.
pub const TITLE: &str = "sc-offline: pick your StarCitizen folder (the one with LIVE in it)";

/// `IUnknown` slots 0 to 2, then the slots of `IModalWindow` and `IFileDialog` this file calls. Slots that
/// are not called are typed as plain pointers so the layout is right.
#[repr(C)]
struct FileDialogVtbl {
    query_interface: usize,
    add_ref: usize,
    release: unsafe extern "system" fn(*mut c_void) -> u32,
    /// IModalWindow::Show(hwndOwner)
    show: unsafe extern "system" fn(*mut c_void, *mut c_void) -> HRESULT,
    set_file_types: usize,
    set_file_type_index: usize,
    get_file_type_index: usize,
    advise: usize,
    unadvise: usize,
    set_options: unsafe extern "system" fn(*mut c_void, u32) -> HRESULT,
    get_options: unsafe extern "system" fn(*mut c_void, *mut u32) -> HRESULT,
    set_default_folder: usize,
    set_folder: usize,
    get_folder: usize,
    get_current_selection: usize,
    set_file_name: usize,
    get_file_name: usize,
    set_title: unsafe extern "system" fn(*mut c_void, *const u16) -> HRESULT,
    set_ok_button_label: usize,
    set_file_name_label: usize,
    get_result: unsafe extern "system" fn(*mut c_void, *mut *mut c_void) -> HRESULT,
}

/// `IUnknown` and `IShellItem` slots up to `GetDisplayName`.
#[repr(C)]
struct ShellItemVtbl {
    query_interface: usize,
    add_ref: usize,
    release: unsafe extern "system" fn(*mut c_void) -> u32,
    bind_to_handler: usize,
    get_parent: usize,
    get_display_name: unsafe extern "system" fn(*mut c_void, i32, *mut *mut u16) -> HRESULT,
}

fn succeeded(hr: HRESULT) -> bool {
    hr >= 0
}

/// A COM object pointer; the first word of the object is its v-table.
unsafe fn vtbl<T>(obj: *mut c_void) -> &'static T {
    // SAFETY (caller): obj is a live COM interface pointer of the type T describes.
    unsafe { &**(obj as *mut *const T) }
}

/// Shows the folder dialog and returns the folder the player picked, `None` when cancelled or when the
/// dialog cannot be made. `owner` is the window handle to stay on top of (the console window of a double-clicked
/// exe), 0 for none. Blocks until the dialog closes.
pub fn pick_folder(owner: isize) -> Option<String> {
    let mut result = None;
    // SAFETY: every pointer below comes from the COM calls that made it and is released once on every path;
    // the v-table types match `shobjidl_core.h`; the wide title is NUL-terminated and outlives its call.
    unsafe {
        let init = CoInitializeEx(
            std::ptr::null(),
            (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) as u32,
        );
        let mut dlg: *mut c_void = std::ptr::null_mut();
        let made = CoCreateInstance(
            &CLSID_FILE_OPEN_DIALOG,
            std::ptr::null_mut(),
            CLSCTX_INPROC_SERVER,
            &IID_IFILE_OPEN_DIALOG,
            &mut dlg,
        );
        if succeeded(made) && !dlg.is_null() {
            let v: &FileDialogVtbl = vtbl(dlg);
            let mut opts = 0u32;
            (v.get_options)(dlg, &mut opts);
            (v.set_options)(
                dlg,
                opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST,
            );
            let title: Vec<u16> = TITLE.encode_utf16().chain(Some(0)).collect();
            (v.set_title)(dlg, title.as_ptr());
            if succeeded((v.show)(dlg, owner as *mut c_void)) {
                let mut item: *mut c_void = std::ptr::null_mut();
                if succeeded((v.get_result)(dlg, &mut item)) && !item.is_null() {
                    let iv: &ShellItemVtbl = vtbl(item);
                    let mut path: *mut u16 = std::ptr::null_mut();
                    if succeeded((iv.get_display_name)(item, SIGDN_FILESYSPATH, &mut path))
                        && !path.is_null()
                    {
                        let mut n = 0usize;
                        while *path.add(n) != 0 {
                            n += 1;
                        }
                        result = Some(String::from_utf16_lossy(std::slice::from_raw_parts(
                            path, n,
                        )));
                        CoTaskMemFree(path as *const c_void);
                    }
                    (iv.release)(item);
                }
            }
            (v.release)(dlg);
        }
        if succeeded(init) {
            CoUninitialize();
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_ids_are_the_shell_ones() {
        // {DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7} and {D57C7288-D4AD-4768-BE02-9D969532D960}
        assert_eq!(CLSID_FILE_OPEN_DIALOG.data1, 0xDC1C5A9C);
        assert_eq!(CLSID_FILE_OPEN_DIALOG.data4[7], 0xF7);
        assert_eq!(IID_IFILE_OPEN_DIALOG.data1, 0xD57C7288);
        assert_eq!(IID_IFILE_OPEN_DIALOG.data4[7], 0x60);
    }

    /// Opens the real dialog, finds its window by the title, closes it as a Cancel would, and checks that the
    /// call returns `None` without a crash: the interface calls up to `Show` work against the real shell.
    /// Run by hand (`cargo test -p scol-play --lib real_dialog -- --ignored`): a dialog is on screen for a moment.
    #[test]
    #[ignore = "opens a real folder dialog for a moment"]
    fn real_dialog_opens_and_cancels() {
        use windows_sys::Win32::UI::WindowsAndMessaging::{FindWindowW, PostMessageW, WM_CLOSE};
        let title: Vec<u16> = TITLE.encode_utf16().chain(Some(0)).collect();
        let closer = std::thread::spawn(move || {
            for _ in 0..100 {
                std::thread::sleep(std::time::Duration::from_millis(50));
                // SAFETY: NUL-terminated title; a window handle from FindWindowW is only posted a message.
                let w = unsafe { FindWindowW(std::ptr::null(), title.as_ptr()) };
                if !w.is_null() {
                    unsafe { PostMessageW(w, WM_CLOSE, 0, 0) };
                    return true;
                }
            }
            false
        });
        let picked = pick_folder(0);
        assert!(closer.join().unwrap(), "the dialog never showed its title");
        assert_eq!(picked, None, "closing the dialog is a cancel");
    }

    /// Types a folder into the dialog's name box and presses its button, then checks that the folder comes back
    /// through `GetResult` and `GetDisplayName`: the rest of the interface calls. By hand, like the one above.
    #[test]
    #[ignore = "opens a real folder dialog for a moment"]
    fn real_dialog_returns_the_typed_folder() {
        use windows_sys::Win32::Foundation::{HWND, LPARAM};
        use windows_sys::Win32::UI::WindowsAndMessaging::{
            EnumChildWindows, FindWindowW, GetClassNameW, GetDlgItem, PostMessageW, SendMessageW,
            BM_CLICK, WM_SETTEXT,
        };
        let want = std::env::temp_dir();
        let want_s = want.to_string_lossy().into_owned();
        std::fs::create_dir_all(&want).unwrap();
        let title: Vec<u16> = TITLE.encode_utf16().chain(Some(0)).collect();
        let typed: Vec<u16> = want_s.encode_utf16().chain(Some(0)).collect();
        unsafe extern "system" fn find_edit(w: HWND, lp: LPARAM) -> i32 {
            let mut buf = [0u16; 64];
            // SAFETY: buf is valid for 64 units; lp is the out pointer EnumChildWindows was given.
            let n = unsafe { GetClassNameW(w, buf.as_mut_ptr(), 64) } as usize;
            if String::from_utf16_lossy(&buf[..n]) == "Edit" {
                unsafe { *(lp as *mut HWND) = w };
                return 0;
            }
            1
        }
        let driver = std::thread::spawn(move || {
            for _ in 0..100 {
                std::thread::sleep(std::time::Duration::from_millis(50));
                // SAFETY: NUL-terminated strings that outlive the calls; handles come from the system.
                unsafe {
                    let dlg = FindWindowW(std::ptr::null(), title.as_ptr());
                    if dlg.is_null() {
                        continue;
                    }
                    std::thread::sleep(std::time::Duration::from_millis(300));
                    let mut edit: HWND = std::ptr::null_mut();
                    EnumChildWindows(dlg, Some(find_edit), (&mut edit as *mut HWND) as LPARAM);
                    if edit.is_null() {
                        PostMessageW(
                            dlg,
                            windows_sys::Win32::UI::WindowsAndMessaging::WM_CLOSE,
                            0,
                            0,
                        );
                        return false;
                    }
                    SendMessageW(edit, WM_SETTEXT, 0, typed.as_ptr() as LPARAM);
                    let ok = GetDlgItem(dlg, 1);
                    SendMessageW(ok, BM_CLICK, 0, 0);
                    return true;
                }
            }
            false
        });
        let picked = pick_folder(0);
        assert!(driver.join().unwrap(), "could not drive the dialog");
        let got = picked.expect("a folder came back");
        let norm = |s: &str| s.trim_end_matches('\\').to_ascii_lowercase();
        assert_eq!(norm(&got), norm(&want_s));
    }

    #[test]
    fn the_vtable_slots_are_where_the_header_puts_them() {
        let word = std::mem::size_of::<usize>();
        // Show is slot 3; SetOptions 9, GetOptions 10; SetTitle 17; GetResult 20
        assert_eq!(std::mem::offset_of!(FileDialogVtbl, show), 3 * word);
        assert_eq!(std::mem::offset_of!(FileDialogVtbl, set_options), 9 * word);
        assert_eq!(std::mem::offset_of!(FileDialogVtbl, get_options), 10 * word);
        assert_eq!(std::mem::offset_of!(FileDialogVtbl, set_title), 17 * word);
        assert_eq!(std::mem::offset_of!(FileDialogVtbl, get_result), 20 * word);
        // IShellItem::GetDisplayName is slot 5
        assert_eq!(
            std::mem::offset_of!(ShellItemVtbl, get_display_name),
            5 * word
        );
    }
}
