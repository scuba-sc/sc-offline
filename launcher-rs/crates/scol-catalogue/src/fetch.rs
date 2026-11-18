// SPDX-License-Identifier: GPL-3.0-only
//! Fetching, behind a trait. This crate has no network code: the launcher implements [`Fetcher`]
//! (WinHTTP, one request per call, no automatic redirects, byte cap, per-read timeouts) and tests implement it from local files.
//!
//! A fetch happens only on an explicit player action, in the launcher process, never while the game
//! runs, and never when `check_updates = off`. Those gates belong to the caller.

use std::path::Path;

use crate::error::Error;
use crate::index::{Entry, MAX_INDEX_BYTES};

/// The sig file is one short line.
pub const MAX_SIG_BYTES: u64 = 512;

/// Longest redirect chain [`fetch_index`] and [`download_zip`] follow.
pub const MAX_REDIRECTS: usize = 2;
/// Longest redirect target accepted (signed CDN links carry long queries).
pub const MAX_REDIRECT_URL: usize = 2048;

/// What one request returned.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Step<T> {
    /// A 2xx answer, fully read (and within the byte cap).
    Done(T),
    /// A 301, 302, 303, 307 or 308 answer with this `Location` value, exactly as sent.
    Redirect(String),
}

/// Contract for implementors.
///
/// A fetcher makes exactly ONE request per call and never follows a redirect itself. A redirect is
/// returned as [`Step::Redirect`]; the functions of this module then check the target (https only,
/// host on the allow lists, see [`check_redirect_url`]) and call again, at most [`MAX_REDIRECTS`]
/// times. This keeps the host check on every hop in code that tests can reach. Any other non-2xx
/// answer is an `Err`. A fetcher that follows redirects on its own breaks this contract.
///
/// GitHub release assets answer 302 to a signed link on a second host, so callers pass that host
/// as `redirect_hosts`. It is trusted for the final hop only: bytes from it are still checked
/// against the signed index (size and SHA-256).
pub trait Fetcher {
    /// GET `url` into memory. Must stop with an error once more than `max_bytes` arrive.
    fn fetch_bytes(&mut self, url: &str, max_bytes: u64) -> Result<Step<Vec<u8>>, String>;
    /// GET `url` into the file `dest` (created, or replaced; untouched on a redirect). Must stop
    /// with an error once more than `max_bytes` arrive. Returns the number of bytes written.
    fn fetch_to_file(
        &mut self,
        url: &str,
        dest: &Path,
        max_bytes: u64,
    ) -> Result<Step<u64>, String>;
}

/// A redirect target: `https://<host>/<path>` with a host from `allowed_hosts` or
/// `redirect_hosts`, no user info, no port, no fragment, no space, control character or
/// backslash. A query is allowed here (signed CDN links need one). Relative targets are refused.
pub fn check_redirect_url(
    url: &str,
    allowed_hosts: &[&str],
    redirect_hosts: &[&str],
) -> Result<(), Error> {
    let bad = |m: &str| Error::BadUrl(format!("redirect: {m}"));
    if url.len() > MAX_REDIRECT_URL {
        return Err(bad("target is too long"));
    }
    let rest = url
        .strip_prefix("https://")
        .ok_or_else(|| bad("must be an absolute https:// URL"))?;
    if rest
        .bytes()
        .any(|c| c <= 0x20 || c == 0x7f || matches!(c, b'\\' | b'#'))
    {
        return Err(bad(
            "contains a space, control character, backslash or fragment",
        ));
    }
    let end = rest.find(['/', '?']).unwrap_or(rest.len());
    let host = &rest[..end];
    if host.is_empty() || host.contains('@') || host.contains(':') || !rest[end..].starts_with('/')
    {
        return Err(bad("host must be a bare name followed by a path"));
    }
    let host = host.to_ascii_lowercase();
    if !allowed_hosts
        .iter()
        .chain(redirect_hosts)
        .any(|h| h.eq_ignore_ascii_case(&host))
    {
        return Err(Error::BadUrl(format!(
            "redirect to host {host}, which is not on the allowed lists"
        )));
    }
    Ok(())
}

