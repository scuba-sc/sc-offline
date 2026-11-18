// SPDX-License-Identifier: GPL-3.0-only
//! Install (local folder or zip, catalogue), update and remove.
//!
//! Every install follows the same road: the zip is copied into a private staging folder and
//! verified there (`scol-plugins`, and for the catalogue also size and SHA-256 against the signed
//! index), only the files the verifier reported are extracted from that copy into a staged `new`
//! folder, the empty `disabled` marker is added, and [`crate::txn::apply`] swaps the folder in and
//! rewrites `installed.json` as one journaled unit. A plugin always lands disabled.

use std::path::{Path, PathBuf};

use scol_catalogue::fetch::{self, Fetcher};
use scol_catalogue::index::{open_index, AcceptedIndex, Entry, LastAccepted};
use scol_catalogue::install::{
    plan_install, stage_download, InstalledFile, InstalledRecord, LocalState, PlanOptions,
    Replaces, DISABLED_MARKER, LUA_WARNING, NATIVE_WARNING,
};
use scol_catalogue::policy::{self, offered};
use scol_catalogue::semver::Version;
use scol_plugins::manifest::{self, HostApi, Kind};
use scol_plugins::verify::{self, Report};

use crate::cli::{Ctx, Io};
use crate::layout::{utc_text, valid_id, Layout};
use crate::store;
use crate::txn::{self, Change, TxnError};
use crate::zipx;

/// Hosts the catalogue may be fetched from (index and zip), and the one extra host a release
/// download may redirect to for its last hop.
pub const ALLOWED_HOSTS: [&str; 1] = ["github.com"];
pub const REDIRECT_HOSTS: [&str; 1] = ["objects.githubusercontent.com"];

#[derive(Debug, Clone, Copy, Default)]
pub struct Opts {
    /// Replace a plugin that is already there (a local install over anything, or a catalogue
    /// install over a folder the launcher did not install).
    pub replace: bool,
    /// Allow an older version over a newer catalogue install.
    pub allow_downgrade: bool,
}

/// Where the catalogue comes from: a fetcher and the two URLs.
pub struct Source<'a> {
    pub fetcher: &'a mut dyn Fetcher,
    pub index_url: String,
    pub sig_url: String,
}

fn warn_for(kind: Kind, io: &mut dyn Io) {
    match kind {
        Kind::Native => io.out(&format!("warning: {NATIVE_WARNING}")),
        Kind::Lua => io.out(&format!("warning: {LUA_WARNING}")),
        Kind::Data => {}
    }
}

/// Checks every changing command starts with: the game is not running, and an interrupted
/// earlier change is rolled back.
pub fn begin(ctx: &Ctx, io: &mut dyn Io) -> Result<(), String> {
    if (ctx.game_running)() {
        return Err("the game is running; close it first".into());
    }
    if let Some(line) = txn::recover(&ctx.layout)? {
        io.out(&line);
    }
    Ok(())
}

fn incoming(l: &Layout) -> PathBuf {
    l.launcher().join("staging").join(".incoming")
}

fn clean(l: &Layout, id: &str) {
    let _ = std::fs::remove_dir_all(l.staging(id));
    let _ = std::fs::remove_dir_all(incoming(l));
}

fn txn_text(e: TxnError) -> String {
    match e {
        TxnError::Failed(m) => m,
        TxnError::Crashed => "stopped by a test fault".into(),
    }
}

fn no_link(l: &Layout, id: &str) -> Result<(), String> {
    for p in [l.plugins(), l.launcher(), l.plugin_dir(id)] {
        if store::is_link(&p) {
            return Err(format!("{} is a link; not touching it", p.display()));
        }
    }
    Ok(())
}

