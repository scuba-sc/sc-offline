// SPDX-License-Identifier: GPL-3.0-only
//! Safe wrapper over the `sco_widgets_view_v1` function table. The table and the client handle are
//! static for the life of the process (see `sco/widgets_view.h`), so the wrapper copies the table
//! and keeps both pointers; it never frees either.
use crate::blob::{self, fixed_str, Panel};
use crate::ffi::*;
use std::ffi::{c_void, CString};

#[derive(Clone, Copy)]
pub struct View {
    table: sco_widgets_view_v1,
    client: *mut c_void,
}

// SAFETY: the table is a block of function pointers and the client handle is an opaque pointer, both
// static for the process. The functions take their own locks; the thread rules are the caller's
// (the menu thread), enforced by Core with SCO_WRONG_THREAD.
unsafe impl Send for View {}
unsafe impl Sync for View {}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Stamps {
    pub global: u64,
    pub list: u64,
    pub drops: u64,
    pub done: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub id: String,
    pub title: String,
    pub owner: String,
    pub flags: u32,
    pub revision: u64,
    pub epoch: u64,
    pub node_count: u32,
}

/// One event as the menu builds it; `epoch` and `node_key` come from the blob node acted on.
#[derive(Debug, Clone, PartialEq)]
pub struct Event {
    pub node_id: u32,
    pub kind: u32,
    pub index: i32,
    pub value: f64,
    pub text: Option<String>,
    pub row: i32,
    pub epoch: u64,
    pub node_key: u64,
}

impl View {
    /// # Safety
    /// `table` must point to a valid `sco_widgets_view_v1` (at least its first 8 bytes readable, and
    /// `size` bytes after that) that stays valid and unchanged for the life of the process, and
    /// `client` must be the handle that goes with it.
    pub unsafe fn new(
        table: *const sco_widgets_view_v1,
        client: *mut c_void,
    ) -> Result<View, String> {
        if table.is_null() || client.is_null() {
            return Err("view table or client is NULL".into());
        }
        let head = std::ptr::read(table as *const [u32; 2]);
        if head[0] < TABLE_SIZE as u32 {
            return Err(format!(
                "view table size {} is below {}",
                head[0], TABLE_SIZE
            ));
        }
        if head[1] >> 16 != WIDGETS_VIEW_VERSION_1_0 >> 16 {
            return Err(format!("view table version {:#x} is not 1.x", head[1]));
        }
        Ok(View {
            table: std::ptr::read(table),
            client,
        })
    }

    pub fn capabilities(&self) -> Result<Vec<u32>, i32> {
        let mut caps = [0u32; VIEW_CAPS_1_0];
        caps[0] = VIEW_CAPS_1_0 as u32;
        let r = unsafe { (self.table.capabilities)(self.client, caps.as_mut_ptr()) };
        if r != SCO_OK {
            return Err(r);
        }
        let n = (caps[0] as usize).min(VIEW_CAPS_1_0);
        Ok(caps[..n].to_vec())
    }

    pub fn stamps(&self) -> Result<Stamps, i32> {
        let mut s = Stamps::default();
        let r = unsafe {
            (self.table.stamps)(
                self.client,
                &mut s.global,
                &mut s.list,
                &mut s.drops,
                &mut s.done,
            )
        };
        if r == SCO_OK {
            Ok(s)
        } else {
            Err(r)
        }
    }

    pub fn enter(&self) -> i32 {
        unsafe { (self.table.menu_thread_enter)(self.client) }
    }

    pub fn leave(&self) -> i32 {
        unsafe { (self.table.menu_thread_leave)(self.client) }
    }

    pub fn list(&self) -> Result<Vec<Entry>, i32> {
        let mut capacity = 16u32;
        for _ in 0..8 {
            let mut out = vec![sco_view_panel_entry::zeroed(); capacity as usize];
            let mut count = capacity;
            let r = unsafe {
                (self.table.list_panels)(
                    self.client,
                    out.as_mut_ptr(),
                    ENTRY_SIZE as u32,
                    &mut count,
                )
            };
            match r {
                SCO_OK => {
                    out.truncate(count as usize);
                    return Ok(out
                        .iter()
                        .map(|e| Entry {
                            id: fixed_str(&e.id),
                            title: fixed_str(&e.title),
                            owner: fixed_str(&e.owner),
                            flags: e.flags,
                            revision: e.revision,
                            epoch: e.epoch,
                            node_count: e.node_count,
                        })
                        .collect());
                }
                SCO_TOO_MANY => capacity = count.max(capacity + 1),
                other => return Err(other),
            }
        }
        Err(SCO_TOO_MANY)
    }

    /// Copies one panel and parses it. The buffer is 8-byte aligned (a `Vec<u64>`).
    pub fn copy(&self, id: &str) -> Result<Panel, CopyError> {
        let cid = CString::new(id).map_err(|_| CopyError::Code(SCO_BAD_ARG))?;
        let mut need = 0u32;
        let r = unsafe {
            (self.table.copy_panel)(self.client, cid.as_ptr(), std::ptr::null_mut(), &mut need)
        };
        if r != SCO_TOO_MANY {
            return Err(CopyError::Code(if r == SCO_OK { SCO_FAILED } else { r }));
        }
        for _ in 0..16 {
            let mut mem = vec![0u64; (need as usize).div_ceil(8) + 1];
            let mut capacity = (mem.len() * 8) as u32;
            let r = unsafe {
                (self.table.copy_panel)(
                    self.client,
                    cid.as_ptr(),
                    mem.as_mut_ptr() as *mut c_void,
                    &mut capacity,
                )
            };
            match r {
                SCO_OK => {
                    // SAFETY: the host wrote `capacity` bytes into `mem`, which holds at least that many.
                    let bytes = unsafe {
                        std::slice::from_raw_parts(mem.as_ptr() as *const u8, capacity as usize)
                    };
                    return blob::parse(bytes).map_err(CopyError::Blob);
                }
                SCO_TOO_MANY => need = capacity,
                other => return Err(CopyError::Code(other)),
            }
        }
        Err(CopyError::Code(SCO_TOO_MANY))
    }

    /// Returns the sequence number on success.
    pub fn submit(&self, panel_id: &str, e: &Event) -> Result<u64, i32> {
        let cid = CString::new(panel_id).map_err(|_| SCO_BAD_ARG)?;
        let ctext = match &e.text {
            Some(t) => Some(CString::new(t.as_str()).map_err(|_| SCO_BAD_ARG)?),
            None => None,
        };
        let raw = sco_view_event {
            size: EVENT_SIZE as u32,
            node_id: e.node_id,
            kind: e.kind,
            index: e.index,
            value: e.value,
            text: ctext.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
            row: e.row,
            _reserved: 0,
            epoch: e.epoch,
            node_key: e.node_key,
        };
        let mut seq = 0u64;
        let r = unsafe { (self.table.submit_event)(self.client, cid.as_ptr(), &raw, &mut seq) };
        if r == SCO_OK {
            Ok(seq)
        } else {
            Err(r)
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum CopyError {
    Code(i32),
    Blob(blob::BlobError),
}

impl std::fmt::Display for CopyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CopyError::Code(c) => write!(f, "copy_panel answered {c}"),
            CopyError::Blob(b) => write!(f, "{b}"),
        }
    }
}
