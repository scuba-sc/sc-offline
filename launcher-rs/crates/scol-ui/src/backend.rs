// SPDX-License-Identifier: GPL-3.0-only
//! What the window cannot do by itself. The integrator connects these: Play, Stop, Update and
//! Repair run commands that touch the game folder and the PC, and a catalogue download needs a
//! network fetcher. Until then each answers "not available" and the window shows a clear message.
//! Nothing here was run in game.

use std::path::Path;

use scol_catalogue::fetch::Fetcher;
use scol_catalogue::trust::TrustRoot;

/// A command the window can ask for. Each is a `sc-offline.exe <command>` of the C++ launcher.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Command {
    Play,
    Stop,
    Update,
    /// `uninstall`: the Repair button.
    Repair,
}

impl Command {
    /// The command word of the CLI.
    pub fn word(self) -> &'static str {
        match self {
            Command::Play => "play",
            Command::Stop => "stop",
            Command::Update => "update",
            Command::Repair => "uninstall",
        }
    }
}

/// Lines go to `out` as they are produced (the Output page shows them live). Return the exit code,
/// or an error text when the command could not start.
pub trait Backend: Send + Sync {
    fn run(&self, command: Command, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        let _ = (here, out);
        Err(format!("{} is not connected yet", command.word()))
    }

    /// True while the game process of this install runs (Play shows Stop then).
    fn game_running(&self) -> bool {
        false
    }

    /// A fetcher for catalogue downloads. None: the Install button says it is not available.
    fn fetcher(&self) -> Option<Box<dyn Fetcher + Send>> {
        None
    }

    /// Hosts the catalogue index and zips may come from, and hosts a redirect may end on.
    fn allowed_hosts(&self) -> (Vec<String>, Vec<String>) {
        (
            vec!["github.com".into()],
            vec![
                "objects.githubusercontent.com".into(),
                "release-assets.githubusercontent.com".into(),
            ],
        )
    }

    /// The signing keys the catalogue index is checked with. Default: the keys built in (none yet,
    /// so every index is refused until the owner adds the production key).
    fn trust(&self) -> TrustRoot {
        TrustRoot::embedded()
    }

    /// Open a folder or file in Explorer. Default: do nothing.
    fn open(&self, path: &Path) {
        let _ = path;
    }
}

/// The backend of a window with nothing connected.
pub struct Unconnected;

impl Backend for Unconnected {}