/// Extract the staged zip into `staging/<id>/new` and add the disabled marker.
fn extract_new(l: &Layout, id: &str, zip: &Path, report: &Report) -> Result<(), String> {
    let new = l.staging(id).join("new");
    let _ = std::fs::remove_dir_all(&new);
    std::fs::create_dir_all(&new).map_err(|e| format!("cannot create {}: {e}", new.display()))?;
    zipx::extract_verified(zip, report, &new)?;
    std::fs::File::create(new.join(DISABLED_MARKER))
        .map_err(|e| format!("cannot create the disabled marker: {e}"))?;
    Ok(())
}

// ---- local install --------------------------------------------------------------------------

/// Install from a folder (a folder with `plugin.ini` in it) or from a plugin zip. Lands disabled,
/// recorded as `local`, unverified by any index.
pub fn install_local(ctx: &Ctx, src: &Path, opts: Opts, io: &mut dyn Io) -> Result<(), String> {
    let l = &ctx.layout;
    begin(ctx, io)?;
    let _tidy = store::Tidy::new(l);
    let inc = incoming(l);
    let _ = std::fs::remove_dir_all(&inc);
    std::fs::create_dir_all(&inc).map_err(|e| format!("cannot create {}: {e}", inc.display()))?;
    let staged = inc.join("package.zip");
    let result = install_local_inner(ctx, src, opts, &staged, io);
    let _ = std::fs::remove_dir_all(&inc);
    result
}

fn install_local_inner(
    ctx: &Ctx,
    src: &Path,
    opts: Opts,
    staged: &Path,
    io: &mut dyn Io,
) -> Result<(), String> {
    let l = &ctx.layout;
    let meta = std::fs::symlink_metadata(src)
        .map_err(|e| format!("cannot read {}: {e}", src.display()))?;
    if meta.file_type().is_symlink() {
        return Err(format!("{} is a link; not following it", src.display()));
    }
    if meta.is_dir() {
        let ini = std::fs::read(src.join("plugin.ini"))
            .map_err(|e| format!("{} has no readable plugin.ini: {e}", src.display()))?;
        let m = manifest::parse(&ini, None)
            .map_err(|e| format!("plugin.ini line {}: {}", e.line, e.message))?;
        if !valid_id(&m.id) {
            return Err(format!("{:?} is not a plugin id", m.id));
        }
        let members = zipx::collect_folder(src, &m.id, DISABLED_MARKER)?;
        zipx::write_stored_zip(staged, &members)?;
    } else if meta.is_file() {
        if meta.len() > verify::MAX_ARCHIVE_BYTES {
            return Err("the zip is over the size limit".into());
        }
        std::fs::copy(src, staged).map_err(|e| format!("cannot copy {}: {e}", src.display()))?;
    } else {
        return Err(format!("{} is neither a folder nor a file", src.display()));
    }
    // Everything after this point looks only at the staged copy.
    let report = verify::verify_archive(staged)
        .map_err(|e| format!("the plugin package was refused: {e}"))?;
    let m = &report.manifest;
    if !valid_id(&m.id) || report.plugin.folder != m.id {
        return Err(format!(
            "plugin id {:?} is not usable as a folder name",
            m.id
        ));
    }
    if policy::is_reserved(&m.id) {
        return Err(format!("{} is an id the host keeps for itself", m.id));
    }
    if m.api_major != ctx.host.api_major || m.api_minor > ctx.host.api_minor {
        return Err(format!(
            "plugin api {} does not fit this launcher (api {}.{})",
            m.api, ctx.host.api_major, ctx.host.api_minor
        ));
    }
    let id = m.id.clone();
    no_link(l, &id)?;
    let installed = store::read_installed(l)?;
    let there = installed.find(&id).is_some() || store::plugin_folders(l).contains(&id);
    if there && !opts.replace {
        return Err(format!(
            "plugin {id} is already there; use --replace to replace it (its settings are kept)"
        ));
    }
    let stage = l.staging(&id);
    let _ = std::fs::remove_dir_all(&stage);
    std::fs::create_dir_all(&stage)
        .map_err(|e| format!("cannot create {}: {e}", stage.display()))?;
    let zip = stage.join("package.zip");
    std::fs::rename(staged, &zip).map_err(|e| format!("cannot stage the package: {e}"))?;
    let prepared = (|| {
        extract_new(l, &id, &zip, &report)?;
        let record = InstalledRecord {
            id: id.clone(),
            version: m.version.clone(),
            source: "local".into(),
            zip_sha256: None,
            index_digest: None,
            index_sequence: None,
            installed: utc_text(ctx.now),
            state: "disabled".into(),
        };
        Ok::<_, String>(installed.with_record(record).render())
    })();
    let after = match prepared {
        Ok(a) => a,
        Err(e) => {
            clean(l, &id);
            return Err(e);
        }
    };
    // From here the journal owns the staging folder: it rolls back on failure and recovery
    // rolls back after a crash, so nothing is cleaned by hand.
    txn::apply(
        l,
        &Change {
            id: &id,
            has_new: true,
            installed_after: &after,
        },
        ctx.fault,
    )
    .map_err(txn_text)?;
    io.out(&format!(
        "installed {id} {} ({} plugin, {} files) from {}; it is disabled",
        m.version,
        m.kind.as_str(),
        report.artifacts.len(),
        if meta.is_dir() { "a folder" } else { "a zip" }
    ));
    io.out("this package was not checked against any signed index");
    warn_for(m.kind, io);
    io.out(&format!("turn it on with: plugins enable {id}"));
    Ok(())
}

