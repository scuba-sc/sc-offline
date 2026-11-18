// SPDX-License-Identifier: GPL-3.0-only
//! The two zip jobs this crate needs and `scol-plugins` does not do: writing a stored zip from a
//! folder (so a local folder goes through the same verifier as a zip), and extracting a zip that
//! `scol_plugins::verify` already accepted. The extractor trusts nothing it reads: it only writes
//! the files named in the verifier's report, checks every segment of every path again, and checks
//! the SHA-256 and size of what it wrote against the report.

use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use flate2::read::DeflateDecoder;
use scol_plugins::sha256::{to_hex, Sha256};
use scol_plugins::verify::{Report, MAX_ENTRIES, MAX_FILE_BYTES, MAX_TOTAL_BYTES};

fn crc_table() -> [u32; 256] {
    let mut t = [0u32; 256];
    for (i, slot) in t.iter_mut().enumerate() {
        let mut c = i as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 {
                0xEDB8_8320 ^ (c >> 1)
            } else {
                c >> 1
            };
        }
        *slot = c;
    }
    t
}

pub fn crc32(data: &[u8]) -> u32 {
    let t = crc_table();
    let mut c = 0xFFFF_FFFFu32;
    for &b in data {
        c = t[((c ^ b as u32) & 0xFF) as usize] ^ (c >> 8);
    }
    !c
}

/// One file to pack: path inside the zip (forward slashes) and its bytes.
pub struct Member {
    pub name: String,
    pub data: Vec<u8>,
}

/// Write a stored (uncompressed) zip. Plain regular-file entries, UTF-8 names, fixed 1980 time so
/// the same input gives the same bytes.
pub fn write_stored_zip(path: &Path, members: &[Member]) -> Result<(), String> {
    let mut out: Vec<u8> = Vec::new();
    let mut central: Vec<u8> = Vec::new();
    for m in members {
        let crc = crc32(&m.data);
        let name = m.name.as_bytes();
        let offset = u32::try_from(out.len()).map_err(|_| "archive too large".to_string())?;
        let size = u32::try_from(m.data.len()).map_err(|_| "file too large".to_string())?;
        let nlen = u16::try_from(name.len()).map_err(|_| "name too long".to_string())?;
        out.extend_from_slice(b"PK\x03\x04");
        out.extend_from_slice(&20u16.to_le_bytes()); // version needed
        out.extend_from_slice(&0x0800u16.to_le_bytes()); // UTF-8 names
        out.extend_from_slice(&0u16.to_le_bytes()); // stored
        out.extend_from_slice(&0u16.to_le_bytes()); // time
        out.extend_from_slice(&0x0021u16.to_le_bytes()); // date 1980-01-01
        out.extend_from_slice(&crc.to_le_bytes());
        out.extend_from_slice(&size.to_le_bytes());
        out.extend_from_slice(&size.to_le_bytes());
        out.extend_from_slice(&nlen.to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(name);
        out.extend_from_slice(&m.data);

        central.extend_from_slice(b"PK\x01\x02");
        central.extend_from_slice(&((3u16 << 8) | 20).to_le_bytes()); // made by Unix, 2.0
        central.extend_from_slice(&20u16.to_le_bytes());
        central.extend_from_slice(&0x0800u16.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes());
        central.extend_from_slice(&0x0021u16.to_le_bytes());
        central.extend_from_slice(&crc.to_le_bytes());
        central.extend_from_slice(&size.to_le_bytes());
        central.extend_from_slice(&size.to_le_bytes());
        central.extend_from_slice(&nlen.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes()); // extra
        central.extend_from_slice(&0u16.to_le_bytes()); // comment
        central.extend_from_slice(&0u16.to_le_bytes()); // disk
        central.extend_from_slice(&0u16.to_le_bytes()); // internal attrs
        central.extend_from_slice(&(0o100_644u32 << 16).to_le_bytes()); // regular file 0644
        central.extend_from_slice(&offset.to_le_bytes());
        central.extend_from_slice(name);
    }
    let cd_offset = u32::try_from(out.len()).map_err(|_| "archive too large".to_string())?;
    let cd_size = u32::try_from(central.len()).map_err(|_| "archive too large".to_string())?;
    let count = u16::try_from(members.len()).map_err(|_| "too many files".to_string())?;
    out.extend_from_slice(&central);
    out.extend_from_slice(b"PK\x05\x06");
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&count.to_le_bytes());
    out.extend_from_slice(&count.to_le_bytes());
    out.extend_from_slice(&cd_size.to_le_bytes());
    out.extend_from_slice(&cd_offset.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    let mut f = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|e| format!("cannot create {}: {e}", path.display()))?;
    f.write_all(&out)
        .and_then(|_| f.flush())
        .map_err(|e| format!("cannot write {}: {e}", path.display()))
}

