// SPDX-License-Identifier: GPL-3.0-only
//! `data\launcher.log`: `OpenLog` and `Out` of the C++ launcher.

use scol_install::fsops::{local_time, move_file};
use std::io::Write;
use std::os::windows::fs::OpenOptionsExt;

/// `OpenLog`: a new run (`append` false) moves the previous log to `launcher.log.1`, replacing an older
/// one, and starts a new file; every writer appends. The first line is `--- <time> <header>`. Every
/// writer opens the file with all three share modes, as `_SH_DENYNO` does.
pub fn open_log(data: &str, append: bool, header: &str) -> Option<std::fs::File> {
    let file = format!("{data}\\launcher.log");
    if !append && std::fs::symlink_metadata(&file).is_ok() {
        let _ = move_file(&file, &format!("{file}.1"), true);
    }
    let mut f = std::fs::OpenOptions::new()
        .append(true)
        .create(true)
        .share_mode(7)
        .open(&file)
        .ok()?;
    let (y, mo, d, h, mi, s) = local_time();
    let _ = writeln!(
        f,
        "--- {y:04}-{mo:02}-{d:02} {h:02}:{mi:02}:{s:02} {header}"
    );
    let _ = f.flush();
    Some(f)
}

/// Console text and the log at once, like the C++ `Out`.
pub struct Tee<'a> {
    pub out: &'a mut dyn Write,
    pub log: Option<std::fs::File>,
}

impl Write for Tee<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.out.write_all(buf)?;
        if let Some(l) = self.log.as_mut() {
            let _ = l.write_all(buf);
            let _ = l.flush();
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.out.flush()
    }
}

/// Writes to the log file only (the helper of a run has no console).
pub struct LogOnly(pub Option<std::fs::File>);

impl Write for LogOnly {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        if let Some(l) = self.0.as_mut() {
            let _ = l.write_all(buf);
            let _ = l.flush();
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}
