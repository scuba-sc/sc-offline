// SPDX-License-Identifier: GPL-3.0-only
//! Static verifier for a single-folder plugin zip. Rust port of the sco-core SDK tool
//! `sco-plugin-verify.py`.
//!
//! The archive is never extracted and no plugin code is run or loaded. Members are streamed
//! through a hash. This checks archive structure, `plugin.ini` syntax, folder identity and the
//! presence of the declared entry. It does not check runtime compatibility, provider behaviour,
//! native-code safety or trust. Not run in game.
//!
//! The limits and rules below are copied from the Python tool. Where the Python tool leans on
//! Python's `zipfile` module (name decoding, local header checks, CRC check, overlap check, the
//! unicode path extra field) this port does the same by hand. The shared corpus under
//! `launcher-rs/tests/zip-corpus` is run by both tools and they must agree on it.
//!
//! Known, deliberate differences (none is a way to get a bad archive accepted):
//!
//! * A ZIP64 end-of-central-directory locator is always refused. Python follows a valid ZIP64
//!   record, or ignores a broken one.
//! * If Python's `zipfile` would pick a different end-of-central-directory record than the
//!   bounded preflight did, the archive is refused.
//! * Deflate data is inflated only as far as the declared size (plus a small look-ahead for an
//!   empty declared size). Python looks a little further ahead before it stops.

use std::collections::{HashMap, HashSet};
use std::fmt;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use crate::manifest::{self, Kind};
use crate::sha256::{to_hex, Sha256};
use crate::unicode;

pub const MAX_ARCHIVE_BYTES: u64 = 256 * 1024 * 1024;
pub const MAX_CENTRAL_DIRECTORY_BYTES: u64 = 8 * 1024 * 1024;
pub const MAX_ENTRIES: u64 = 4096;
pub const MAX_FILE_BYTES: u64 = 128 * 1024 * 1024;
pub const MAX_TOTAL_BYTES: u64 = 512 * 1024 * 1024;
pub const MAX_COMPRESSION_RATIO: u64 = 200;
pub const MAX_EMPTY_FILE_COMPRESSED_BYTES: u64 = 64;
pub const MAX_PATH_BYTES: usize = 1024;
pub const MAX_SEGMENT_BYTES: usize = 255;
pub const MAX_COLLISION_PREFIX_CHARS: u64 = 4 * 1024 * 1024;
pub const MAX_MANIFEST_BYTES: u64 = 16 * 1024;

const CHUNK_BYTES: usize = 1024 * 1024;
const EOCD_LEN: usize = 22;
const EOCD_SIG: &[u8; 4] = b"PK\x05\x06";
const ZIP64_LOCATOR_SIG: &[u8; 4] = b"PK\x06\x07";
const CENTRAL_LEN: usize = 46;
const CENTRAL_SIG: &[u8; 4] = b"PK\x01\x02";
const LOCAL_LEN: usize = 30;
const LOCAL_SIG: &[u8; 4] = b"PK\x03\x04";
/// Highest "version needed to extract" Python's zipfile accepts.
const MAX_EXTRACT_VERSION: u8 = 63;
const FLAG_UTF8: u16 = 0x0800;
/// Output the inflater may produce for an empty declared size before it stops. Python reads at
/// least this much ahead, so a broken stream is seen there too.
const EMPTY_FILE_LOOKAHEAD: u64 = 4096;

const WINDOWS_RESERVED: &[&str] = &[
    "CON",
    "PRN",
    "AUX",
    "NUL",
    "CONIN$",
    "CONOUT$",
    "COM1",
    "COM2",
    "COM3",
    "COM4",
    "COM5",
    "COM6",
    "COM7",
    "COM8",
    "COM9",
    "LPT1",
    "LPT2",
    "LPT3",
    "LPT4",
    "LPT5",
    "LPT6",
    "LPT7",
    "LPT8",
    "LPT9",
    "COM\u{b9}",
    "COM\u{b2}",
    "COM\u{b3}",
    "LPT\u{b9}",
    "LPT\u{b2}",
    "LPT\u{b3}",
];

// POSIX file type bits as the high half of the ZIP external attributes.
const S_IFMT: u32 = 0o170000;
const S_IFLNK: u32 = 0o120000;
const S_IFREG: u32 = 0o100000;
const S_IFDIR: u32 = 0o040000;

/// Machine-readable reason for a refusal. The text form is stable, so tests and the corpus
/// expected-results file can name it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Code {
    NotRegularFile,
    CannotOpen,
    ArchiveTooLarge,
    ArchiveEmpty,
    TooShort,
    NoEndRecord,
    MultiDisk,
    Zip64,
    TooManyEntries,
    CentralDirectoryTooLarge,
    PrependedData,
    CentralDirectoryTruncated,
    CentralDirectoryRecord,
    EntryCountMismatch,
    EndRecordAmbiguous,
    ExtractVersion,
    NameEncoding,
    NulInName,
    CorruptExtra,
    UnicodePathExtra,
    UnsafePath,
    Symlink,
    SpecialEntry,
    ModeMismatch,
    DosAttributes,
    CollisionBudget,
    PathCollision,
    DuplicatePath,
    FileAndDirectory,
    FileUsedAsDirectory,
    Encrypted,
    Compression,
    LocalHeaderOffset,
    SharedLocalHeader,
    DirectoryNotEmpty,
    FileTooLarge,
    TotalTooLarge,
    CompressionRatio,
    EmptyFileData,
    NotOnePluginFolder,
    RootNotFolder,
    ManifestMissing,
    ManifestTooLarge,
    LocalHeader,
    Overlap,
    MemberRead,
    ManifestInvalid,
    ManifestIdMismatch,
    ManifestEntryEncoding,
    EntryMissing,
    ArchiveChanged,
    TotalMismatch,
    Io,
}