// ---- remove ---------------------------------------------------------------------------------

/// Remove a plugin folder and its record. Settings (`data/storage/<id>.db`) stay unless `purge`.
pub fn remove(ctx: &Ctx, id: &str, purge: bool, io: &mut dyn Io) -> Result<(), String> {
    let l = &ctx.layout;
    if !valid_id(id) {
        return Err(format!(
            "{id:?} is not a plugin id (1 to 31 of a-z, 0-9, _)"
        ));
    }
    begin(ctx, io)?;
    let _tidy = store::Tidy::new(l);
    no_link(l, id)?;
    let installed = store::read_installed(l)?;
    let dir = l.plugin_dir(id);
    let has_dir = dir.is_dir();
    if !has_dir && installed.find(id).is_none() {
        return Err(format!("no plugin {id} is installed"));
    }
    let after = InstalledFile {
        plugins: installed
            .plugins
            .iter()
            .filter(|r| r.id != id)
            .cloned()
            .collect(),
        ..installed.clone()
    };
    txn::apply(
        l,
        &Change {
            id,
            has_new: false,
            installed_after: &after.render(),
        },
        ctx.fault,
    )
    .map_err(txn_text)?;
    io.out(&format!("removed {id}"));
    let stores: Vec<PathBuf> = l
        .storage_files(id)
        .into_iter()
        .filter(|p| p.exists())
        .collect();
    if purge {
        for p in &stores {
            std::fs::remove_file(p).map_err(|e| format!("cannot delete {}: {e}", p.display()))?;
        }
        io.out(&format!("purged {} settings file(s)", stores.len()));
    } else if !stores.is_empty() {
        io.out("its settings were kept; use --purge to delete them too");
    }
    Ok(())
}

// ---- catalogue ------------------------------------------------------------------------------

