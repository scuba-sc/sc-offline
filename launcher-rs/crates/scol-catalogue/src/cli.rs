// SPDX-License-Identifier: GPL-3.0-only
//! The catalogue commands, as a library entry the launcher CLI can call. Offline and read-only:
//! these commands read files you name, print, and exit. They fetch nothing and write nothing.
//!
//! ```text
//! catalogue list <index.json> <index.json.sig> --sdk <version> --api <major.minor> [--last-sequence <n> --last-digest <sha256>]
//! catalogue plan <index.json> <index.json.sig> <id> <plugin.zip> --sdk <version> --api <major.minor>
//!                --root <launcher folder> [--last-sequence <n> --last-digest <sha256>] [--replace-local] [--allow-downgrade]
//! ```
//!
//! `list` verifies the index and prints the entries a launcher offers. `plan` also checks the zip
//! and prints the install plan. Exit code 0 when the command worked, 1 when it refused or the
//! arguments are wrong. The trust root is the one compiled in ([`crate::trust::PRODUCTION_KEYS`]);
//! [`run_with`] takes another root for tests.

use std::path::{Path, PathBuf};

use crate::error::Error;
use crate::index::{open_index, Entry, LastAccepted};
use crate::install::{
    plan_install, verify_download, InstalledFile, LocalState, PlanOptions, Replaces,
};
use crate::policy::{offered, Host};
use crate::semver::Version;
use crate::trust::TrustRoot;

/// Where the command writes, and the clock. The launcher passes its console; tests pass a buffer.
pub trait Io {
    fn out(&mut self, line: &str);
    fn err(&mut self, line: &str);
    /// Seconds since 1970-01-01T00:00:00Z.
    fn now_unix(&self) -> i64;
}

pub const USAGE: &str = "usage:\n\
\x20 catalogue list <index.json> <index.json.sig> --sdk <version> --api <major.minor> [--last-sequence <n> --last-digest <sha256>]\n\
\x20 catalogue plan <index.json> <index.json.sig> <id> <plugin.zip> --sdk <version> --api <major.minor> --root <folder>\n\
\x20                [--last-sequence <n> --last-digest <sha256>] [--replace-local] [--allow-downgrade]";

/// Run with the compiled-in trust root.
pub fn run(args: &[String], io: &mut dyn Io) -> i32 {
    run_with(args, io, &TrustRoot::embedded())
}

struct Opts {
    positional: Vec<String>,
    sdk: Option<String>,
    api: Option<String>,
    root: Option<PathBuf>,
    last_sequence: Option<u64>,
    last_digest: Option<String>,
    replace_local: bool,
    allow_downgrade: bool,
}

fn parse_opts(args: &[String]) -> Result<Opts, String> {
    let mut o = Opts {
        positional: Vec::new(),
        sdk: None,
        api: None,
        root: None,
        last_sequence: None,
        last_digest: None,
        replace_local: false,
        allow_downgrade: false,
    };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        let mut value = |name: &str| {
            it.next()
                .cloned()
                .ok_or_else(|| format!("{name} needs a value"))
        };
        match a.as_str() {
            "--sdk" => o.sdk = Some(value("--sdk")?),
            "--api" => o.api = Some(value("--api")?),
            "--root" => o.root = Some(PathBuf::from(value("--root")?)),
            "--last-sequence" => {
                o.last_sequence = Some(
                    value("--last-sequence")?
                        .parse()
                        .map_err(|_| "--last-sequence must be a number".to_string())?,
                )
            }
            "--last-digest" => o.last_digest = Some(value("--last-digest")?),
            "--replace-local" => o.replace_local = true,
            "--allow-downgrade" => o.allow_downgrade = true,
            s if s.starts_with("--") => return Err(format!("unknown option {s}")),
            _ => o.positional.push(a.clone()),
        }
    }
    Ok(o)
}

fn host_from(o: &Opts) -> Result<Host, String> {
    let sdk = o.sdk.as_deref().ok_or("--sdk is required")?;
    let api = o.api.as_deref().ok_or("--api is required")?;
    let sdk = Version::parse(sdk).ok_or("--sdk is not a version like 2.4.1")?;
    let (a, b) = api.split_once('.').ok_or("--api must be <major>.<minor>")?;
    let major = a.parse().map_err(|_| "--api must be <major>.<minor>")?;
    let minor = b.parse().map_err(|_| "--api must be <major>.<minor>")?;
    Ok(Host {
        api_major: major,
        api_minor: minor,
        sdk,
    })
}

fn read(path: &str) -> Result<Vec<u8>, Error> {
    std::fs::read(path).map_err(|e| Error::Io(format!("cannot read {path}: {e}")))
}

fn read_text(path: &str) -> Result<String, Error> {
    String::from_utf8(read(path)?).map_err(|_| Error::Io(format!("{path} is not UTF-8")))
}

fn local_state(root: &Path) -> Result<LocalState, Error> {
    let installed_path = root.join("data").join("launcher").join("installed.json");
    let installed = match std::fs::read(&installed_path) {
        Ok(b) => InstalledFile::parse(&b)?,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => InstalledFile::default(),
        Err(e) => {
            return Err(Error::Io(format!(
                "cannot read {}: {e}",
                installed_path.display()
            )))
        }
    };
    let mut folders = std::collections::BTreeSet::new();
    if let Ok(rd) = std::fs::read_dir(root.join("data").join("plugins")) {
        for e in rd.flatten() {
            if let Some(n) = e.file_name().to_str() {
                folders.insert(n.to_string());
            }
        }
    }
    Ok(LocalState { installed, folders })
}

