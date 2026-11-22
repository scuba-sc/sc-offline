// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline.exe report`: a problem report made on request, for a bug that is not a crash.
//!
//! It is the crash report's bundle ([`crate::post::offer_crash_report`]) without the crash: the text
//! of `status`, `sc-offline.ini`, the plugin list, `data\mod.log`, `data\launcher.log`, the game's
//! `Game.log` and the text files of crashes from the last [`REPORT_DAYS`] days, each run through the
//! strict redactor, zipped into `data\reports`. Memory dumps are left out; they cannot be redacted.
//!
//! Nothing is sent. The player reads the zip and attaches it to a bug report. The game folder is only
//! read, so the command also works while the game runs. Tested with synthetic files only; not run in
//! game.

use crate::host::Host;
use crate::post::{
    files_in, is_text_report, plural, url_encode, write_bundle, BundleSpec, REDACTED_NOTE,
};
use scol_contract::{exit_code, files};
use scol_install::checks::game_build;
use scol_install::paths::parent_dir;
use scol_install::status::{fail, status_and_folder};
use scol_install::version::SCO_VERSION;
use std::io::Write;
use std::os::windows::fs::MetadataExt;

/// Crash text files older than this many days are left out.
pub const REPORT_DAYS: u64 = 7;
/// At most this many crash text files go in, the newest first.
pub const MAX_CRASH_TEXTS: usize = 32;
/// Where the bug form is. The launcher opens it only after a yes, and sends nothing itself.
pub const BUG_FORM: &str = "https://github.com/scuba-sc/sc-offline/issues/new?template=bug.yml";

const TICKS_PER_DAY: u64 = 864_000_000_000;

/// What `report` was asked.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ReportArgs {
    /// `--game <folder>`; empty when absent.
    pub game: String,
    /// False with `--no-game-log`: leave the game's own log out.
    pub game_log: bool,
}

/// The plugin folders under `data\plugins`: one line each with the folder, on or off, and the id,
/// version and kind its `plugin.ini` names. Only those three keys are read.
pub fn plugin_lines(here: &str) -> Vec<u8> {
    let dir = format!("{here}\\data\\plugins");
    let mut rows: Vec<String> = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&dir) {
        let mut entries: Vec<_> = entries.flatten().collect();
        entries.sort_by_key(|e| e.file_name());
        for e in entries {
            let name = e.file_name().to_string_lossy().into_owned();
            let is_dir = e
                .metadata()
                .map(|m| m.file_attributes() & 0x10 != 0)
                .unwrap_or(false);
            if !is_dir || name.starts_with('.') {
                continue;
            }
            let folder = format!("{dir}\\{name}");
            let Ok(ini) = std::fs::read(format!("{folder}\\plugin.ini")) else {
                continue;
            };
            let ini = String::from_utf8_lossy(&ini);
            let key = |k: &str| -> String {
                ini.lines()
                    .filter_map(|l| l.trim().split_once('='))
                    .find(|(name, _)| name.trim().eq_ignore_ascii_case(k))
                    .map(|(_, v)| v.trim().trim_matches('"').chars().take(64).collect())
                    .unwrap_or_default()
            };
            let off =
                std::fs::symlink_metadata(format!("{folder}\\{}", files::PLUGIN_DISABLED_MARKER))
                    .is_ok();
            rows.push(format!(
                "{name:<20} {:<4} id={} version={} kind={}",
                if off { "off" } else { "on" },
                key("id"),
                key("version"),
                key("kind"),
            ));
        }
    }
    let mut text = format!(
        "{} plugin folder{} in data\\plugins\r\n",
        rows.len(),
        plural(rows.len())
    );
    for r in rows {
        text.push_str(&r);
        text.push_str("\r\n");
    }
    text.into_bytes()
}

/// The text files under `Crashes` written since `since`, newest first, at most [`MAX_CRASH_TEXTS`];
/// and how many other files (dumps) were written there in that time.
fn crash_texts(channel_dir: &str, since: u64) -> (Vec<String>, usize) {
    let mut all = Vec::new();
    files_in(&format!("{channel_dir}\\Crashes"), since, true, &mut all);
    let dumps = all.iter().filter(|f| !is_text_report(f)).count();
    let mut texts: Vec<(u64, String)> = all
        .into_iter()
        .filter(|f| is_text_report(f))
        .map(|f| {
            let t = std::fs::metadata(&f)
                .map(|m| m.last_write_time())
                .unwrap_or(0);
            (t, f)
        })
        .collect();
    texts.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| a.1.cmp(&b.1)));
    texts.truncate(MAX_CRASH_TEXTS);
    (texts.into_iter().map(|(_, f)| f).collect(), dumps)
}

fn size_words(bytes: usize) -> String {
    if bytes < 1024 {
        format!("{bytes} B")
    } else if bytes < 1024 * 1024 {
        format!("{} KB", bytes.div_ceil(1024))
    } else {
        format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
    }
}

