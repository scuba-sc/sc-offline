// SPDX-License-Identifier: GPL-3.0-only
//! Signed plugin catalogue client for the sc-offline Rust launcher (design `docs/rust-launcher.md`,
//! section 5 and phase P7).
//!
//! What this crate does, in the order a pull runs:
//!
//! 1. [`index::open_index`] checks a downloaded `index.json` against a trust root: size cap,
//!    Ed25519 signature (before any JSON is parsed), strict parse (unknown field and duplicate key
//!    are errors), expiry, and the sequence number (no rollback below the last accepted one, and the
//!    same sequence only with the same bytes: the caller passes [`index::LastAccepted`]).
//! 2. [`policy::check_entry`] decides whether one entry may be offered here: id not reserved,
//!    review level listed, plugin api major equal and minor not newer, SDK range matches.
//! 3. [`install::verify_download`] checks a downloaded zip against the index (size, SHA-256) and
//!    then runs the `scol-plugins` zip verifier, and checks the zip says what the index says.
//! 4. [`install::plan_install`] builds an [`install::InstallPlan`]: where the files go under
//!    `data/plugins/<id>/`, the `disabled` marker, and the new `data/launcher/installed.json`.
//!
//! What it does not do: no network code (fetching goes through the [`fetch::Fetcher`] trait, one request per call, redirects driven and host-checked here; the
//! launcher implements), no file writes, no process start, and it never loads or runs plugin code.
//! Every pulled plugin is planned as disabled with a native-code warning. Nothing here was run in
//! game; the tests are synthetic and use local fixtures signed with a TEST key.
//!
//! Choice of signature crate: `ed25519-compact` (MIT, no dependencies with default features off).
//! It rejects non-canonical signatures and small-order keys, and its code is a small, readable
//! implementation of Ed25519 by the author of libsodium. `ed25519-dalek` is the other candidate.
//! It is widely reviewed too, but it pulls `curve25519-dalek`, `sha2`, `subtle`, `zeroize` and
//! more, about ten crates for one `verify` call. Fewer crates is the smaller thing to audit. Both
//! licences pass `deny.toml`. The crate's signing code is linked in as well (its
//! `disable-signatures` feature would break the test signer); the linker drops what is unused.
//!
//! The production trust root is empty until the maintainer generates the signing key offline and adds
//! its public half to [`trust::PRODUCTION_KEYS`]. Until then every index is refused (fail closed).

pub mod cli;
pub mod error;
pub mod fetch;
pub mod index;
pub mod install;
pub mod policy;
pub mod semver;
pub mod time;
pub mod trust;

pub use cli::{run, run_with, Io};
pub use error::Error;