/// Request `url` (checked against `allowed_hosts` first), following at most [`MAX_REDIRECTS`]
/// redirects, each target checked with [`check_redirect_url`] before it is requested.
fn follow<T>(
    url: &str,
    allowed_hosts: &[&str],
    redirect_hosts: &[&str],
    mut call: impl FnMut(&str) -> Result<Step<T>, String>,
) -> Result<T, Error> {
    check_url(url, allowed_hosts)?;
    let mut current = url.to_string();
    let mut hops = 0;
    loop {
        match call(&current).map_err(Error::Fetch)? {
            Step::Done(v) => return Ok(v),
            Step::Redirect(next) => {
                if hops >= MAX_REDIRECTS {
                    return Err(Error::BadUrl("too many redirects".into()));
                }
                hops += 1;
                check_redirect_url(&next, allowed_hosts, redirect_hosts)?;
                current = next;
            }
        }
    }
}

/// `https://<host>/<path>` with a host from `allowed_hosts` (compared in lower case), no user
/// info, no port, no query, no fragment, no backslash.
pub fn check_url(url: &str, allowed_hosts: &[&str]) -> Result<(), Error> {
    let bad = |m: &str| Error::BadUrl(m.to_string());
    let rest = url
        .strip_prefix("https://")
        .ok_or_else(|| bad("must start with https://"))?;
    if rest
        .bytes()
        .any(|c| c <= 0x20 || c == 0x7f || matches!(c, b'\\' | b'?' | b'#'))
    {
        return Err(bad(
            "contains a space, control character, backslash, query or fragment",
        ));
    }
    let (host, path) = rest.split_once('/').ok_or_else(|| bad("has no path"))?;
    if host.is_empty() || host.contains('@') || host.contains(':') {
        return Err(bad("host must be a bare name (no user info, no port)"));
    }
    if path.is_empty() {
        return Err(bad("has no path"));
    }
    let host = host.to_ascii_lowercase();
    if !allowed_hosts.iter().any(|h| h.eq_ignore_ascii_case(&host)) {
        return Err(Error::BadUrl(format!(
            "host {host} is not on the allowed list"
        )));
    }
    Ok(())
}

/// Download `index.json` and `index.json.sig`. The signature text must be UTF-8.
/// The two URLs must be on `allowed_hosts`; redirects may also go to `redirect_hosts`.
pub fn fetch_index(
    f: &mut dyn Fetcher,
    index_url: &str,
    sig_url: &str,
    allowed_hosts: &[&str],
    redirect_hosts: &[&str],
) -> Result<(Vec<u8>, String), Error> {
    check_url(index_url, allowed_hosts)?;
    check_url(sig_url, allowed_hosts)?;
    let bytes = follow(index_url, allowed_hosts, redirect_hosts, |u| {
        f.fetch_bytes(u, MAX_INDEX_BYTES as u64)
    })?;
    if bytes.len() > MAX_INDEX_BYTES {
        return Err(Error::TooLarge("index download over the cap".into()));
    }
    let sig = follow(sig_url, allowed_hosts, redirect_hosts, |u| {
        f.fetch_bytes(u, MAX_SIG_BYTES)
    })?;
    if sig.len() as u64 > MAX_SIG_BYTES {
        return Err(Error::TooLarge("signature download over the cap".into()));
    }
    let sig = String::from_utf8(sig).map_err(|_| Error::SigFormat("not UTF-8".into()))?;
    Ok((bytes, sig))
}

/// Download the zip of `entry` into `dest`. The cap is the size the signed index declares, so a
/// server cannot send more than was signed for. The caller then runs
/// [`crate::install::stage_download`].
pub fn download_zip(
    f: &mut dyn Fetcher,
    entry: &Entry,
    dest: &Path,
    allowed_hosts: &[&str],
    redirect_hosts: &[&str],
) -> Result<(), Error> {
    let n = follow(&entry.zip_url, allowed_hosts, redirect_hosts, |u| {
        f.fetch_to_file(u, dest, entry.zip_size)
    })?;
    if n > entry.zip_size {
        return Err(Error::TooLarge(
            "zip download over the size in the index".into(),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn urls() {
        let hosts = ["github.com"];
        assert!(check_url(
            "https://github.com/scuba-sc/x/releases/download/v1/a.zip",
            &hosts
        )
        .is_ok());
        assert!(check_url("https://GitHub.com/a", &hosts).is_ok());
        for bad in [
            "http://github.com/a",
            "https://github.com",
            "https://github.com/",
            "https://evil.example/a",
            "https://github.com@evil.example/a",
            "https://github.com:443/a",
            "https://github.com/a?x=1",
            "https://github.com/a#f",
            "https://github.com/a b",
            "https://github.com\\@evil/a",
            "https://github.com.evil.example/a",
            "ftp://github.com/a",
        ] {
            assert_eq!(
                check_url(bad, &hosts).unwrap_err().code(),
                "bad-url",
                "{bad}"
            );
        }
    }
}
