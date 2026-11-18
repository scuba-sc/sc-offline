// SPDX-License-Identifier: GPL-3.0-only
//! The `plugins` command line, as a library entry the launcher CLI calls.
//!
//! ```text
//! plugins [--root <launcher folder>] list [--json]
//! plugins enable <id>
//! plugins disable <id>
//! plugins install <folder|zip> [--replace]
//! plugins remove <id> [--purge]
//! plugins catalogue list    --source <folder>
//! plugins catalogue install <id> --source <folder> [--replace] [--allow-downgrade]
//! plugins catalogue update [<id>] --source <folder> [--allow-downgrade]
//! plugins update [<id>] --source <folder>
//! ```
//!
//! `--root` is the launcher folder (the one that holds `data\`); it defaults to the current
//! folder. `--source` is a local catalogue folder holding `index.json`, `index.json.sig` and the
//! zips (this build has no network fetcher; the catalogue code goes through the
//! [`scol_catalogue::fetch::Fetcher`] trait, so one can be added). Exit code 0 when the command
//! worked, 1 when it refused or the arguments are wrong.

use std::path::{Path, PathBuf};

use scol_catalogue::policy::Host;
use scol_catalogue::semver::Version;
use scol_catalogue::trust::TrustRoot;

use crate::fetcher::{FileFetcher, INDEX_URL, SIG_URL};
use crate::install::{self, Opts, Source};
use crate::layout::Layout;
use crate::list;
use crate::store;
use crate::txn::Fault;

pub const USAGE: &str = "usage:\n\
\x20 plugins [--root <launcher folder>] list [--json]\n\
\x20 plugins enable <id>\n\
\x20 plugins disable <id>\n\
\x20 plugins install <folder|zip> [--replace]\n\
\x20 plugins remove <id> [--purge]\n\
\x20 plugins catalogue list --source <folder>\n\
\x20 plugins catalogue install <id> --source <folder> [--replace] [--allow-downgrade]\n\
\x20 plugins catalogue update [<id>] --source <folder> [--allow-downgrade]\n\
\x20 plugins update [<id>] --source <folder> [--allow-downgrade]";

/// Where the command writes.
pub trait Io {
    fn out(&mut self, line: &str);
    fn err(&mut self, line: &str);
}

/// Prints to the console.
pub struct Console;

impl Io for Console {
    fn out(&mut self, line: &str) {
        println!("{line}");
    }
    fn err(&mut self, line: &str) {
        eprintln!("{line}");
    }
}

/// Collects output, for tests.
#[derive(Default)]
pub struct Capture {
    pub out: Vec<String>,
    pub err: Vec<String>,
}

impl Io for Capture {
    fn out(&mut self, line: &str) {
        self.out.push(line.to_string());
    }
    fn err(&mut self, line: &str) {
        self.err.push(line.to_string());
    }
}

impl Capture {
    pub fn text(&self) -> String {
        self.out.join("\n")
    }
}

/// Everything a command needs from outside. Tests build one with a fake clock, a fake game probe
/// and a test trust root; [`Ctx::real`] is what the binary uses.
#[derive(Clone)]
pub struct Ctx {
    pub layout: Layout,
    /// Seconds since 1970-01-01T00:00:00Z.
    pub now: i64,
    pub game_running: fn() -> bool,
    pub trust: TrustRoot,
    pub host: Host,
    pub fault: Fault,
}

impl Ctx {
    /// The real clock, the real process check, the compiled-in trust root and the host this
    /// launcher is built against: plugin api 1.1, SDK 2.4.1.
    pub fn real(root: &Path) -> Ctx {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_or(0, |d| d.as_secs() as i64);
        Ctx {
            layout: Layout::new(root),
            now,
            game_running: store::game_running,
            trust: TrustRoot::embedded(),
            host: Host {
                api_major: 1,
                api_minor: 1,
                sdk: Version::parse("2.4.1").expect("constant"),
            },
            fault: Fault::None,
        }
    }
}

struct Args {
    pos: Vec<String>,
    root: Option<PathBuf>,
    source: Option<String>,
    json: bool,
    purge: bool,
    replace: bool,
    allow_downgrade: bool,
    test_trust_key: Option<String>,
}

fn parse(args: &[String]) -> Result<Args, String> {
    let mut a = Args {
        pos: Vec::new(),
        root: None,
        source: None,
        json: false,
        purge: false,
        replace: false,
        allow_downgrade: false,
        test_trust_key: None,
    };
    let mut it = args.iter();
    while let Some(s) = it.next() {
        let mut value = |n: &str| {
            it.next()
                .cloned()
                .ok_or_else(|| format!("{n} needs a value"))
        };
        match s.as_str() {
            "--root" => a.root = Some(PathBuf::from(value("--root")?)),
            "--source" => a.source = Some(value("--source")?),
            "--json" => a.json = true,
            "--purge" => a.purge = true,
            "--replace" => a.replace = true,
            "--allow-downgrade" => a.allow_downgrade = true,
            "--test-trust-key" => a.test_trust_key = Some(value("--test-trust-key")?),
            x if x.starts_with("--") => return Err(format!("unknown option {x}")),
            _ => a.pos.push(s.clone()),
        }
    }
    Ok(a)
}