fn utc_text(unix: i64) -> String {
    let days = unix.div_euclid(86_400);
    let secs = unix.rem_euclid(86_400);
    // Civil date from days (inverse of the algorithm in `time`).
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z",
        secs / 3600,
        secs % 3600 / 60,
        secs % 60
    )
}

fn describe(e: &Entry) -> String {
    format!(
        "{} {} ({}), plugin api {}, SDK {}, licence {}, maintainers {}, review {}\n    evidence: {}",
        e.id,
        e.version,
        e.kind.as_str(),
        e.api,
        e.sdk,
        e.license,
        e.maintainers.join(", "),
        e.review.as_str(),
        e.evidence
    )
}

fn refuse(io: &mut dyn Io, e: &Error) -> i32 {
    io.err(&format!("refused ({}): {e}", e.code()));
    1
}

/// Run a catalogue command against the given trust root. `args` excludes the program name and
/// starts with `list` or `plan` (the launcher CLI strips the word `catalogue`).
pub fn run_with(args: &[String], io: &mut dyn Io, trust: &TrustRoot) -> i32 {
    let Some((cmd, rest)) = args.split_first() else {
        io.err(USAGE);
        return 1;
    };
    let o = match parse_opts(rest) {
        Ok(o) => o,
        Err(m) => {
            io.err(&format!("{m}\n{USAGE}"));
            return 1;
        }
    };
    let want = match cmd.as_str() {
        "list" => 2,
        "plan" => 4,
        _ => {
            io.err(&format!("unknown catalogue command {cmd}\n{USAGE}"));
            return 1;
        }
    };
    if o.positional.len() != want {
        io.err(USAGE);
        return 1;
    }
    let host = match host_from(&o) {
        Ok(h) => h,
        Err(m) => {
            io.err(&format!("{m}\n{USAGE}"));
            return 1;
        }
    };
    let (bytes, sig) = match (read(&o.positional[0]), read_text(&o.positional[1])) {
        (Ok(b), Ok(s)) => (b, s),
        (Err(e), _) | (_, Err(e)) => return refuse(io, &e),
    };
    let now = io.now_unix();
    let last = match (o.last_sequence, &o.last_digest) {
        (None, None) => None,
        (Some(sequence), Some(digest)) => Some(LastAccepted {
            sequence,
            digest: digest.clone(),
        }),
        _ => {
            io.err(&format!(
                "--last-sequence and --last-digest go together\n{USAGE}"
            ));
            return 1;
        }
    };
    let accepted = match open_index(&bytes, &sig, trust, now, last.as_ref()) {
        Ok(a) => a,
        Err(e) => return refuse(io, &e),
    };
    if cmd == "list" {
        io.out(&format!(
            "index sequence {}, issued {}, expires {}, digest {}",
            accepted.index.sequence, accepted.index.issued, accepted.index.expires, accepted.digest
        ));
        for offer in offered(&accepted.index, &host) {
            io.out(&describe(offer.entry));
            match offer.compatible {
                Ok(()) => io.out("    can be installed here (it starts disabled)"),
                Err(e) => io.out(&format!("    cannot be installed here: {e}")),
            }
        }
        return 0;
    }
    // plan
    let Some(root) = o.root.clone() else {
        io.err(&format!("--root is required\n{USAGE}"));
        return 1;
    };
    let id = &o.positional[2];
    let Some(entry) = accepted.index.find(id) else {
        io.err(&format!(
            "refused (not-in-index): the index has no plugin {id}"
        ));
        return 1;
    };
    if let Err(e) = crate::policy::check_entry(entry, &host) {
        return refuse(io, &e);
    }
    let report = match verify_download(Path::new(&o.positional[3]), entry) {
        Ok(r) => r,
        Err(e) => return refuse(io, &e),
    };
    let state = match local_state(&root) {
        Ok(s) => s,
        Err(e) => return refuse(io, &e),
    };
    let opts = PlanOptions {
        replace_local: o.replace_local,
        allow_downgrade: o.allow_downgrade,
    };
    let plan = match plan_install(
        entry,
        &report,
        &accepted.digest,
        accepted.index.sequence,
        &host,
        &state,
        opts,
        &root,
        &utc_text(now),
    ) {
        Ok(p) => p,
        Err(e) => return refuse(io, &e),
    };
    io.out(&format!(
        "plan: install {} {} ({})",
        plan.id,
        plan.version,
        plan.kind.as_str()
    ));
    io.out(&format!("  stage in   {}", plan.staging_dir.display()));
    io.out(&format!("  install to {}", plan.target_dir.display()));
    io.out(&format!(
        "  marker     {} (created empty, the plugin starts disabled)",
        plan.disabled_marker.display()
    ));
    io.out(&format!("  record     {}", plan.installed_json.display()));
    io.out(&match &plan.replaces {
        Replaces::Nothing => "  replaces   nothing".to_string(),
        Replaces::Catalogue(v) => format!("  replaces   catalogue install {v}"),
        Replaces::Local => "  replaces   a local folder (confirmed)".to_string(),
    });
    io.out(&format!("  files      {}", plan.files.len()));
    for f in &plan.files {
        io.out(&format!(
            "    {}  {} bytes  {}",
            f.path, f.size_bytes, f.sha256
        ));
    }
    for w in &plan.warnings {
        io.out(&format!("  warning    {w}"));
    }
    io.out("nothing was written");
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn utc_text_round_trips() {
        for s in [
            "1970-01-01T00:00:00Z",
            "2000-03-01T00:00:00Z",
            "2024-02-29T23:59:59Z",
            "2026-11-17T12:30:45Z",
        ] {
            assert_eq!(utc_text(crate::time::parse_utc(s).unwrap()), s);
        }
    }
}