/// Fetch the index, check it against the trust root and the remembered sequence, and remember
/// it. The first accepted index is pinned. Read-only except for `catalogue-state.json`.
pub fn open_catalogue(
    ctx: &Ctx,
    src: &mut Source,
    io: &mut dyn Io,
) -> Result<AcceptedIndex, String> {
    let l = &ctx.layout;
    let last = store::read_catalogue_state(l)?;
    let (bytes, sig) = fetch::fetch_index(
        src.fetcher,
        &src.index_url,
        &src.sig_url,
        &ALLOWED_HOSTS,
        &REDIRECT_HOSTS,
    )
    .map_err(|e| format!("refused ({}): {e}", e.code()))?;
    let acc = open_index(&bytes, &sig, &ctx.trust, ctx.now, last.as_ref())
        .map_err(|e| format!("refused ({}): {e}", e.code()))?;
    store::write_catalogue_state(
        l,
        &LastAccepted {
            sequence: acc.index.sequence,
            digest: acc.digest.clone(),
        },
    )?;
    match &last {
        None => io.out(&format!(
            "first index seen: sequence {} is now pinned; older ones will be refused",
            acc.index.sequence
        )),
        Some(p) if acc.index.sequence > p.sequence => io.out(&format!(
            "index sequence {} accepted (was {})",
            acc.index.sequence, p.sequence
        )),
        _ => {}
    }
    Ok(acc)
}

pub fn catalogue_list(ctx: &Ctx, src: &mut Source, io: &mut dyn Io) -> Result<(), String> {
    let acc = open_catalogue(ctx, src, io)?;
    let installed = store::read_installed(&ctx.layout)?;
    io.out(&format!(
        "index sequence {}, issued {}, expires {}, digest {}",
        acc.index.sequence, acc.index.issued, acc.index.expires, acc.digest
    ));
    for o in offered(&acc.index, &ctx.host) {
        let e = o.entry;
        io.out(&format!(
            "{} {} ({}), plugin api {}, SDK {}, licence {}, maintainers {}, review {}",
            e.id,
            e.version,
            e.kind.as_str(),
            e.api,
            e.sdk,
            e.license,
            e.maintainers.join(", "),
            e.review.as_str()
        ));
        io.out(&format!("    evidence: {}", e.evidence));
        let here = match installed.find(&e.id) {
            Some(r) => format!("installed here: {} ({})", r.version, r.source),
            None => "not installed".into(),
        };
        match o.compatible {
            Ok(()) => io.out(&format!(
                "    {here}; can be installed here (it starts disabled)"
            )),
            Err(err) => io.out(&format!("    {here}; cannot be installed here: {err}")),
        }
    }
    Ok(())
}

/// Download, verify and install one entry of an accepted index.
fn install_entry(
    ctx: &Ctx,
    src: &mut Source,
    acc: &AcceptedIndex,
    entry: &Entry,
    opts: Opts,
    io: &mut dyn Io,
) -> Result<(), String> {
    let l = &ctx.layout;
    let id = entry.id.clone();
    let refuse = |e: scol_catalogue::Error| format!("refused ({}): {e}", e.code());
    policy::check_entry(entry, &ctx.host).map_err(refuse)?;
    let _tidy = store::Tidy::new(l);
    no_link(l, &id)?;
    let inc = incoming(l);
    let _ = std::fs::remove_dir_all(&inc);
    std::fs::create_dir_all(&inc).map_err(|e| format!("cannot create {}: {e}", inc.display()))?;
    let res = (|| {
        let download = inc.join("download.zip");
        fetch::download_zip(
            src.fetcher,
            entry,
            &download,
            &ALLOWED_HOSTS,
            &REDIRECT_HOSTS,
        )
        .map_err(refuse)?;
        let stage = l.staging(&id);
        let _ = std::fs::remove_dir_all(&stage);
        let (zip, report) = stage_download(&download, entry, &stage).map_err(refuse)?;
        let state = LocalState {
            installed: store::read_installed(l)?,
            folders: store::plugin_folders(l),
        };
        let plan = plan_install(
            entry,
            &report,
            &acc.digest,
            acc.index.sequence,
            &ctx.host,
            &state,
            PlanOptions {
                replace_local: opts.replace,
                allow_downgrade: opts.allow_downgrade,
            },
            &l.root,
            &utc_text(ctx.now),
        )
        .map_err(refuse)?;
        extract_new(l, &id, &zip, &report)?;
        Ok::<_, String>(plan)
    })();
    let _ = std::fs::remove_dir_all(&inc);
    let plan = match res {
        Ok(p) => p,
        Err(e) => {
            clean(l, &id);
            return Err(e);
        }
    };
    txn::apply(
        l,
        &Change {
            id: &id,
            has_new: true,
            installed_after: &plan.installed_json_after,
        },
        ctx.fault,
    )
    .map_err(txn_text)?;
    io.out(&format!(
        "installed {} {} ({} plugin, {} files) from index sequence {}; it is disabled",
        plan.id,
        plan.version,
        plan.kind.as_str(),
        plan.files.len(),
        acc.index.sequence
    ));
    match &plan.replaces {
        Replaces::Nothing => {}
        Replaces::Catalogue(v) => io.out(&format!("replaced catalogue install {v}")),
        Replaces::Local => io.out("replaced a local folder (its settings were kept)"),
    }
    for w in &plan.warnings {
        io.out(&format!("warning: {w}"));
    }
    io.out(&format!("turn it on with: plugins enable {}", plan.id));
    Ok(())
}