#[cfg(windows)]
fn is_reparse(meta: &std::fs::Metadata) -> bool {
    use std::os::windows::fs::MetadataExt;
    meta.file_attributes() & 0x400 != 0
}
#[cfg(not(windows))]
fn is_reparse(_: &std::fs::Metadata) -> bool {
    false
}

/// Collect the regular files of `dir` as zip members under `<prefix>/`. Symbolic links, junctions
/// and other non-regular files are refused. A top-level file named `skip_top` (the disabled marker)
/// is left out. Sorted by name.
pub fn collect_folder(dir: &Path, prefix: &str, skip_top: &str) -> Result<Vec<Member>, String> {
    let mut members = Vec::new();
    let mut total = 0u64;
    walk(dir, "", prefix, skip_top, &mut members, &mut total)?;
    members.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(members)
}

fn walk(
    dir: &Path,
    rel: &str,
    prefix: &str,
    skip_top: &str,
    out: &mut Vec<Member>,
    total: &mut u64,
) -> Result<(), String> {
    let rd = std::fs::read_dir(dir).map_err(|e| format!("cannot read {}: {e}", dir.display()))?;
    for entry in rd {
        let entry = entry.map_err(|e| format!("cannot read {}: {e}", dir.display()))?;
        let name = entry
            .file_name()
            .into_string()
            .map_err(|_| format!("a name in {} is not valid Unicode", dir.display()))?;
        let meta = std::fs::symlink_metadata(entry.path())
            .map_err(|e| format!("cannot stat {}: {e}", entry.path().display()))?;
        let rel_name = if rel.is_empty() {
            name.clone()
        } else {
            format!("{rel}/{name}")
        };
        if meta.file_type().is_symlink() || is_reparse(&meta) {
            return Err(format!("{rel_name} is a link; links are not installed"));
        }
        if meta.is_dir() {
            walk(&entry.path(), &rel_name, prefix, skip_top, out, total)?;
        } else if meta.is_file() {
            if rel.is_empty() && name.eq_ignore_ascii_case(skip_top) {
                continue;
            }
            if meta.len() > MAX_FILE_BYTES {
                return Err(format!("{rel_name} is over {MAX_FILE_BYTES} bytes"));
            }
            *total += meta.len();
            if *total > MAX_TOTAL_BYTES || out.len() as u64 >= MAX_ENTRIES {
                return Err("the folder is over the size or file count limits".into());
            }
            let data =
                std::fs::read(entry.path()).map_err(|e| format!("cannot read {rel_name}: {e}"))?;
            out.push(Member {
                name: format!("{prefix}/{rel_name}"),
                data,
            });
        } else {
            return Err(format!("{rel_name} is not a regular file"));
        }
    }
    Ok(())
}

struct CdEntry {
    name: Vec<u8>,
    method: u16,
    csize: u64,
    usize_: u64,
    header_offset: u64,
}

fn le16(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}
fn le32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

fn io(e: std::io::Error) -> String {
    format!("read error: {e}")
}

fn read_central(f: &mut File, size: u64) -> Result<Vec<CdEntry>, String> {
    let tail_len = size.min(22 + 65_535);
    f.seek(SeekFrom::Start(size - tail_len)).map_err(io)?;
    let mut tail = vec![0u8; tail_len as usize];
    f.read_exact(&mut tail).map_err(io)?;
    let pos = tail
        .windows(4)
        .rposition(|w| w == b"PK\x05\x06")
        .ok_or("no end-of-central-directory record")?;
    let eocd = &tail[pos..];
    if eocd.len() < 22 {
        return Err("short end-of-central-directory record".into());
    }
    let count = le16(eocd, 10) as usize;
    let cd_size = le32(eocd, 12) as u64;
    let cd_off = le32(eocd, 16) as u64;
    if cd_size > 8 * 1024 * 1024 || cd_off.checked_add(cd_size).is_none_or(|e| e > size) {
        return Err("central directory out of range".into());
    }
    f.seek(SeekFrom::Start(cd_off)).map_err(io)?;
    let mut cd = vec![0u8; cd_size as usize];
    f.read_exact(&mut cd).map_err(io)?;
    let mut out = Vec::with_capacity(count);
    let mut p = 0usize;
    for _ in 0..count {
        if p + 46 > cd.len() || &cd[p..p + 4] != b"PK\x01\x02" {
            return Err("bad central directory entry".into());
        }
        let nlen = le16(&cd, p + 28) as usize;
        let elen = le16(&cd, p + 30) as usize;
        let clen = le16(&cd, p + 32) as usize;
        let end = p + 46 + nlen + elen + clen;
        if end > cd.len() {
            return Err("central directory entry runs past the end".into());
        }
        out.push(CdEntry {
            name: cd[p + 46..p + 46 + nlen].to_vec(),
            method: le16(&cd, p + 10),
            csize: le32(&cd, p + 20) as u64,
            usize_: le32(&cd, p + 24) as u64,
            header_offset: le32(&cd, p + 42) as u64,
        });
        p = end;
    }
    Ok(out)
}

