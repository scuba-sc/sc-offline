// SPDX-License-Identifier: GPL-3.0-only
//! After the game closes: the session logs (`CleanSessionLogs`, `DeleteSessionLogs`) and the crash
//! report offer (`OfferCrashReport`). Only this session's files are ever touched, and only on an
//! explicit yes (or `clean_logs = always`). Nothing is sent anywhere.

use crate::host::{Host, LogAnswer};
use scol_config::ini_edit::set_ini_value;
use scol_config::LogChoice;
use scol_contract::files;
use scol_install::fsops::local_time;
use scol_install::paths::GAME_EXE;
use scol_install::version::SCO_VERSION;
use scol_pcchanges::system::system32;
use std::io::Write;
use std::os::windows::fs::MetadataExt;
use std::path::Path;

const ATTR_DIRECTORY: u32 = 0x10;
const ATTR_REPARSE_POINT: u32 = 0x400;
const ERROR_ACCESS_DENIED: i32 = 5;

/// Files of `dir` written at or after `since` (FILETIME ticks). Reparse points are skipped.
pub(crate) fn files_in(dir: &str, since: u64, recurse: bool, out: &mut Vec<String>) {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return;
    };
    let mut entries: Vec<_> = rd.flatten().collect();
    entries.sort_by_key(|e| e.file_name());
    for e in entries {
        let Ok(m) = std::fs::symlink_metadata(e.path()) else {
            continue;
        };
        let attrs = m.file_attributes();
        if attrs & ATTR_REPARSE_POINT != 0 {
            continue;
        }
        let p = format!("{dir}\\{}", e.file_name().to_string_lossy());
        if attrs & ATTR_DIRECTORY != 0 {
            if recurse {
                files_in(&p, since, true, out);
            }
            continue;
        }
        if m.last_write_time() >= since {
            out.push(p);
        }
    }
}

/// `SessionLogs`: this session's Game.log, `logbackups` and `Crashes` files of a channel folder.
pub fn session_logs(channel_dir: &str, since: u64) -> Vec<String> {
    let mut files = Vec::new();
    let game_log = format!("{channel_dir}\\Game.log");
    if let Ok(m) = std::fs::metadata(&game_log) {
        if m.last_write_time() >= since {
            files.push(game_log);
        }
    }
    files_in(
        &format!("{channel_dir}\\logbackups"),
        since,
        false,
        &mut files,
    );
    files_in(&format!("{channel_dir}\\Crashes"), since, true, &mut files);
    files
}

/// `DeleteSessionLogs` (`--delete-logs <channel folder> <since>`): the elevated half of
/// [`clean_session_logs`]. Acts only on a real channel folder (`Bin64\StarCitizen.exe` inside) and only
/// on this session's files. 0 ok, 3 a file could not be deleted, 4 refused.
pub fn delete_session_logs(channel_dir: &str, since: u64) -> i32 {
    if !Path::new(&format!("{channel_dir}\\Bin64\\{GAME_EXE}")).is_file() || since == 0 {
        return 4;
    }
    let failed = session_logs(channel_dir, since)
        .iter()
        .filter(|f| std::fs::remove_file(f).is_err())
        .count();
    if failed > 0 {
        3
    } else {
        0
    }
}

pub(crate) fn plural(n: usize) -> &'static str {
    if n == 1 {
        ""
    } else {
        "s"
    }
}