/// `report`: writes the bundle and says what is in it. Returns the exit code: 0 when a bundle was
/// written, 1 when nothing could be written.
pub fn make_report(host: &mut dyn Host, out: &mut dyn Write, here: &str, args: &ReportArgs) -> i32 {
    let _ = write!(out, "sc-offline launcher {SCO_VERSION}\n\n");
    let _ = writeln!(
        out,
        "Report:   collecting the status check and the logs (read-only; nothing is sent)"
    );
    let mut status = Vec::new();
    let (code, bin) = status_and_folder(here, &args.game, &mut status);
    if code != exit_code::OK {
        status.extend_from_slice(format!("\n(status ended with exit code {code})\n").as_bytes());
    }
    let (channel_dir, build) = if bin.is_empty() {
        (String::new(), String::new())
    } else {
        (parent_dir(&bin), game_build(&bin))
    };

    let mut items: Vec<(String, String)> = vec![
        (format!("{here}\\{}", files::INI), files::INI.into()),
        (format!("{here}\\data\\mod.log"), "mod.log".into()),
        (format!("{here}\\data\\launcher.log"), "launcher.log".into()),
    ];
    let mut dumps = 0;
    if !channel_dir.is_empty() {
        if args.game_log {
            items.push((format!("{channel_dir}\\Game.log"), "Game.log".into()));
        }
        let since = host.now_ticks().saturating_sub(REPORT_DAYS * TICKS_PER_DAY);
        let (texts, others) = crash_texts(&channel_dir, since);
        dumps = others;
        let root = format!("{channel_dir}\\Crashes\\");
        for f in texts {
            let rel = f.get(root.len()..).unwrap_or(&f).replace('\\', "_");
            items.push((f.clone(), format!("Crashes_{rel}")));
        }
    }
    let texts = vec![
        ("status.txt".to_string(), status),
        ("plugins.txt".to_string(), plugin_lines(here)),
    ];
    // Whether this is the build the release was checked on: the first thing a reader of the report asks.
    let check = scol_contract::game_builds::describe(&scol_contract::game_builds::fit(&build));
    let build_check = if check.is_empty() {
        String::new()
    } else {
        format!("Build check: {check}\r\n")
    };
    let readme = format!(
        "sc-offline {SCO_VERSION} problem report\r\nGame: {}\r\n{}Made by `sc-offline.exe report` on this PC. Nothing was sent anywhere: attach this zip to your bug report yourself.\r\n{}{REDACTED_NOTE}",
        if build.is_empty() { "not found" } else { &build },
        build_check,
        if args.game_log {
            ""
        } else {
            "Game.log was left out (--no-game-log).\r\n"
        },
    );
    let dir = format!("{here}\\data\\reports");
    let bundle = write_bundle(
        host,
        &BundleSpec {
            dir: &dir,
            name: "sc-offline-report",
            files: &items,
            texts: &texts,
            readme: &readme,
        },
    );
    if bundle.names.is_empty() {
        return fail(
            out,
            &format!("couldn't write the report into {dir} (is the folder read-only?)"),
        );
    }
    for (name, bytes) in &bundle.names {
        let _ = writeln!(out, "          {name:<28} {}", size_words(*bytes));
    }
    let n = bundle.names.len();
    let _ = writeln!(
        out,
        "Report:   saved: {} ({n} file{}{})",
        bundle.made,
        plural(n),
        if bundle.zipped {
            ""
        } else {
            "; tar.exe failed, so it is a folder - zip it yourself"
        }
    );
    if !args.game_log {
        let _ = writeln!(out, "          Game.log was left out (--no-game-log).");
    } else if channel_dir.is_empty() {
        let _ = writeln!(
            out,
            "          The game folder was not found, so Game.log and the crash files are not in it."
        );
    }
    if dumps > 0 {
        let _ = writeln!(
            out,
            "          Memory dumps (.dmp) are left out; they can't be redacted."
        );
    }
    let _ = writeln!(out, "          Your RSI handle, account numbers, e-mail address and Windows user name were replaced.");
    let _ = writeln!(out, "          Look through it before you share it: the redaction covers known shapes only, so a name in free text can remain.");
    if bundle.handles_dropped {
        let _ = writeln!(out, "          The logs held more handles than the redactor keeps, so a handle may be left in free text: read every file.");
    }
    let _ = writeln!(
        out,
        "          Nothing was sent. To report a problem, attach it to an issue yourself: {BUG_FORM}"
    );
    // The window opens the folder itself; a console is asked.
    if host.env(scol_contract::env::GUI).is_some() || !host.owns_console_input() {
        return exit_code::OK;
    }
    if host.ask_yes(
        out,
        "Open the bug form in your browser (you attach the zip there)?",
    ) {
        let wine = host.system().on_wine();
        host.open_url(&format!(
            "{BUG_FORM}&version={}&game={}&os={}",
            url_encode(SCO_VERSION),
            url_encode(&build),
            url_encode(if wine {
                "Linux (Wine/Proton)"
            } else {
                "Windows"
            })
        ));
        host.reveal_file(&bundle.made);
    }
    exit_code::OK
}
