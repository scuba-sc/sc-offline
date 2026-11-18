// SPDX-License-Identifier: GPL-3.0-only
//! A small, strict zip reader for release archives. Stored and deflate only; no zip64, no
//! encryption, no multi-disk. Every entry name is checked before anything is written, and
//! extraction stops at the size the caller allows, so a lying header cannot fill the disk.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

use flate2::read::DeflateDecoder;
use flate2::Crc;

/// Most entries an archive may hold.
pub const MAX_ENTRIES: usize = 6000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub name: String,
    pub method: u16,
    pub crc32: u32,
    pub comp_size: u64,
    pub size: u64,
    pub offset: u64,
}

impl Entry {
    pub fn is_dir(&self) -> bool {
        self.name.ends_with('/')
    }
}

pub struct Archive {
    file: File,
    pub entries: Vec<Entry>,
}

fn u16le(b: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([b[at], b[at + 1]])
}
fn u32le(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]])
}

/// An entry name that is safe to extract: relative, `/`-separated, no `..`, `.` or empty part, no
/// `\`, `:` or control character. A trailing `/` (a directory) is allowed.
fn name_ok(name: &str) -> bool {
    if name.is_empty() || name.len() > 400 || name.starts_with('/') {
        return false;
    }
    if name.contains(['\\', ':']) || name.bytes().any(|c| c < 0x20 || c == 0x7f) {
        return false;
    }
    let body = name.strip_suffix('/').unwrap_or(name);
    body.split('/')
        .all(|p| !p.is_empty() && p != "." && p != ".." && !p.ends_with(['.', ' ']))
}

impl Archive {
    /// Open and index `path`. Err is a short reason.
    pub fn open(path: &Path) -> Result<Archive, String> {
        let mut file = File::open(path).map_err(|e| format!("can't open the zip ({e})"))?;
        let len = file.metadata().map_err(|e| e.to_string())?.len();
        if len < 22 {
            return Err("not a zip file".into());
        }
        let tail_len = len.min(22 + 65_535) as usize;
        let mut tail = vec![0u8; tail_len];
        file.seek(SeekFrom::Start(len - tail_len as u64))
            .and_then(|_| file.read_exact(&mut tail))
            .map_err(|e| e.to_string())?;
        let eocd = (0..=tail_len - 22)
            .rev()
            .find(|&i| tail[i..i + 4] == [0x50, 0x4b, 0x05, 0x06])
            .ok_or("not a zip file")?;
        let e = &tail[eocd..];
        let (disk, cd_disk) = (u16le(e, 4), u16le(e, 6));
        let (n_here, n_all) = (u16le(e, 8) as usize, u16le(e, 10) as usize);
        let (cd_size, cd_off) = (u32le(e, 12) as u64, u32le(e, 16) as u64);
        if disk != 0 || cd_disk != 0 || n_here != n_all {
            return Err("multi-disk zips are refused".into());
        }
        if n_all == 0xFFFF || cd_size == 0xFFFF_FFFF || cd_off == 0xFFFF_FFFF {
            return Err("zip64 archives are refused".into());
        }
        if n_all > MAX_ENTRIES {
            return Err("the zip holds too many entries".into());
        }
        if cd_off.checked_add(cd_size).is_none_or(|end| end > len) || cd_size > (16 << 20) {
            return Err("the zip's directory is out of range".into());
        }
        let mut cd = vec![0u8; cd_size as usize];
        file.seek(SeekFrom::Start(cd_off))
            .and_then(|_| file.read_exact(&mut cd))
            .map_err(|e| e.to_string())?;
        let mut entries = Vec::with_capacity(n_all);
        let mut at = 0usize;
        for _ in 0..n_all {
            if at + 46 > cd.len() || cd[at..at + 4] != [0x50, 0x4b, 0x01, 0x02] {
                return Err("the zip's directory is damaged".into());
            }
            let flags = u16le(&cd, at + 8);
            let method = u16le(&cd, at + 10);
            let crc32 = u32le(&cd, at + 16);
            let comp = u32le(&cd, at + 20) as u64;
            let size = u32le(&cd, at + 24) as u64;
            let (nl, xl, cl) = (
                u16le(&cd, at + 28) as usize,
                u16le(&cd, at + 30) as usize,
                u16le(&cd, at + 32) as usize,
            );
            let offset = u32le(&cd, at + 42) as u64;
            let name_end = at + 46 + nl;
            if name_end + xl + cl > cd.len() {
                return Err("the zip's directory is damaged".into());
            }
            let name = std::str::from_utf8(&cd[at + 46..name_end])
                .map_err(|_| "an entry name isn't UTF-8".to_string())?
                .to_string();
            at = name_end + xl + cl;
            if flags & 1 != 0 {
                return Err(format!("{name} is encrypted"));
            }
            if comp == 0xFFFF_FFFF || size == 0xFFFF_FFFF || offset == 0xFFFF_FFFF {
                return Err("zip64 entries are refused".into());
            }
            if !name_ok(&name) {
                return Err(format!("refused entry name {name:?}"));
            }
            if method != 0 && method != 8 {
                return Err(format!("{name} uses an unsupported compression method"));
            }
            if method == 0 && comp != size {
                return Err(format!("{name} has inconsistent sizes"));
            }
            if offset >= cd_off {
                return Err(format!("{name} points outside the file area"));
            }
            if entries
                .iter()
                .any(|x: &Entry| x.name.eq_ignore_ascii_case(&name))
            {
                return Err(format!("{name} appears twice"));
            }
            entries.push(Entry {
                name,
                method,
                crc32,
                comp_size: comp,
                size,
                offset,
            });
        }
        Ok(Archive { file, entries })
    }