/// `CleanSessionLogs`. ask: list this session's logs and ask; always: delete them without asking;
/// never: do nothing.
pub fn clean_session_logs(
    host: &mut dyn Host,
    out: &mut dyn Write,
    here: &str,
    channel_dir: &str,
    since: u64,
    mode: LogChoice,
    can_ask: bool,
) {
    if mode == LogChoice::Never {
        return;
    }
    if mode == LogChoice::Ask && !can_ask {
        let _ = writeln!(
            out,
            "Logs:     not asking (no console to answer in); run from a window to be asked"
        );
        return;
    }
    let files = session_logs(channel_dir, since);
    if files.is_empty() {
        let _ = writeln!(out, "Logs:     the game wrote no logs this session");
        return;
    }
    let _ = write!(
        out,
        "\nThe game wrote these logs during this modded session:\n"
    );
    for f in &files {
        let _ = writeln!(out, "    {f}");
    }
    let ini = format!("{here}\\{}", files::INI);
    if mode == LogChoice::Always {
        let _ = writeln!(
            out,
            "clean_logs = always: deleting them without asking. Older logs are not touched."
        );
    } else {
        let _ = write!(
            out,
            "Deleting them removes the record of this session from your game folder. Keep Game.log if you\n\
want to report a bug. Older logs are not touched.\n\
\x20 y = delete   n = keep (the default)   always = delete now and after every session   never = keep, and don't ask again\n\
\x20 (clean_logs = ask, always or never in sc-offline.ini, or Settings in the window, changes this later.)\n"
        );
        match host.ask_logs(out, "Delete these files?") {
            LogAnswer::Never => {
                if set_ini_value(Path::new(&ini), "clean_logs", LogChoice::Never.word()) {
                    let _ = writeln!(out, "Logs:     kept; won't ask again (clean_logs = never)");
                } else {
                    let _ = writeln!(out, "Logs:     kept; couldn't write sc-offline.ini, set clean_logs = never there by hand");
                }
                return;
            }
            LogAnswer::Keep => {
                let _ = writeln!(out, "Logs:     kept");
                return;
            }
            LogAnswer::Always => {
                if set_ini_value(Path::new(&ini), "clean_logs", LogChoice::Always.word()) {
                    let _ = writeln!(out, "[i] clean_logs = always saved; later sessions' logs are deleted without asking");
                } else {
                    let _ = writeln!(
                        out,
                        "[!] couldn't write sc-offline.ini; set clean_logs = always there by hand"
                    );
                }
            }
            LogAnswer::Delete => {
                if !host.ask_yes(out, "Are you sure? They can't be recovered.") {
                    let _ = writeln!(out, "Logs:     kept");
                    return;
                }
            }
        }
    }
    let (mut gone, mut failed, mut denied) = (0usize, 0usize, 0usize);
    for f in &files {
        match std::fs::remove_file(f) {
            Ok(()) => gone += 1,
            Err(e) if e.raw_os_error() == Some(ERROR_ACCESS_DENIED) => denied += 1,
            Err(e) => {
                failed += 1;
                let _ = writeln!(
                    out,
                    "[!] couldn't delete {f} (error {})",
                    e.raw_os_error().unwrap_or(0)
                );
            }
        }
    }
    if denied > 0 {
        // The game folder needs administrator rights: one UAC prompt. The elevated run works out this
        // session's files again itself, it is not handed a list.
        let _ = writeln!(
            out,
            "[i] {denied} file{} need administrator rights to delete; Windows will ask once.",
            plural(denied)
        );
        let code = host.delete_logs_elevated(channel_dir, since);
        let left = session_logs(channel_dir, since).len();
        if code == 0 && left == 0 {
            gone += denied;
        } else {
            failed += left;
            let _ = writeln!(
                out,
                "[!] {left} file{} still there (administrator rights refused?)",
                plural(left)
            );
        }
    }
    let _ = writeln!(
        out,
        "Logs:     deleted {gone} file{}{}",
        plural(gone),
        if failed > 0 {
            "; see above for the rest"
        } else {
            ""
        }
    );
}

pub(crate) fn is_text_report(f: &str) -> bool {
    match f.rfind('.') {
        Some(dot) => {
            let ext = &f[dot..];
            [".log", ".txt", ".json", ".xml", ".cfg", ".ini"]
                .iter()
                .any(|e| ext.eq_ignore_ascii_case(e))
        }
        None => false,
    }
}

pub(crate) fn url_encode(s: &str) -> String {
    let mut o = String::new();
    for c in s.bytes() {
        if c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_' | b'.' | b'~') {
            o.push(c as char);
        } else {
            o.push_str(&format!("%{c:02X}"));
        }
    }
    o
}

