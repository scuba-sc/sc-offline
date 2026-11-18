// SPDX-License-Identifier: GPL-3.0-only
//! Hand-written `extern "C"` mirrors of `sco/widgets_view.h` (sco.widgets.view 1.0). No bindgen, no
//! C compiler. The sizes and offsets are pinned by the layout test at the bottom of this file; the
//! same numbers are asserted in sco-core's `tests/abi_widgets_view.c`.
#![allow(non_camel_case_types)]

use std::ffi::{c_char, c_void};

pub const SCO_OK: i32 = 0;
pub const SCO_UNAVAILABLE: i32 = 1;
pub const SCO_NOT_FOUND: i32 = 2;
pub const SCO_BAD_ARG: i32 = 3;
pub const SCO_CRASHED: i32 = 4;
pub const SCO_WRONG_THREAD: i32 = 5;
pub const SCO_TOO_MANY: i32 = 6;
pub const SCO_FAILED: i32 = 7;

pub const WIDGETS_VIEW_VERSION_1_0: u32 = 0x0001_0000;
pub const BLOB_MAGIC: u32 = 0x4256_4353;
pub const ID_BYTES: usize = 64;

pub const PANEL_HOST_OWNED: u32 = 0x1;
pub const PANEL_TAGGED_EVENTS: u32 = 0x2;

pub const WIDGET_LABEL: u32 = 1;
pub const WIDGET_BUTTON: u32 = 2;
pub const WIDGET_TOGGLE: u32 = 3;
pub const WIDGET_SLIDER: u32 = 4;
pub const WIDGET_COMBO: u32 = 5;
pub const WIDGET_LIST: u32 = 6;
pub const WIDGET_TEXT_INPUT: u32 = 7;
pub const WIDGET_TABLE: u32 = 8;
pub const WIDGET_ROW: u32 = 9;
pub const WIDGET_COLUMN: u32 = 10;
pub const WIDGET_EVENT_PAGE: u32 = 0x100;

pub const WIDGET_DISABLED: u32 = 1;
pub const WIDGET_PASSWORD: u32 = 2;

pub const SEVERITY_NONE: u32 = 0;
pub const SEVERITY_INFO: u32 = 1;
pub const SEVERITY_WARNING: u32 = 2;
pub const SEVERITY_ERROR: u32 = 3;

pub const NUMERIC_NUMBER: u32 = 0;
pub const NUMERIC_INTEGER: u32 = 1;
pub const NUMERIC_INTEGER_PERCENT: u32 = 2;

pub const MAX_TEXT: usize = 255;
pub const MAX_ITEMS: usize = 256;
pub const MAX_COLUMNS: u32 = 16;

pub const VIEW_CAP_COUNT: usize = 0;
pub const VIEW_CAP_FEATURES: usize = 1;
pub const VIEW_CAPS_1_0: usize = 6;

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct sco_view_str {
    pub offset: u32,
    pub length: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct sco_view_panel_entry {
    pub size: u32,
    pub flags: u32,
    pub revision: u64,
    pub epoch: u64,
    pub node_count: u32,
    pub _reserved: u32,
    pub id: [u8; ID_BYTES],
    pub title: [u8; ID_BYTES],
    pub owner: [u8; ID_BYTES],
}

impl sco_view_panel_entry {
    pub const fn zeroed() -> Self {
        Self {
            size: 0,
            flags: 0,
            revision: 0,
            epoch: 0,
            node_count: 0,
            _reserved: 0,
            id: [0; ID_BYTES],
            title: [0; ID_BYTES],
            owner: [0; ID_BYTES],
        }
    }
}

/// One event, menu to plugin. `text` is the one pointer in the interface.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct sco_view_event {
    pub size: u32,
    pub node_id: u32,
    pub kind: u32,
    pub index: i32,
    pub value: f64,
    pub text: *const c_char,
    pub row: i32,
    pub _reserved: u32,
    pub epoch: u64,
    pub node_key: u64,
}