    pub fn find(&self, name: &str) -> Option<usize> {
        self.entries
            .iter()
            .position(|e| !e.is_dir() && e.name.eq_ignore_ascii_case(name))
    }

    /// Write the entry's bytes to `sink`. The entry's declared size must be at most `max`; more
    /// bytes than declared, fewer, or a wrong CRC-32 is an error. Returns the bytes written.
    pub fn extract(&mut self, idx: usize, sink: &mut dyn Write, max: u64) -> Result<u64, String> {
        let e = self.entries[idx].clone();
        if e.size > max {
            return Err(format!("{} is over the allowed size", e.name));
        }
        let mut lh = [0u8; 30];
        self.file
            .seek(SeekFrom::Start(e.offset))
            .and_then(|_| self.file.read_exact(&mut lh))
            .map_err(|_| format!("{} is cut short", e.name))?;
        if lh[..4] != [0x50, 0x4b, 0x03, 0x04] {
            return Err(format!("{} has a damaged header", e.name));
        }
        let skip = u16le(&lh, 26) as u64 + u16le(&lh, 28) as u64;
        self.file
            .seek(SeekFrom::Start(e.offset + 30 + skip))
            .map_err(|e| e.to_string())?;
        let src = (&mut self.file).take(e.comp_size);
        let mut reader: Box<dyn Read> = if e.method == 0 {
            Box::new(src)
        } else {
            Box::new(DeflateDecoder::new(src))
        };
        // Read at most size + 1 so an entry that inflates past its header is caught.
        let mut limited = (&mut reader).take(e.size + 1);
        let mut crc = Crc::new();
        let mut buf = vec![0u8; 256 * 1024];
        let mut total = 0u64;
        loop {
            let n = limited
                .read(&mut buf)
                .map_err(|err| format!("{} is damaged ({err})", e.name))?;
            if n == 0 {
                break;
            }
            total += n as u64;
            if total > e.size {
                return Err(format!("{} is larger than its header says", e.name));
            }
            crc.update(&buf[..n]);
            sink.write_all(&buf[..n])
                .map_err(|err| format!("couldn't write {} ({err})", e.name))?;
        }
        if total != e.size {
            return Err(format!("{} is shorter than its header says", e.name));
        }
        if crc.sum() != e.crc32 {
            return Err(format!("{} fails its CRC-32", e.name));
        }
        Ok(total)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names() {
        for ok in ["a", "a/b.txt", "dir/", "a/b/"] {
            assert!(name_ok(ok), "{ok}");
        }
        for bad in [
            "", "/a", "../a", "a/../b", "a//b", "a\\b", "c:x", "a/./b", "a./b", "x\u{1}",
        ] {
            assert!(!name_ok(bad), "{bad:?}");
        }
    }
}
