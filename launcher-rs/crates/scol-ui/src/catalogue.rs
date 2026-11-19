// SPDX-License-Identifier: GPL-3.0-only
//! The Catalogue tab's data and the install step, through `scol-plugin-manager`.
//!
//! This release reads the catalogue from a local folder, `data\launcher\catalogue`, that holds
//! `index.json`, `index.json.sig` and the plugin zips. The index must be signed by a key in the trust
//! root. The shipped trust root is empty (no production signing key exists yet), so the tab says that
//! no catalogue is published for this build. Adding a key is a release-time step.
//! The window never runs plugin code; an install lands the plugin disabled.

use std::path::{Path, PathBuf};

use scol_catalogue::index::Entry;
use scol_catalogue::policy::offered;
use scol_plugin_manager::cli::{Capture, Ctx};
use scol_plugin_manager::fetcher::{FileFetcher, INDEX_URL, SIG_URL};
use scol_plugin_manager::install::{self, Opts, Source};
use scol_plugin_manager::store;

pub use crate::plugins::HOST_SDK;

#[derive(Debug, Clone)]
pub struct OfferRow {
    pub entry: Entry,
    /// None when installable here; otherwise why not.
    pub refused: Option<String>,
    /// The version installed from the catalogue, when there is one.
    pub installed: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Loaded {
    pub sequence: u64,
    pub expires: String,
    pub rows: Vec<OfferRow>,
}

/// What the Catalogue tab can show.
#[derive(Debug, Clone)]
pub enum State {
    /// The trust root has no key: no catalogue is published for this build.
    NoCatalogue,
    /// The catalogue folder has no index.
    NoIndex,
    /// The index was refused (bad signature, expired, older than one seen before ...).
    Refused(String),
    Loaded(Loaded),
}

pub fn dir(here: &Path) -> PathBuf {
    here.join("data").join("launcher").join("catalogue")
}

/// Read and check the index of the local catalogue folder.
pub fn load(ctx: &Ctx) -> State {
    if ctx.trust.is_empty() {
        return State::NoCatalogue;
    }
    let d = dir(&ctx.layout.root);
    if !d.join("index.json").is_file() || !d.join("index.json.sig").is_file() {
        return State::NoIndex;
    }
    let mut fetcher = FileFetcher::new(&d);
    let mut src = Source {
        fetcher: &mut fetcher,
        index_url: INDEX_URL.into(),
        sig_url: SIG_URL.into(),
    };
    let acc = match install::open_catalogue(ctx, &mut src, &mut Capture::default()) {
        Ok(a) => a,
        Err(e) => return State::Refused(e),
    };
    let installed = store::read_installed(&ctx.layout).unwrap_or_default();
    let rows = offered(&acc.index, &ctx.host)
        .into_iter()
        .map(|o| OfferRow {
            entry: o.entry.clone(),
            refused: o.compatible.err().map(|e| e.to_string()),
            installed: installed
                .find(&o.entry.id)
                .filter(|r| r.source == "catalogue")
                .map(|r| r.version.clone()),
        })
        .collect();
    State::Loaded(Loaded {
        sequence: acc.index.sequence,
        expires: acc.index.expires.clone(),
        rows,
    })
}

/// What an install did.
#[derive(Debug, Clone)]
pub struct Installed {
    pub id: String,
    pub name: String,
    pub version: String,
    /// `native`, `lua` or `data`.
    pub kind: String,
}

/// Install one catalogue entry, disabled. The manager checks the signed index again, downloads the
/// zip from the catalogue folder, verifies size and digest, extracts and records it.
pub fn install(ctx: &Ctx, id: &str) -> Result<Installed, String> {
    let d = dir(&ctx.layout.root);
    let mut fetcher = FileFetcher::new(&d);
    let mut src = Source {
        fetcher: &mut fetcher,
        index_url: INDEX_URL.into(),
        sig_url: SIG_URL.into(),
    };
    install::catalogue_install(ctx, &mut src, id, Opts::default(), &mut Capture::default())?;
    let a = crate::plugins::describe(ctx, id);
    let version = store::read_installed(&ctx.layout)
        .ok()
        .and_then(|f| f.find(id).map(|r| r.version.clone()))
        .unwrap_or_default();
    Ok(Installed {
        id: a.id,
        name: a.name,
        version,
        kind: a.kind,
    })
}
