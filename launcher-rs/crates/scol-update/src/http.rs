// SPDX-License-Identifier: GPL-3.0-only
//! The real [`Fetcher`]: WinHTTP through `windows-sys`. One request per call, redirects are NOT
//! followed (the caller checks every hop against its host lists), only `https`, a byte cap, and a
//! timeout per step (resolve, connect, send, one read) so a long download that keeps moving never
//! times out and only a stall does, like the C++ `HttpGet`.

use std::fs::File;
use std::io::Write;
use std::path::Path;

use scol_catalogue::fetch::{Fetcher, Step};
use windows_sys::Win32::Networking::WinHttp::*;

/// What the C++ launcher sends for the release check and download.
pub const GITHUB_ACCEPT: &str = "application/vnd.github+json";

struct Handle(*mut core::ffi::c_void);
impl Drop for Handle {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: the handle came from a WinHttp open call and is closed once.
            unsafe { WinHttpCloseHandle(self.0) };
        }
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// WinHTTP client settings.
pub struct WinHttp {
    agent: String,
    timeout_ms: u32,
    accept: &'static str,
}

impl WinHttp {
    /// `version` goes into the user agent (`sc-offline/<version>`; GitHub requires one).
    pub fn new(version: &str, timeout_ms: u32) -> WinHttp {
        WinHttp {
            agent: format!("sc-offline/{version}"),
            timeout_ms,
            accept: GITHUB_ACCEPT,
        }
    }

    /// Same, with another `Accept` value for byte fetches (the catalogue index uses `*/*`).
    pub fn with_accept(mut self, accept: &'static str) -> WinHttp {
        self.accept = accept;
        self
    }

    /// One GET. `sink` gets each chunk of a 2xx body.
    fn get(
        &self,
        url: &str,
        accept: &str,
        max_bytes: u64,
        sink: &mut dyn FnMut(&[u8]) -> Result<(), String>,
    ) -> Result<Step<u64>, String> {
        let rest = url
            .strip_prefix("https://")
            .ok_or_else(|| "only https is allowed".to_string())?;
        let (host, path) = match rest.find('/') {
            Some(i) => (&rest[..i], &rest[i..]),
            None => (rest, "/"),
        };
        if host.is_empty() || host.contains([':', '@']) {
            return Err("bad host".into());
        }
        let t = self.timeout_ms.min(i32::MAX as u32) as i32;
        let (agent, host_w, verb, path_w) =
            (wide(&self.agent), wide(host), wide("GET"), wide(path));
        // SAFETY (whole block): every pointer passed below is to a live, NUL-terminated buffer or a
        // valid out parameter; each handle is closed by `Handle::drop`.
        unsafe {
            let session = Handle(WinHttpOpen(
                agent.as_ptr(),
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                std::ptr::null(),
                std::ptr::null(),
                0,
            ));
            if session.0.is_null() {
                return Err("couldn't open WinHTTP".into());
            }
            WinHttpSetTimeouts(session.0, t, t, t, t);
            let conn = Handle(WinHttpConnect(session.0, host_w.as_ptr(), 443, 0));
            if conn.0.is_null() {
                return Err("couldn't connect".into());
            }
            let req = Handle(WinHttpOpenRequest(
                conn.0,
                verb.as_ptr(),
                path_w.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                WINHTTP_FLAG_SECURE,
            ));
            if req.0.is_null() {
                return Err("couldn't open the request".into());
            }
            let never: u32 = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(
                req.0,
                WINHTTP_OPTION_REDIRECT_POLICY,
                &never as *const u32 as *const core::ffi::c_void,
                4,
            );
            let headers = wide(&format!("Accept: {accept}\r\n"));
            if WinHttpSendRequest(req.0, headers.as_ptr(), u32::MAX, std::ptr::null(), 0, 0, 0) == 0
                || WinHttpReceiveResponse(req.0, std::ptr::null_mut()) == 0
            {
                return Err("couldn't reach the server".into());
            }
            let mut status: u32 = 0;
            let mut len: u32 = 4;
            if WinHttpQueryHeaders(
                req.0,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                std::ptr::null(),
                &mut status as *mut u32 as *mut core::ffi::c_void,
                &mut len,
                std::ptr::null_mut(),
            ) == 0
            {
                return Err("no status code".into());
            }
            if matches!(status, 301 | 302 | 303 | 307 | 308) {
                let mut buf = vec![0u16; 2100];
                let mut blen = (buf.len() * 2) as u32;
                if WinHttpQueryHeaders(
                    req.0,
                    WINHTTP_QUERY_LOCATION,
                    std::ptr::null(),
                    buf.as_mut_ptr() as *mut core::ffi::c_void,
                    &mut blen,
                    std::ptr::null_mut(),
                ) == 0
                {
                    return Err(format!("redirect ({status}) without a location"));
                }
                return Ok(Step::Redirect(String::from_utf16_lossy(
                    &buf[..(blen / 2) as usize],
                )));
            }
            if !(200..300).contains(&status) {
                return Err(format!("the server answered {status}"));
            }
            let mut chunk = vec![0u8; 65536];
            let mut total: u64 = 0;
            loop {
                let mut got: u32 = 0;
                if WinHttpReadData(
                    req.0,
                    chunk.as_mut_ptr() as *mut core::ffi::c_void,
                    chunk.len() as u32,
                    &mut got,
                ) == 0
                {
                    return Err("the download stalled or failed".into());
                }
                if got == 0 {
                    break;
                }
                total += got as u64;
                if total > max_bytes {
                    return Err("the reply is over the size cap".into());
                }
                sink(&chunk[..got as usize])?;
            }
            Ok(Step::Done(total))
        }
    }
}

impl Fetcher for WinHttp {
    fn fetch_bytes(&mut self, url: &str, max_bytes: u64) -> Result<Step<Vec<u8>>, String> {
        let mut body = Vec::new();
        let accept = self.accept;
        match self.get(url, accept, max_bytes, &mut |c| {
            body.extend_from_slice(c);
            Ok(())
        })? {
            Step::Done(_) => Ok(Step::Done(body)),
            Step::Redirect(l) => Ok(Step::Redirect(l)),
        }
    }

    fn fetch_to_file(
        &mut self,
        url: &str,
        dest: &Path,
        max_bytes: u64,
    ) -> Result<Step<u64>, String> {
        // The file is created when the first chunk arrives, so a redirect leaves it alone.
        let mut file: Option<File> = None;
        let r = self.get(url, "application/octet-stream", max_bytes, &mut |c| {
            if file.is_none() {
                file = Some(File::create(dest).map_err(|e| format!("can't write {dest:?} ({e})"))?);
            }
            file.as_mut()
                .unwrap()
                .write_all(c)
                .map_err(|e| format!("can't write {dest:?} ({e})"))
        });
        match r {
            Ok(Step::Done(n)) => {
                match file {
                    Some(f) => f.sync_all().map_err(|e| e.to_string())?,
                    None => {
                        File::create(dest).map_err(|e| e.to_string())?;
                    }
                }
                Ok(Step::Done(n))
            }
            Ok(r) => Ok(r),
            Err(e) => {
                drop(file);
                let _ = std::fs::remove_file(dest);
                Err(e)
            }
        }
    }
}

/// The fetcher for the plugin catalogue (`scol_catalogue::fetch::{fetch_index, download_zip}`):
/// the same HTTP code, `Accept: */*`, 30 s per step.
pub fn catalogue_fetcher(version: &str) -> WinHttp {
    WinHttp::new(version, 30_000).with_accept("*/*")
}