pub fn catalogue_install(
    ctx: &Ctx,
    src: &mut Source,
    id: &str,
    opts: Opts,
    io: &mut dyn Io,
) -> Result<(), String> {
    begin(ctx, io)?;
    let acc = open_catalogue(ctx, src, io)?;
    let Some(entry) = acc.index.find(id) else {
        return Err(format!(
            "refused (not-in-index): the index has no plugin {id}"
        ));
    };
    let entry = entry.clone();
    install_entry(ctx, src, &acc, &entry, opts, io)
}

/// Update catalogue installs whose index entry is newer. `only` limits it to one id. A plugin
/// the launcher did not install from the catalogue is never touched.
pub fn catalogue_update(
    ctx: &Ctx,
    src: &mut Source,
    only: Option<&str>,
    opts: Opts,
    io: &mut dyn Io,
) -> Result<(), String> {
    begin(ctx, io)?;
    let acc = open_catalogue(ctx, src, io)?;
    let installed = store::read_installed(&ctx.layout)?;
    let mut targets: Vec<(Entry, String)> = Vec::new();
    for rec in installed.plugins.iter().filter(|r| r.source == "catalogue") {
        if only.is_some_and(|o| o != rec.id) {
            continue;
        }
        match acc.index.find(&rec.id) {
            None => io.out(&format!("{}: not in this index, left alone", rec.id)),
            Some(e) => {
                let newer = match (Version::parse(&e.version), Version::parse(&rec.version)) {
                    (Some(n), Some(o)) => n > o,
                    _ => e.version != rec.version,
                };
                if newer {
                    targets.push((e.clone(), rec.version.clone()));
                } else {
                    io.out(&format!("{}: up to date ({})", rec.id, rec.version));
                }
            }
        }
    }
    if let Some(o) = only {
        if installed.find(o).is_none_or(|r| r.source != "catalogue") {
            return Err(format!("{o} was not installed from the catalogue"));
        }
    }
    let mut failed = 0;
    for (e, old) in targets {
        io.out(&format!("updating {} {old} -> {}", e.id, e.version));
        let mut o = opts;
        o.replace = false;
        if let Err(m) = install_entry(ctx, src, &acc, &e, o, io) {
            io.err(&format!("{}: {m}", e.id));
            failed += 1;
        }
    }
    if failed > 0 {
        return Err(format!("{failed} update(s) failed"));
    }
    Ok(())
}

/// Manifest check used by tests and the CLI to read a folder's plugin.ini without installing.
pub fn read_manifest(dir: &Path, host: HostApi) -> Result<manifest::Manifest, String> {
    let b = std::fs::read(dir.join("plugin.ini")).map_err(|e| format!("plugin.ini: {e}"))?;
    manifest::parse(&b, Some(host))
        .map_err(|e| format!("plugin.ini line {}: {}", e.line, e.message))
}
