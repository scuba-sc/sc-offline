// SPDX-License-Identifier: GPL-3.0-only
//! Which entries may be offered on this host, and which may be installed.
//!
//! Rules, checked in this order by [`check_entry`]:
//!
//! 1. the id is not reserved (`sco`, `host`, `menu`, `game`; the same list as the plugin.ini parser);
//! 2. the review level is `first-party` or `reviewed`;
//! 3. plugin api: major equal to the host's, minor not newer;
//! 4. the entry's SDK range admits the SDK the host is pinned to.
//!
//! The SDK range is carried by the signed index only. A plugin zip declares its plugin api in
//! `plugin.ini` but no SDK range, so there is nothing in the zip to compare it with; the range
//! was reviewed and signed with the entry.
//!
//! "No clash with a local folder" needs the folder list, so it is checked in
//! [`crate::install::plan_install`].

use crate::error::Error;
use crate::index::{Entry, Index};
use crate::semver::{SdkRange, Version};

/// Ids the host keeps for itself.
pub const RESERVED_IDS: [&str; 4] = ["sco", "host", "menu", "game"];

/// What the running launcher is built against.
#[derive(Debug, Clone)]
pub struct Host {
    pub api_major: u16,
    pub api_minor: u16,
    /// The SDK the launcher is pinned to, for example `2.4.1`.
    pub sdk: Version,
}

pub fn is_reserved(id: &str) -> bool {
    RESERVED_IDS.contains(&id)
}

pub fn check_entry(e: &Entry, host: &Host) -> Result<(), Error> {
    if is_reserved(&e.id) {
        return Err(Error::ReservedId(e.id.clone()));
    }
    if !e.review.is_listed() {
        return Err(Error::NotListed {
            id: e.id.clone(),
            review: e.review.as_str().to_string(),
        });
    }
    if e.api_major != host.api_major || e.api_minor > host.api_minor {
        return Err(Error::IncompatibleApi {
            id: e.id.clone(),
            plugin: e.api.clone(),
            host: format!("{}.{}", host.api_major, host.api_minor),
        });
    }
    // The index parser already refused an entry whose range does not parse.
    let in_range = SdkRange::parse(&e.sdk).is_some_and(|r| r.matches(&host.sdk));
    if !in_range {
        return Err(Error::IncompatibleSdk {
            id: e.id.clone(),
            range: e.sdk.clone(),
            host: host.sdk.to_string(),
        });
    }
    Ok(())
}

/// One row of the catalogue list: an entry a launcher may show, and whether it can be installed here.
#[derive(Debug, Clone)]
pub struct Offer<'a> {
    pub entry: &'a Entry,
    pub compatible: Result<(), Error>,
}

/// The entries to show: first-party and reviewed only, never a reserved id. Each carries its
/// compatibility verdict so the page can grey out what cannot be installed.
pub fn offered<'a>(index: &'a Index, host: &Host) -> Vec<Offer<'a>> {
    index
        .plugins
        .iter()
        .filter(|e| e.review.is_listed() && !is_reserved(&e.id))
        .map(|entry| Offer {
            entry,
            compatible: check_entry(entry, host),
        })
        .collect()
}