impl Code {
    pub fn as_str(self) -> &'static str {
        match self {
            Code::NotRegularFile => "not-regular-file",
            Code::CannotOpen => "cannot-open",
            Code::ArchiveTooLarge => "archive-too-large",
            Code::ArchiveEmpty => "archive-empty",
            Code::TooShort => "too-short",
            Code::NoEndRecord => "no-end-record",
            Code::MultiDisk => "multi-disk",
            Code::Zip64 => "zip64",
            Code::TooManyEntries => "too-many-entries",
            Code::CentralDirectoryTooLarge => "central-directory-too-large",
            Code::PrependedData => "prepended-data",
            Code::CentralDirectoryTruncated => "central-directory-truncated",
            Code::CentralDirectoryRecord => "central-directory-record",
            Code::EntryCountMismatch => "entry-count-mismatch",
            Code::EndRecordAmbiguous => "end-record-ambiguous",
            Code::ExtractVersion => "extract-version",
            Code::NameEncoding => "name-encoding",
            Code::NulInName => "nul-in-name",
            Code::CorruptExtra => "corrupt-extra",
            Code::UnicodePathExtra => "unicode-path-extra",
            Code::UnsafePath => "unsafe-path",
            Code::Symlink => "symlink",
            Code::SpecialEntry => "special-entry",
            Code::ModeMismatch => "mode-mismatch",
            Code::DosAttributes => "dos-attributes",
            Code::CollisionBudget => "collision-budget",
            Code::PathCollision => "path-collision",
            Code::DuplicatePath => "duplicate-path",
            Code::FileAndDirectory => "file-and-directory",
            Code::FileUsedAsDirectory => "file-used-as-directory",
            Code::Encrypted => "encrypted",
            Code::Compression => "compression",
            Code::LocalHeaderOffset => "local-header-offset",
            Code::SharedLocalHeader => "shared-local-header",
            Code::DirectoryNotEmpty => "directory-not-empty",
            Code::FileTooLarge => "file-too-large",
            Code::TotalTooLarge => "total-too-large",
            Code::CompressionRatio => "compression-ratio",
            Code::EmptyFileData => "empty-file-data",
            Code::NotOnePluginFolder => "not-one-plugin-folder",
            Code::RootNotFolder => "root-not-folder",
            Code::ManifestMissing => "manifest-missing",
            Code::ManifestTooLarge => "manifest-too-large",
            Code::LocalHeader => "local-header",
            Code::Overlap => "overlap",
            Code::MemberRead => "member-read",
            Code::ManifestInvalid => "manifest-invalid",
            Code::ManifestIdMismatch => "manifest-id-mismatch",
            Code::ManifestEntryEncoding => "manifest-entry-encoding",
            Code::EntryMissing => "entry-missing",
            Code::ArchiveChanged => "archive-changed",
            Code::TotalMismatch => "total-mismatch",
            Code::Io => "io",
        }
    }
}

/// An archive failed a documented static rule.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VerifyError {
    pub code: Code,
    pub message: String,
}

impl fmt::Display for VerifyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "plugin archive refused: {}", self.message)
    }
}

impl std::error::Error for VerifyError {}

fn refuse<T>(code: Code, message: impl Into<String>) -> Result<T, VerifyError> {
    Err(VerifyError {
        code,
        message: message.into(),
    })
}

fn io_err(what: &str, e: std::io::Error) -> VerifyError {
    VerifyError {
        code: Code::Io,
        message: format!("{what}: {e}"),
    }
}

/// Facts about the archive file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ArchiveInfo {
    pub name: String,
    pub size_bytes: u64,
    pub sha256: String,
}

/// Facts about the plugin the archive declares.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginInfo {
    pub id: String,
    pub folder: String,
    pub kind: Kind,
    pub declared_api: String,
    pub entry: Option<String>,
    pub requires: Vec<String>,
    pub manifest_path: String,
    pub manifest_sha256: String,
}

/// One regular file in the archive.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Artifact {
    pub path: String,
    pub size_bytes: u64,
    pub sha256: String,
}

/// Static evidence for a verified archive.
#[derive(Debug, Clone, PartialEq)]
pub struct Report {
    pub archive: ArchiveInfo,
    pub plugin: PluginInfo,
    pub file_count: u64,
    pub total_uncompressed_bytes: u64,
    /// Sorted by path.
    pub artifacts: Vec<Artifact>,
    /// The parsed manifest, including `[settings]`.
    pub manifest: manifest::Manifest,
}

// ---- byte helpers ---------------------------------------------------------------------------