fn source_dir(s: &str) -> PathBuf {
    let t = s
        .strip_prefix("file:///")
        .or_else(|| s.strip_prefix("file://"))
        .unwrap_or(s);
    PathBuf::from(t)
}

/// Trust root for a proof run: only with the `test-trust` feature, and only the one key named.
#[cfg(feature = "test-trust")]
fn apply_test_key(ctx: &mut Ctx, file: &str) -> Result<(), String> {
    let t = std::fs::read_to_string(file).map_err(|e| format!("cannot read {file}: {e}"))?;
    ctx.trust = TrustRoot::from_hex_keys(&[t.trim()])
        .ok_or("the test trust key is not 64 lower-case hex digits")?;
    Ok(())
}

#[cfg(not(feature = "test-trust"))]
fn apply_test_key(_ctx: &mut Ctx, _file: &str) -> Result<(), String> {
    Err("--test-trust-key needs a build with the test-trust feature".into())
}

/// Run with the real environment. `args` excludes the program name and the word `plugins`.
pub fn run(args: &[String]) -> i32 {
    let root = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    run_with(args, &mut Console, &Ctx::real(&root))
}

/// Run against a given context. `--root` on the command line overrides `base.layout.root`.
pub fn run_with(args: &[String], io: &mut dyn Io, base: &Ctx) -> i32 {
    match dispatch(args, io, base) {
        Ok(()) => 0,
        Err(m) => {
            io.err(&m);
            1
        }
    }
}

fn need<'a>(a: &'a Args, n: usize, what: &str) -> Result<&'a [String], String> {
    if a.pos.len() != n + 1 {
        return Err(format!("{what}\n{USAGE}"));
    }
    Ok(&a.pos[1..])
}

fn dispatch(args: &[String], io: &mut dyn Io, base: &Ctx) -> Result<(), String> {
    let a = parse(args).map_err(|m| format!("{m}\n{USAGE}"))?;
    let mut ctx = base.clone();
    if let Some(r) = &a.root {
        ctx.layout = Layout::new(r);
    }
    if let Some(k) = &a.test_trust_key {
        apply_test_key(&mut ctx, k)?;
    }
    let Some(cmd) = a.pos.first().map(String::as_str) else {
        return Err(USAGE.into());
    };
    let opts = Opts {
        replace: a.replace,
        allow_downgrade: a.allow_downgrade,
    };
    match cmd {
        "list" => {
            need(&a, 0, "list takes no argument")?;
            list::list(&ctx, a.json, io)
        }
        "enable" | "disable" => {
            let r = need(&a, 1, "give one plugin id")?;
            list::set_enabled(&ctx, &r[0], cmd == "enable", io)
        }
        "install" => {
            let r = need(&a, 1, "give one folder or zip")?;
            install::install_local(&ctx, Path::new(&r[0]), opts, io)
        }
        "remove" => {
            let r = need(&a, 1, "give one plugin id")?;
            install::remove(&ctx, &r[0], a.purge, io)
        }
        "update" => catalogue_cmd(&ctx, &a, "update", &a.pos[1..], opts, io),
        "catalogue" => {
            let sub = a.pos.get(1).map(String::as_str).unwrap_or("");
            let rest = if a.pos.len() > 2 {
                &a.pos[2..]
            } else {
                &[][..]
            };
            catalogue_cmd(&ctx, &a, sub, rest, opts, io)
        }
        other => Err(format!("unknown command {other}\n{USAGE}")),
    }
}

fn catalogue_cmd(
    ctx: &Ctx,
    a: &Args,
    sub: &str,
    rest: &[String],
    opts: Opts,
    io: &mut dyn Io,
) -> Result<(), String> {
    let want_args = match sub {
        "list" => (0, 0),
        "install" => (1, 1),
        "update" => (0, 1),
        _ => return Err(format!("unknown catalogue command {sub:?}\n{USAGE}")),
    };
    if rest.len() < want_args.0 || rest.len() > want_args.1 {
        return Err(format!("wrong number of arguments\n{USAGE}"));
    }
    let Some(s) = &a.source else {
        return Err(format!(
            "--source <folder> is required (this build fetches from a local catalogue folder only)\n{USAGE}"
        ));
    };
    let mut fetcher = FileFetcher::new(&source_dir(s));
    let mut src = Source {
        fetcher: &mut fetcher,
        index_url: INDEX_URL.into(),
        sig_url: SIG_URL.into(),
    };
    match sub {
        "list" => install::catalogue_list(ctx, &mut src, io),
        "install" => install::catalogue_install(ctx, &mut src, &rest[0], opts, io),
        _ => install::catalogue_update(ctx, &mut src, rest.first().map(String::as_str), opts, io),
    }
}