pub type sco_view_client = c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct sco_widgets_view_v1 {
    pub size: u32,
    pub version: u32,
    pub capabilities: unsafe extern "C" fn(*mut sco_view_client, *mut u32) -> i32,
    pub stamps:
        unsafe extern "C" fn(*mut sco_view_client, *mut u64, *mut u64, *mut u64, *mut u64) -> i32,
    pub menu_thread_enter: unsafe extern "C" fn(*mut sco_view_client) -> i32,
    pub menu_thread_leave: unsafe extern "C" fn(*mut sco_view_client) -> i32,
    pub list_panels:
        unsafe extern "C" fn(*mut sco_view_client, *mut sco_view_panel_entry, u32, *mut u32) -> i32,
    pub copy_panel:
        unsafe extern "C" fn(*mut sco_view_client, *const c_char, *mut c_void, *mut u32) -> i32,
    pub submit_event: unsafe extern "C" fn(
        *mut sco_view_client,
        *const c_char,
        *const sco_view_event,
        *mut u64,
    ) -> i32,
    pub dispatch_events: unsafe extern "C" fn(*mut sco_view_client, *mut u32, *mut u32) -> i32,
}

/// Blob layout sizes the reader checks against the producer's stride fields.
pub const HEADER_SIZE: usize = 96;
pub const NODE_SIZE: usize = 120;
pub const ENTRY_SIZE: usize = 224;
pub const EVENT_SIZE: usize = 56;
pub const TABLE_SIZE: usize = 72;

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{offset_of, size_of};

    #[test]
    fn layout_matches_the_header() {
        assert_eq!(size_of::<sco_view_str>(), 8);
        assert_eq!(offset_of!(sco_view_str, length), 4);

        assert_eq!(size_of::<sco_view_panel_entry>(), ENTRY_SIZE);
        assert_eq!(offset_of!(sco_view_panel_entry, flags), 4);
        assert_eq!(offset_of!(sco_view_panel_entry, revision), 8);
        assert_eq!(offset_of!(sco_view_panel_entry, epoch), 16);
        assert_eq!(offset_of!(sco_view_panel_entry, node_count), 24);
        assert_eq!(offset_of!(sco_view_panel_entry, id), 32);
        assert_eq!(offset_of!(sco_view_panel_entry, title), 96);
        assert_eq!(offset_of!(sco_view_panel_entry, owner), 160);

        assert_eq!(size_of::<sco_view_event>(), EVENT_SIZE);
        assert_eq!(offset_of!(sco_view_event, node_id), 4);
        assert_eq!(offset_of!(sco_view_event, kind), 8);
        assert_eq!(offset_of!(sco_view_event, index), 12);
        assert_eq!(offset_of!(sco_view_event, value), 16);
        assert_eq!(offset_of!(sco_view_event, text), 24);
        assert_eq!(offset_of!(sco_view_event, row), 32);
        assert_eq!(offset_of!(sco_view_event, epoch), 40);
        assert_eq!(offset_of!(sco_view_event, node_key), 48);

        assert_eq!(size_of::<sco_widgets_view_v1>(), TABLE_SIZE);
        assert_eq!(offset_of!(sco_widgets_view_v1, version), 4);
        assert_eq!(offset_of!(sco_widgets_view_v1, capabilities), 8);
        assert_eq!(offset_of!(sco_widgets_view_v1, stamps), 16);
        assert_eq!(offset_of!(sco_widgets_view_v1, menu_thread_enter), 24);
        assert_eq!(offset_of!(sco_widgets_view_v1, menu_thread_leave), 32);
        assert_eq!(offset_of!(sco_widgets_view_v1, list_panels), 40);
        assert_eq!(offset_of!(sco_widgets_view_v1, copy_panel), 48);
        assert_eq!(offset_of!(sco_widgets_view_v1, submit_event), 56);
        assert_eq!(offset_of!(sco_widgets_view_v1, dispatch_events), 64);
    }
}