/// What the redactor knows about this PC: the profile folder and the Windows account name (USERNAME and
/// the last part of the profile path).
fn host_identities(host: &dyn Host) -> scol_redact::Identities {
    let mut id = scol_redact::Identities::default();
    let profile = host.env("USERPROFILE").unwrap_or_default();
    if !profile.is_empty() {
        id.profile_roots.push(profile.clone().into_bytes());
    }
    let account = host.env("USERNAME").unwrap_or_default();
    if !account.is_empty() {
        id.account_names.push(account.into_bytes());
    }
    let trimmed = profile.trim_end_matches(['\\', '/']);
    let last = match trimmed.rfind(['\\', '/']) {
        Some(i) => &trimmed[i + 1..],
        None => trimmed,
    };
    if !last.is_empty() {
        id.account_names.push(last.as_bytes().to_vec());
    }
    id
}

/// The last lines of a bundle's README: what was replaced and what the reader still has to do.
pub(crate) const REDACTED_NOTE: &str = "Redacted: RSI handle, nickname, character name and account/GEID numbers -> <redacted>; Windows user name -> <user>;\r\nprofile paths -> %USERPROFILE%; e-mail addresses -> <redacted>. Free text can still hold a name: read the files before you share them.\r\n";

/// What goes into a bundle and where it is written.
pub(crate) struct BundleSpec<'a> {
    /// The folder the bundle is made in (`data\crash-reports`, `data\reports`).
    pub dir: &'a str,
    /// The start of the zip's name; the local date and time follow.
    pub name: &'a str,
    /// Files to take: (path, name in the bundle). A missing or empty file is left out; of a huge one
    /// the last 16 MiB are kept.
    pub files: &'a [(String, String)],
    /// Texts the launcher made itself: (name in the bundle, text). Redacted like the files.
    pub texts: &'a [(String, Vec<u8>)],
    /// `README.txt`, written as it is.
    pub readme: &'a str,
}

/// A bundle that was written.
pub(crate) struct Bundle {
    /// The zip, or the folder when `tar.exe` could not zip it.
    pub made: String,
    pub zipped: bool,
    /// What is in it besides `README.txt`: (name, bytes after redaction).
    pub names: Vec<(String, usize)>,
    /// The logs held more handles than the redactor keeps, so one may be left in free text.
    pub handles_dropped: bool,
}

/// Writes a redacted bundle: every file teaches the redactor the handle for all of them, then each
/// is written with the strict rules (a bundle leaves the PC), then `tar.exe` zips the folder. Nothing
/// is sent anywhere.
pub(crate) fn write_bundle(host: &mut dyn Host, spec: &BundleSpec<'_>) -> Bundle {
    let mut ids = host_identities(host);
    ids.strict = true; // hide short and common-word handles inside words too
    const KEEP: usize = 16 << 20;
    let mut parts: Vec<(&str, Vec<u8>)> = Vec::new();
    for (src, name) in spec.files {
        let Ok(mut bytes) = std::fs::read(src) else {
            continue;
        };
        if bytes.len() > KEEP {
            bytes.drain(..bytes.len() - KEEP); // keep the tail of huge logs
        }
        scol_redact::learn(&mut ids, &bytes);
        parts.push((name, bytes));
    }
    for (name, text) in spec.texts {
        scol_redact::learn(&mut ids, text);
        parts.push((name, text.clone()));
    }
    let (y, mo, d, h, mi, s) = local_time();
    let stamp = format!("{y:04}{mo:02}{d:02}-{h:02}{mi:02}{s:02}");
    let stage = format!("{}\\tmp-{stamp}", spec.dir);
    let zip = format!("{}\\{}-{stamp}.zip", spec.dir, spec.name);
    let _ = std::fs::create_dir_all(&stage);
    let redactor = scol_redact::Redactor::new(&ids);
    let mut names = Vec::new();
    for (name, bytes) in &parts {
        if bytes.is_empty() {
            continue;
        }
        let clean = redactor.apply(bytes);
        if std::fs::write(format!("{stage}\\{name}"), &clean).is_ok() {
            names.push((name.to_string(), clean.len()));
        }
    }
    let _ = std::fs::write(format!("{stage}\\README.txt"), spec.readme);
    let tar = system32(host.system(), "tar.exe");
    let zipped = host.system().is_file(&tar)
        && host
            .system()
            .run(&tar, &format!("-a -cf \"{zip}\" -C \"{stage}\" ."))
            == 0;
    if zipped {
        let _ = std::fs::remove_dir_all(&stage);
    }
    Bundle {
        made: if zipped { zip } else { stage },
        zipped,
        names,
        handles_dropped: ids.legacy_dropped > 0 || ids.priority_dropped > 0,
    }
}