fn le16(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

fn le32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

/// Last start index `p` with `hay[p..p + needle.len()] == needle` and `p + needle.len() <= end`.
fn rfind(hay: &[u8], needle: &[u8], end: usize) -> Option<usize> {
    let end = end.min(hay.len());
    if end < needle.len() {
        return None;
    }
    (0..=end - needle.len())
        .rev()
        .find(|&p| &hay[p..p + needle.len()] == needle)
}

fn read_at<R: Read + Seek>(r: &mut R, offset: u64, len: u64) -> Result<Vec<u8>, VerifyError> {
    r.seek(SeekFrom::Start(offset))
        .map_err(|e| io_err("seek in archive", e))?;
    let mut out = Vec::new();
    r.by_ref()
        .take(len)
        .read_to_end(&mut out)
        .map_err(|e| io_err("read archive", e))?;
    Ok(out)
}

fn hash_stream<R: Read + Seek>(r: &mut R) -> Result<String, VerifyError> {
    r.seek(SeekFrom::Start(0))
        .map_err(|e| io_err("seek in archive", e))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; CHUNK_BYTES];
    loop {
        let n = r.read(&mut buf).map_err(|e| io_err("read archive", e))?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    r.seek(SeekFrom::Start(0))
        .map_err(|e| io_err("seek in archive", e))?;
    Ok(to_hex(&h.finish()))
}

// ---- preflight ------------------------------------------------------------------------------

struct Preflight {
    directory_offset: u64,
    entries: u64,
    central: Vec<u8>,
}

/// Bound central-directory allocation before anything builds per-entry state.
fn preflight<R: Read + Seek>(r: &mut R, archive_size: u64) -> Result<Preflight, VerifyError> {
    if archive_size < EOCD_LEN as u64 {
        return refuse(Code::TooShort, "file is too short to be a ZIP archive");
    }
    let tail_size = archive_size.min(EOCD_LEN as u64 + 0xFFFF);
    let tail_start = archive_size - tail_size;
    let tail = read_at(r, tail_start, tail_size)?;
    let mut found: Option<(usize, usize)> = None; // (position in tail, comment bytes)
    let mut end = tail.len();
    while let Some(position) = rfind(&tail, EOCD_SIG, end) {
        if position + EOCD_LEN <= tail.len() {
            let comment_bytes = le16(&tail, position + 20) as usize;
            if position + EOCD_LEN + comment_bytes == tail.len() {
                found = Some((position, comment_bytes));
                break;
            }
        }
        // Python: tail.rfind(sig, 0, position). The next match must end before `position`.
        end = position;
    }
    let Some((position, _)) = found else {
        return refuse(
            Code::NoEndRecord,
            "ZIP end-of-central-directory record is missing or malformed",
        );
    };
    let disk_number = le16(&tail, position + 4);
    let directory_disk = le16(&tail, position + 6);
    let disk_entries = le16(&tail, position + 8);
    let total_entries = le16(&tail, position + 10);
    let directory_size = le32(&tail, position + 12);
    let directory_offset = le32(&tail, position + 16);
    let eocd_offset = tail_start + position as u64;
    if disk_number != 0 || directory_disk != 0 || disk_entries != total_entries {
        return refuse(Code::MultiDisk, "multi-disk ZIP archives are not supported");
    }
    if disk_entries == 0xFFFF
        || total_entries == 0xFFFF
        || directory_size == 0xFFFF_FFFF
        || directory_offset == 0xFFFF_FFFF
    {
        return refuse(
            Code::Zip64,
            "ZIP64 archives are not supported by this bounded verifier",
        );
    }
    if u64::from(total_entries) > MAX_ENTRIES {
        return refuse(
            Code::TooManyEntries,
            format!("archive has {total_entries} entries; limit is {MAX_ENTRIES}"),
        );
    }
    if u64::from(directory_size) > MAX_CENTRAL_DIRECTORY_BYTES {
        return refuse(
            Code::CentralDirectoryTooLarge,
            format!("central directory exceeds {MAX_CENTRAL_DIRECTORY_BYTES} bytes"),
        );
    }
    if u64::from(directory_offset) + u64::from(directory_size) != eocd_offset {
        return refuse(
            Code::PrependedData,
            "ZIP must have no prepended data and no gap before its central directory",
        );
    }
    // Python's zipfile selects the end record itself. It must pick the same one.
    check_python_end_record(&tail, tail_start, eocd_offset)?;
    // Python follows a ZIP64 record when a locator sits right before the end record. This
    // verifier does not support ZIP64 at all.
    if eocd_offset >= 20 {
        let locator = read_at(r, eocd_offset - 20, 4)?;
        if locator.as_slice() == ZIP64_LOCATOR_SIG {
            return refuse(
                Code::Zip64,
                "ZIP64 archives are not supported by this bounded verifier",
            );
        }
    }
    let central = read_at(r, u64::from(directory_offset), u64::from(directory_size))?;
    if central.len() as u64 != u64::from(directory_size) {
        return refuse(
            Code::CentralDirectoryTruncated,
            "ZIP central directory is truncated",
        );
    }
    let mut cursor = 0usize;
    let mut actual = 0u64;
    while cursor < central.len() {
        if cursor + CENTRAL_LEN > central.len() {
            return refuse(
                Code::CentralDirectoryTruncated,
                "ZIP central directory record is truncated",
            );
        }
        let rec = &central[cursor..];
        if &rec[..4] != CENTRAL_SIG {
            return refuse(
                Code::CentralDirectoryRecord,
                "ZIP central directory contains an unsupported record",
            );
        }
        let compressed_size = le32(rec, 20);
        let uncompressed_size = le32(rec, 24);
        let name_bytes = le16(rec, 28) as usize;
        let extra_bytes = le16(rec, 30) as usize;
        let comment_bytes = le16(rec, 32) as usize;
        let disk_start = le16(rec, 34);
        let local_offset = le32(rec, 42);
        if disk_start != 0 {
            return refuse(
                Code::MultiDisk,
                "multi-disk or ZIP64 member records are not supported",
            );
        }
        if compressed_size == 0xFFFF_FFFF
            || uncompressed_size == 0xFFFF_FFFF
            || local_offset == 0xFFFF_FFFF
        {
            return refuse(
                Code::Zip64,
                "ZIP64 member records are not supported by this bounded verifier",
            );
        }
        let record_end = cursor + CENTRAL_LEN + name_bytes + extra_bytes + comment_bytes;
        if record_end > central.len() {
            return refuse(
                Code::CentralDirectoryTruncated,
                "ZIP central directory record is truncated",
            );
        }
        actual += 1;
        if actual > MAX_ENTRIES {
            return refuse(
                Code::TooManyEntries,
                format!("archive has more than {MAX_ENTRIES} central-directory entries"),
            );
        }
        cursor = record_end;
    }
    if actual != u64::from(total_entries) {
        return refuse(
            Code::EntryCountMismatch,
            "ZIP end-of-central-directory entry count disagrees with central directory",
        );
    }
    Ok(Preflight {
        directory_offset: u64::from(directory_offset),
        entries: u64::from(total_entries),
        central,
    })
}

/// Python `_EndRecData` picks the end record as follows: when the last 22 bytes are a record with
/// an empty comment, that one. Otherwise the last signature in the final 64 KiB plus 22 bytes.
fn check_python_end_record(
    tail: &[u8],
    tail_start: u64,
    eocd_offset: u64,
) -> Result<(), VerifyError> {
    let picked = if tail.len() >= EOCD_LEN
        && &tail[tail.len() - EOCD_LEN..tail.len() - EOCD_LEN + 4] == EOCD_SIG
        && tail[tail.len() - 2..] == [0, 0]
    {
        Some(tail_start + (tail.len() - EOCD_LEN) as u64)
    } else {
        match rfind(tail, EOCD_SIG, tail.len()) {
            Some(start) if start + EOCD_LEN <= tail.len() => Some(tail_start + start as u64),
            _ => None,
        }
    };
    if picked != Some(eocd_offset) {
        return refuse(
            Code::EndRecordAmbiguous,
            "ZIP end-of-central-directory record is ambiguous between parsers",
        );
    }
    Ok(())
}

// ---- central directory entries --------------------------------------------------------------

#[derive(Debug, Clone)]
struct Entry {
    /// The decoded name exactly as the central directory gives it.
    name: String,
    flags: u16,
    create_system: u8,
    method: u16,
    crc: u32,
    compressed_size: u64,
    size: u64,
    external_attr: u32,
    header_offset: u64,
}

impl Entry {
    fn is_dir(&self) -> bool {
        self.name.ends_with('/')
    }
}

fn decode_name(bytes: &[u8], utf8_flag: bool) -> Result<String, VerifyError> {
    if utf8_flag {
        String::from_utf8(bytes.to_vec()).map_err(|_| VerifyError {
            code: Code::NameEncoding,
            message: "archive path is not valid UTF-8".into(),
        })
    } else {
        Ok(unicode::cp437_decode(bytes))
    }
}

/// Python's `ZipInfo` cuts a name at the first NUL and turns `\` into `/` on Windows. The
/// verifier refuses any name that differs afterwards. NUL and backslash are refused here, for
/// every host.
fn check_sanitized(name: &str) -> Result<(), VerifyError> {
    if name.contains('\0') {
        return refuse(
            Code::NulInName,
            "archive path contains a NUL byte or was otherwise truncated by ZIP parsing",
        );
    }
    if name.contains('\\') {
        return refuse(Code::UnsafePath, format!("unsafe archive path: {name:?}"));
    }
    Ok(())
}

/// Python 3.14 `_decodeExtra`: walk the extra field, reject a corrupt one, and apply the
/// unicode path field (0x7075). The verifier refuses when that field renames the entry.
fn check_extra(extra: &[u8], raw_name: &[u8], name: &str) -> Result<(), VerifyError> {
    let mut rest = extra;
    while rest.len() >= 4 {
        let tp = le16(rest, 0);
        let ln = le16(rest, 2) as usize;
        if ln + 4 > rest.len() {
            return refuse(
                Code::CorruptExtra,
                format!("Corrupt extra field {tp:04x} (size={ln})"),
            );
        }
        if tp == 0x7075 {
            let data = &rest[4..4 + ln];
            if data.len() < 5 {
                return refuse(
                    Code::UnicodePathExtra,
                    "Corrupt unicode path extra field (0x7075)",
                );
            }
            let version = data[0];
            let name_crc = le32(data, 1);
            let mut crc = flate2::Crc::new();
            crc.update(raw_name);
            if version == 1 && name_crc == crc.sum() {
                let Ok(text) = std::str::from_utf8(&data[5..]) else {
                    return refuse(
                        Code::UnicodePathExtra,
                        "Corrupt unicode path extra field (0x7075): invalid utf-8 bytes",
                    );
                };
                if !text.is_empty() && text != name {
                    return refuse(
                        Code::UnicodePathExtra,
                        "unicode path extra field renames the entry",
                    );
                }
            }
        }
        rest = &rest[ln + 4..];
    }
    Ok(())
}

fn parse_central(pre: &Preflight) -> Result<Vec<Entry>, VerifyError> {
    let central = &pre.central;
    let mut entries = Vec::with_capacity(pre.entries as usize);
    let mut cursor = 0usize;
    while cursor < central.len() {
        let rec = &central[cursor..];
        let flags = le16(rec, 8);
        let name_len = le16(rec, 28) as usize;
        let extra_len = le16(rec, 30) as usize;
        let comment_len = le16(rec, 32) as usize;
        let raw_name = &rec[CENTRAL_LEN..CENTRAL_LEN + name_len];
        let extra = &rec[CENTRAL_LEN + name_len..CENTRAL_LEN + name_len + extra_len];
        let name = decode_name(raw_name, flags & FLAG_UTF8 != 0)?;
        // Byte 4 is "version needed" low byte (what zipfile calls extract_version).
        if rec[6] > MAX_EXTRACT_VERSION {
            return refuse(
                Code::ExtractVersion,
                format!("zip file version {:.1}", f64::from(rec[6]) / 10.0),
            );
        }
        check_extra(extra, raw_name, &name)?;
        check_sanitized(&name)?;
        entries.push(Entry {
            name,
            flags,
            create_system: rec[5],
            method: le16(rec, 10),
            crc: le32(rec, 16),
            compressed_size: u64::from(le32(rec, 20)),
            size: u64::from(le32(rec, 24)),
            external_attr: le32(rec, 38),
            header_offset: u64::from(le32(rec, 42)),
        });
        cursor += CENTRAL_LEN + name_len + extra_len + comment_len;
    }
    if entries.len() as u64 != pre.entries {
        return refuse(
            Code::EntryCountMismatch,
            "ZIP entry count disagrees with end-of-central-directory record",
        );
    }
    Ok(entries)
}

// ---- path and entry rules -------------------------------------------------------------------

fn entry_kind(e: &Entry) -> Result<bool, VerifyError> {
    // Returns is_directory.
    let is_dir = e.is_dir();
    let mode = (e.external_attr >> 16) & 0xFFFF;
    let file_type = mode & S_IFMT;
    if file_type == S_IFLNK {
        return refuse(
            Code::Symlink,
            format!("symlink entry is not allowed: {:?}", e.name),
        );
    }
    if file_type != 0 && file_type != S_IFREG && file_type != S_IFDIR {
        return refuse(
            Code::SpecialEntry,
            format!("special filesystem entry is not allowed: {:?}", e.name),
        );
    }
    if file_type == S_IFDIR && !is_dir {
        return refuse(
            Code::ModeMismatch,
            format!("directory mode disagrees with ZIP path: {:?}", e.name),
        );
    }
    if file_type == S_IFREG && is_dir {
        return refuse(
            Code::ModeMismatch,
            format!("file mode disagrees with ZIP path: {:?}", e.name),
        );
    }
    if e.create_system == 0 {
        if e.external_attr & 0x400 != 0 {
            return refuse(
                Code::DosAttributes,
                format!("DOS reparse-point entry is not allowed: {:?}", e.name),
            );
        }
        if e.external_attr & 0x10 != 0 && !is_dir {
            return refuse(
                Code::DosAttributes,
                format!("DOS directory flag disagrees with ZIP path: {:?}", e.name),
            );
        }
    }
    Ok(is_dir)
}

fn path_parts(name: &str, is_dir: bool) -> Result<Vec<&str>, VerifyError> {
    let unsafe_path = || VerifyError {
        code: Code::UnsafePath,
        message: format!("unsafe archive path: {name:?}"),
    };
    if name.is_empty() {
        return refuse(Code::UnsafePath, "archive contains an empty path");
    }
    if name.contains('\0') || name.contains('\\') || name.starts_with('/') {
        return Err(unsafe_path());
    }
    let body = if is_dir {
        match name.strip_suffix('/') {
            Some(b) => b,
            None => {
                return refuse(
                    Code::UnsafePath,
                    format!("directory entry has no trailing slash: {name:?}"),
                )
            }
        }
    } else {
        name
    };
    let parts: Vec<&str> = body.split('/').collect();
    if body.is_empty()
        || parts
            .iter()
            .any(|p| p.is_empty() || *p == "." || *p == "..")
    {
        return Err(unsafe_path());
    }
    if name.len() > MAX_PATH_BYTES {
        return refuse(
            Code::UnsafePath,
            format!("archive path exceeds {MAX_PATH_BYTES} UTF-8 bytes"),
        );
    }
    for part in &parts {
        if part.len() > MAX_SEGMENT_BYTES {
            return refuse(
                Code::UnsafePath,
                format!("archive path segment exceeds {MAX_SEGMENT_BYTES} UTF-8 bytes: {name:?}"),
            );
        }
        let last = part.chars().next_back().unwrap_or('.');
        if last == '.' || last == ' ' || part.chars().any(|c| "<>:\"|?*".contains(c)) {
            return refuse(
                Code::UnsafePath,
                format!("path is not portable to Windows: {name:?}"),
            );
        }
        if part.chars().any(unicode::is_control_format_surrogate) {
            return refuse(
                Code::UnsafePath,
                format!("archive path contains a control character: {name:?}"),
            );
        }
        let device = unicode::upper(part.split('.').next().unwrap_or(""));
        if WINDOWS_RESERVED.contains(&device.as_str()) {
            return refuse(
                Code::UnsafePath,
                format!("archive path uses a reserved Windows device name: {name:?}"),
            );
        }
    }
    Ok(parts)
}

fn char_len(s: &str) -> u64 {
    s.chars().count() as u64
}

/// Bound the material retained and normalised while checking every path prefix.
fn collision_prefix_cost(parts: &[&str]) -> u64 {
    let (mut original, mut nfc, mut nfkc, mut total) = (0u64, 0u64, 0u64, 0u64);
    for (index, part) in parts.iter().enumerate() {
        let separator = u64::from(index > 0);
        original += char_len(part) + separator;
        nfc += char_len(&unicode::casefold(&unicode::nfc(part))) + separator;
        nfkc += char_len(&unicode::casefold(&unicode::nfkc(part))) + separator;
        total += original + nfc + nfkc;
    }
    total
}

fn collision_keys(prefix: &str) -> (String, String) {
    (
        unicode::casefold(&unicode::nfc(prefix)),
        unicode::casefold(&unicode::nfkc(prefix)),
    )
}

struct Inspected {
    /// Normalised path (no trailing slash) to (is_dir, index into entries).
    by_path: HashMap<String, (bool, usize)>,
    file_count: u64,
    total_uncompressed: u64,
}

fn inspect_entries(
    entries: &[Entry],
    central_directory_offset: u64,
) -> Result<Inspected, VerifyError> {
    if entries.len() as u64 > MAX_ENTRIES {
        return refuse(
            Code::TooManyEntries,
            format!(
                "archive has {} entries; limit is {MAX_ENTRIES}",
                entries.len()
            ),
        );
    }
    let mut by_path: HashMap<String, (bool, usize)> = HashMap::new();
    let mut prefix_spelling: [HashMap<String, String>; 2] = [HashMap::new(), HashMap::new()];
    let mut header_offsets: HashSet<u64> = HashSet::new();
    let mut file_count = 0u64;
    let mut total_uncompressed = 0u64;
    let mut collision_prefix_chars = 0u64;

    for (index, e) in entries.iter().enumerate() {
        let is_dir = entry_kind(e)?;
        let parts = path_parts(&e.name, is_dir)?;
        collision_prefix_chars += collision_prefix_cost(&parts);
        if collision_prefix_chars > MAX_COLLISION_PREFIX_CHARS {
            return refuse(
                Code::CollisionBudget,
                format!(
                    "archive path collision checks exceed {MAX_COLLISION_PREFIX_CHARS} aggregate prefix characters"
                ),
            );
        }
        let path = parts.join("/");
        if let Some(&(previous_dir, _)) = by_path.get(&path) {
            if previous_dir == is_dir {
                return refuse(
                    Code::DuplicatePath,
                    format!("duplicate archive path: {:?}", e.name),
                );
            }
            return refuse(
                Code::FileAndDirectory,
                format!("archive path is both a file and directory: {path:?}"),
            );
        }
        by_path.insert(path, (is_dir, index));

        for n in 1..=parts.len() {
            let prefix = parts[..n].join("/");
            let (k0, k1) = collision_keys(&prefix);
            for (slot, key) in [(0usize, k0), (1usize, k1)] {
                if let Some(old) = prefix_spelling[slot].get(&key) {
                    if *old != prefix {
                        return refuse(
                            Code::PathCollision,
                            format!("case or Unicode path collision: {old:?} and {prefix:?}"),
                        );
                    }
                }
                prefix_spelling[slot].insert(key, prefix.clone());
            }
        }

        if e.flags & (0x0001 | 0x0040 | 0x2000) != 0 {
            return refuse(
                Code::Encrypted,
                format!("encrypted ZIP entries are not supported: {:?}", e.name),
            );
        }
        if e.method != 0 && e.method != 8 {
            return refuse(
                Code::Compression,
                format!("unsupported ZIP compression method for {:?}", e.name),
            );
        }
        if e.header_offset >= central_directory_offset {
            return refuse(
                Code::LocalHeaderOffset,
                format!("invalid local header offset: {:?}", e.name),
            );
        }
        if !header_offsets.insert(e.header_offset) {
            return refuse(
                Code::SharedLocalHeader,
                format!("ZIP entries share a local header: {:?}", e.name),
            );
        }

        if is_dir {
            if e.size != 0 || e.compressed_size != 0 || e.crc != 0 {
                return refuse(
                    Code::DirectoryNotEmpty,
                    format!("directory entries must be empty: {:?}", e.name),
                );
            }
            continue;
        }
        file_count += 1;
        if e.size > MAX_FILE_BYTES {
            return refuse(
                Code::FileTooLarge,
                format!(
                    "file exceeds {MAX_FILE_BYTES} uncompressed bytes: {:?}",
                    e.name
                ),
            );
        }
        total_uncompressed += e.size;
        if total_uncompressed > MAX_TOTAL_BYTES {
            return refuse(
                Code::TotalTooLarge,
                format!("archive exceeds {MAX_TOTAL_BYTES} total uncompressed bytes"),
            );
        }
        if e.size != 0
            && (e.compressed_size == 0 || e.size > e.compressed_size * MAX_COMPRESSION_RATIO)
        {
            return refuse(
                Code::CompressionRatio,
                format!(
                    "compression ratio exceeds {MAX_COMPRESSION_RATIO}:1: {:?}",
                    e.name
                ),
            );
        }
        if e.size == 0 && e.compressed_size > MAX_EMPTY_FILE_COMPRESSED_BYTES {
            return refuse(
                Code::EmptyFileData,
                format!("empty file has excessive compressed data: {:?}", e.name),
            );
        }
    }

    // A file cannot also be an ancestor directory. Unlisted parent directories are implicit.
    for path in by_path.keys() {
        let parts: Vec<&str> = path.split('/').collect();
        for n in 1..parts.len() {
            let parent = parts[..n].join("/");
            if let Some(&(parent_is_dir, _)) = by_path.get(&parent) {
                if !parent_is_dir {
                    return refuse(
                        Code::FileUsedAsDirectory,
                        format!("file is used as a directory: {parent:?}"),
                    );
                }
            }
        }
    }
    Ok(Inspected {
        by_path,
        file_count,
        total_uncompressed,
    })
}

// ---- reading members ------------------------------------------------------------------------

struct MemberOut {
    sha256: String,
    size: u64,
    data: Option<Vec<u8>>,
}

fn member_error(name: &str, why: &str) -> VerifyError {
    VerifyError {
        code: Code::MemberRead,
        message: format!("could not read ZIP entry {name:?}: {why}"),
    }
}

fn local_error(name: &str, why: &str) -> VerifyError {
    VerifyError {
        code: Code::LocalHeader,
        message: format!("could not read ZIP entry {name:?}: {why}"),
    }
}

/// What Python's `ZipFile.open` and `ZipExtFile.read` do for one member, then hash it.
fn read_member<R: Read + Seek>(
    r: &mut R,
    e: &Entry,
    end_offset: u64,
    keep: bool,
    buf: &mut [u8],
) -> Result<MemberOut, VerifyError> {
    let header = read_at(r, e.header_offset, LOCAL_LEN as u64)?;
    if header.len() != LOCAL_LEN {
        return Err(local_error(&e.name, "Truncated file header"));
    }
    if &header[..4] != LOCAL_SIG {
        return Err(local_error(&e.name, "Bad magic number for file header"));
    }
    let local_flags = le16(&header, 6);
    let name_len = le16(&header, 26) as u64;
    let extra_len = le16(&header, 28) as u64;
    let raw_name = read_at(r, e.header_offset + LOCAL_LEN as u64, name_len)?;
    // 0x20: compressed patched data. 0x40: strong encryption. Python raises on both.
    if e.flags & (0x0020 | 0x0040) != 0 {
        return Err(local_error(&e.name, "unsupported ZIP flags"));
    }
    let local_name = decode_name(&raw_name, local_flags & FLAG_UTF8 != 0)
        .map_err(|_| local_error(&e.name, "local file name is not valid UTF-8"))?;
    if local_name != e.name {
        return Err(local_error(
            &e.name,
            "File name in directory and header differ.",
        ));
    }
    let data_start = e.header_offset + LOCAL_LEN as u64 + raw_name.len() as u64 + extra_len;
    if data_start + e.compressed_size > end_offset {
        return Err(local_error(
            &e.name,
            "Overlapped entries (possible zip bomb)",
        ));
    }
    if e.is_dir() {
        return Ok(MemberOut {
            sha256: String::new(),
            size: 0,
            data: None,
        });
    }

    let mut sha = Sha256::new();
    let mut crc = flate2::Crc::new();
    let mut produced = 0u64;
    let mut data = if keep { Some(Vec::new()) } else { None };
    r.seek(SeekFrom::Start(data_start))
        .map_err(|err| io_err("seek in archive", err))?;
    let mut feed = |chunk: &[u8], produced: &mut u64, counted: u64| {
        // `counted` is how many bytes of this chunk are inside the declared size.
        let part = &chunk[..counted as usize];
        sha.update(part);
        crc.update(part);
        *produced += counted;
        if let Some(d) = data.as_mut() {
            d.extend_from_slice(part);
        }
    };
    match e.method {
        0 => {
            if e.compressed_size < e.size {
                return Err(member_error(
                    &e.name,
                    "stored data is shorter than the declared size",
                ));
            }
            let mut input = r.by_ref().take(e.size);
            while produced < e.size {
                let want = buf.len().min((e.size - produced) as usize);
                let n = input
                    .read(&mut buf[..want])
                    .map_err(|err| io_err("read archive", err))?;
                if n == 0 {
                    return Err(member_error(&e.name, "unexpected end of file"));
                }
                feed(&buf[..n], &mut produced, n as u64);
            }
        }
        8 => {
            let limit = if e.size == 0 {
                EMPTY_FILE_LOOKAHEAD
            } else {
                e.size
            };
            let input = r.by_ref().take(e.compressed_size);
            let mut inflater = flate2::read::DeflateDecoder::new(input);
            let mut seen = 0u64;
            while seen < limit {
                let want = buf.len().min((limit - seen) as usize);
                let n = inflater.read(&mut buf[..want]).map_err(|err| {
                    member_error(&e.name, &format!("invalid deflate data: {err}"))
                })?;
                if n == 0 {
                    break;
                }
                seen += n as u64;
                let inside = (n as u64).min(e.size.saturating_sub(produced));
                feed(&buf[..n], &mut produced, inside);
            }
        }
        _ => return Err(member_error(&e.name, "unsupported compression method")),
    }
    if produced != e.size {
        return Err(member_error(
            &e.name,
            "uncompressed size disagrees with ZIP metadata",
        ));
    }
    if crc.sum() != e.crc {
        return Err(member_error(&e.name, "Bad CRC-32"));
    }
    Ok(MemberOut {
        sha256: to_hex(&sha.finish()),
        size: produced,
        data,
    })
}

// ---- driver ---------------------------------------------------------------------------------

/// Verify an archive given as a seekable stream. `name` is only echoed in the report.
pub fn verify_stream<R: Read + Seek>(
    r: &mut R,
    archive_size: u64,
    name: &str,
) -> Result<Report, VerifyError> {
    if archive_size > MAX_ARCHIVE_BYTES {
        return refuse(
            Code::ArchiveTooLarge,
            format!("archive exceeds {MAX_ARCHIVE_BYTES} bytes"),
        );
    }
    if archive_size == 0 {
        return refuse(Code::ArchiveEmpty, "archive is empty");
    }
    let pre = preflight(r, archive_size)?;
    let entries = parse_central(&pre)?;
    let inspected = inspect_entries(&entries, pre.directory_offset)?;
    let by_path = &inspected.by_path;

    let roots: HashSet<&str> = by_path
        .keys()
        .map(|p| p.split('/').next().unwrap_or(""))
        .collect();
    if roots.len() != 1 {
        return refuse(
            Code::NotOnePluginFolder,
            "archive must contain exactly one plugin folder at its root",
        );
    }
    let folder = (*roots.iter().next().unwrap_or(&"")).to_string();
    if let Some(&(is_dir, _)) = by_path.get(&folder) {
        if !is_dir {
            return refuse(Code::RootNotFolder, "archive root must be a folder");
        }
    }
    let manifest_path = format!("{folder}/plugin.ini");
    match by_path.get(&manifest_path) {
        Some(&(false, index)) => {
            if entries[index].size > MAX_MANIFEST_BYTES {
                return refuse(
                    Code::ManifestTooLarge,
                    format!("{manifest_path:?} exceeds {MAX_MANIFEST_BYTES} bytes"),
                );
            }
        }
        _ => {
            return refuse(
                Code::ManifestMissing,
                format!("archive must contain {manifest_path:?}"),
            )
        }
    }

    // Every member's declared size and compression bounds are validated above, before the
    // archive is hashed or any member is opened for decompression.
    let archive_hash_before = hash_stream(r)?;

    // The end offset of each local entry: the next local header, or the central directory.
    let mut order: Vec<usize> = (0..entries.len()).collect();
    order.sort_by_key(|&i| entries[i].header_offset);
    let mut end_offsets = vec![0u64; entries.len()];
    let mut end = pre.directory_offset;
    for &i in order.iter().rev() {
        end_offsets[i] = end;
        end = entries[i].header_offset;
    }

    let mut buf = vec![0u8; CHUNK_BYTES];
    let mut artifacts: Vec<Artifact> = Vec::new();
    let mut manifest_bytes: Option<Vec<u8>> = None;
    let mut actual_total = 0u64;
    for (i, e) in entries.iter().enumerate() {
        let is_manifest = e.name == manifest_path;
        let out = read_member(r, e, end_offsets[i], is_manifest, &mut buf)?;
        if e.is_dir() {
            continue;
        }
        actual_total += out.size;
        if is_manifest {
            manifest_bytes = out.data;
        }
        artifacts.push(Artifact {
            path: e.name.clone(),
            size_bytes: out.size,
            sha256: out.sha256,
        });
    }
    let Some(manifest_bytes) = manifest_bytes else {
        return refuse(
            Code::ManifestMissing,
            format!("could not read {manifest_path:?}"),
        );
    };

    let parsed = manifest::parse(&manifest_bytes, None).map_err(|e| VerifyError {
        code: Code::ManifestInvalid,
        message: format!("plugin.ini rejected by the manifest parser: {e}"),
    })?;
    if parsed.id != folder {
        return refuse(
            Code::ManifestIdMismatch,
            format!(
                "manifest id {:?} does not match archive folder {folder:?}",
                parsed.id
            ),
        );
    }
    let entry_name = match parsed.entry.as_ref() {
        None => None,
        Some(_) => match parsed.entry_str() {
            Some(s) => Some(s.to_string()),
            None => {
                return refuse(
                    Code::ManifestEntryEncoding,
                    "plugin.ini entry is not valid UTF-8",
                )
            }
        },
    };
    if parsed.kind != Kind::Data {
        let entry_path = format!("{folder}/{}", entry_name.as_deref().unwrap_or(""));
        match by_path.get(&entry_path) {
            Some(&(false, _)) => {}
            _ => {
                return refuse(
                    Code::EntryMissing,
                    format!("declared entry is not a regular archive file: {entry_path:?}"),
                )
            }
        }
    }

    let archive_hash_after = hash_stream(r)?;
    if archive_hash_before != archive_hash_after {
        return refuse(
            Code::ArchiveChanged,
            "archive changed while it was being verified",
        );
    }
    if actual_total != inspected.total_uncompressed {
        return refuse(
            Code::TotalMismatch,
            "total uncompressed size disagrees with ZIP metadata",
        );
    }

    let manifest_sha256 = artifacts
        .iter()
        .find(|a| a.path == manifest_path)
        .map(|a| a.sha256.clone())
        .unwrap_or_default();
    artifacts.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(Report {
        archive: ArchiveInfo {
            name: name.to_string(),
            size_bytes: archive_size,
            sha256: archive_hash_before,
        },
        plugin: PluginInfo {
            id: parsed.id.clone(),
            folder,
            kind: parsed.kind,
            declared_api: parsed.api.clone(),
            entry: entry_name,
            requires: parsed.requires.clone(),
            manifest_path,
            manifest_sha256,
        },
        file_count: inspected.file_count,
        total_uncompressed_bytes: actual_total,
        artifacts,
        manifest: parsed,
    })
}

/// Verify an archive file. Refuses anything that is not a regular file, and refuses an archive
/// whose size or modification time changed while it was read.
pub fn verify_archive(path: &Path) -> Result<Report, VerifyError> {
    let meta = std::fs::metadata(path).map_err(|e| VerifyError {
        code: Code::CannotOpen,
        message: format!("cannot stat archive: {e}"),
    })?;
    if !meta.is_file() {
        return refuse(Code::NotRegularFile, "archive is not a regular file");
    }
    let mut file = File::open(path).map_err(|e| VerifyError {
        code: Code::CannotOpen,
        message: format!("cannot open archive: {e}"),
    })?;
    let before = file.metadata().map_err(|e| io_err("stat archive", e))?;
    if !before.is_file() {
        return refuse(Code::NotRegularFile, "archive is not a regular file");
    }
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let report = verify_stream(&mut file, before.len(), &name)?;
    let after = file.metadata().map_err(|e| io_err("stat archive", e))?;
    if before.len() != after.len() || before.modified().ok() != after.modified().ok() {
        return refuse(
            Code::ArchiveChanged,
            "archive changed while it was being verified",
        );
    }
    Ok(report)
}