/// A relative path from the report: every segment must be a plain, safe file name.
fn safe_segments(rel: &str) -> Result<Vec<&str>, String> {
    let segs: Vec<&str> = rel.split('/').collect();
    for s in &segs {
        let bad = s.is_empty()
            || *s == "."
            || *s == ".."
            || s.ends_with('.')
            || s.ends_with(' ')
            || s.chars().any(|c| {
                c.is_control() || matches!(c, '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|')
            });
        if bad {
            return Err(format!("unsafe path {rel:?}"));
        }
    }
    Ok(segs)
}

/// Extract every file the verifier reported into `dest`. `zip` is the staged copy that was
/// verified. Returns the relative paths written.
pub fn extract_verified(zip: &Path, report: &Report, dest: &Path) -> Result<Vec<String>, String> {
    let mut f = File::open(zip).map_err(|e| format!("cannot open {}: {e}", zip.display()))?;
    let size = f.metadata().map_err(io)?.len();
    let entries = read_central(&mut f, size)?;
    let prefix = format!("{}/", report.plugin.folder);
    let mut written = Vec::new();
    for art in &report.artifacts {
        let rel = art
            .path
            .strip_prefix(&prefix)
            .ok_or_else(|| format!("{} is outside the plugin folder", art.path))?;
        let segs = safe_segments(rel)?;
        let e = entries
            .iter()
            .find(|e| e.name == art.path.as_bytes())
            .ok_or_else(|| format!("{} is in the report but not in the archive", art.path))?;
        if e.usize_ != art.size_bytes {
            return Err(format!("{}: size differs from the report", art.path));
        }
        f.seek(SeekFrom::Start(e.header_offset)).map_err(io)?;
        let mut lh = [0u8; 30];
        f.read_exact(&mut lh).map_err(io)?;
        if &lh[0..4] != b"PK\x03\x04" {
            return Err(format!("{}: bad local header", art.path));
        }
        let skip = le16(&lh, 26) as i64 + le16(&lh, 28) as i64;
        f.seek(SeekFrom::Current(skip)).map_err(io)?;
        let mut target: PathBuf = dest.to_path_buf();
        for s in &segs {
            target.push(s);
        }
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|e| format!("cannot create {}: {e}", parent.display()))?;
        }
        let mut out = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&target)
            .map_err(|e| format!("cannot create {}: {e}", target.display()))?;
        let raw = (&mut f).take(e.csize);
        let mut src: Box<dyn Read> = match e.method {
            0 => {
                if e.csize != e.usize_ {
                    return Err(format!("{}: stored size mismatch", art.path));
                }
                Box::new(raw)
            }
            8 => Box::new(DeflateDecoder::new(raw)),
            m => return Err(format!("{}: unsupported method {m}", art.path)),
        };
        let mut limited = (&mut src).take(e.usize_ + 1);
        let mut h = Sha256::new();
        let mut n = 0u64;
        let mut buf = vec![0u8; 64 * 1024];
        loop {
            let k = limited.read(&mut buf).map_err(io)?;
            if k == 0 {
                break;
            }
            h.update(&buf[..k]);
            out.write_all(&buf[..k])
                .map_err(|e| format!("cannot write {}: {e}", target.display()))?;
            n += k as u64;
        }
        out.flush().map_err(io)?;
        drop(out);
        if n != art.size_bytes || to_hex(&h.finish()) != art.sha256 {
            return Err(format!(
                "{}: extracted bytes differ from the report",
                art.path
            ));
        }
        written.push(rel.to_string());
    }
    Ok(written)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
    }

    #[test]
    fn segments() {
        assert!(safe_segments("a/b.txt").is_ok());
        for bad in [
            "", "a//b", "../x", "a/./b", "a\\b", "c:x", "a/b.", "a/b ", "a/\u{1}",
        ] {
            assert!(safe_segments(bad).is_err(), "{bad:?}");
        }
    }
}