/// `OfferCrashReport`: after a session with files in `Crashes`, offers a redacted bundle in
/// `data\crash-reports`. Nothing is sent: the player attaches the zip to the bug form.
pub fn offer_crash_report(
    host: &mut dyn Host,
    out: &mut dyn Write,
    here: &str,
    channel_dir: &str,
    since: u64,
    game_build: &str,
) {
    let mut crash = Vec::new();
    files_in(&format!("{channel_dir}\\Crashes"), since, true, &mut crash);
    if crash.is_empty() {
        return;
    }
    let dumps = crash.iter().filter(|f| !is_text_report(f)).count();
    let _ = write!(
        out,
        "\n[!] the game crashed this session ({} file{} in {channel_dir}\\Crashes).\n",
        crash.len(),
        plural(crash.len())
    );
    let _ = write!(
        out,
        "    A crash report zips mod.log, launcher.log, Game.log and the crash text files into data\\crash-reports,\n\
\x20   with your RSI handle, account numbers, e-mail address and Windows user name replaced. Nothing is sent: you attach\n\
\x20   the zip to the bug form yourself.{}\n",
        if dumps > 0 {
            " Memory dumps (.dmp) are left out; they can't be redacted."
        } else {
            ""
        }
    );
    if !host.ask_yes(out, "Make a crash report?") {
        let _ = writeln!(out, "Crash:    no report made");
        return;
    }
    let mut items: Vec<(String, String)> = vec![
        (format!("{here}\\data\\mod.log"), "mod.log".into()),
        (format!("{here}\\data\\launcher.log"), "launcher.log".into()),
        (format!("{channel_dir}\\Game.log"), "Game.log".into()),
    ];
    let crash_root = format!("{channel_dir}\\Crashes\\");
    for f in &crash {
        if !is_text_report(f) {
            continue;
        }
        let rel = f.get(crash_root.len()..).unwrap_or(f).replace('\\', "_");
        items.push((f.clone(), format!("Crashes_{rel}")));
    }
    let readme =
        format!("sc-offline {SCO_VERSION} crash report\r\nGame: {game_build}\r\n{REDACTED_NOTE}");
    let bundle = write_bundle(
        host,
        &BundleSpec {
            dir: &format!("{here}\\data\\crash-reports"),
            name: "sc-offline-crash",
            files: &items,
            texts: &[],
            readme: &readme,
        },
    );
    let (put, zipped, made) = (bundle.names.len(), bundle.zipped, bundle.made);
    let ids_dropped = bundle.handles_dropped;
    let _ = writeln!(
        out,
        "Crash:    report saved: {made} ({put} file{}{})",
        plural(put),
        if zipped {
            ""
        } else {
            "; tar.exe failed, so it is a folder - zip it yourself"
        }
    );
    let _ = writeln!(out, "          Look through it before you share it: the redaction covers known shapes only, so a name in free text can remain.");
    if ids_dropped {
        let _ = writeln!(out, "          The logs held more handles than the redactor keeps, so a handle may be left in free text: read every file.");
    }
    if !host.ask_yes(
        out,
        "Open the bug form in your browser (you attach the zip there)?",
    ) {
        return;
    }
    let wine = host.system().on_wine();
    let url = format!(
        "https://github.com/scuba-sc/sc-offline/issues/new?template=bug.yml&title={}&version={}&game={}&os={}",
        url_encode("Crash: "),
        url_encode(SCO_VERSION),
        url_encode(game_build),
        url_encode(if wine { "Linux (Wine/Proton)" } else { "Windows" })
    );
    host.open_url(&url);
    host.reveal_file(&made);
}
