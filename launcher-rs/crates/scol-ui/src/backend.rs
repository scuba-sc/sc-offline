// SPDX-License-Identifier: GPL-3.0-only
//! What the window cannot do by itself. Play, Stop, Update and Repair touch the game folder and
//! the PC; the folder picker and the plugin settings store need a Windows dialog or the host's
//! database. The window asks a [`Backend`] for them. `real::RealBackend` is the real one (it calls
//! the library crates); [`Unconnected`] answers every call with a clear message, and the tests use
//! the fixture backend. Every method has a default so a backend only fills in what it has.
//! Nothing here was run in game.

use std::path::{Path, PathBuf};

use scol_catalogue::fetch::Fetcher;
use scol_catalogue::trust::TrustRoot;

/// The message of a call that has no connection yet. The window shows it as it is.
pub const NOT_CONNECTED: &str = "not connected yet";

/// A command the window can ask for.
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

/// The two questions a command asks, by the answers it reads.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PromptKind {
    /// `... [y/N] `: yes or no.
    YesNo,
    /// `... [y/N/always/never] `: the session-log question.
    Logs,
}

/// What the launcher puts at the end of a yes/no question (`AskYes`).
pub const ASK_YES_NO: &str = "[y/N]";
/// What it puts at the end of the session-log question (`AskLogs`).
pub const ASK_LOGS: &str = "[y/N/always/never]";

/// When `line` is a question a command waits on: its kind and the question without the answer list.
/// A question has no line end, so a backend hands it over as soon as its text ends this way.
pub fn question(line: &str) -> Option<(PromptKind, String)> {
    let l = line.trim_end();
    let (kind, mark) = if l.ends_with(ASK_LOGS) {
        (PromptKind::Logs, ASK_LOGS)
    } else if l.ends_with(ASK_YES_NO) {
        (PromptKind::YesNo, ASK_YES_NO)
    } else {
        return None;
    };
    Some((kind, l[..l.len() - mark.len()].trim().to_string()))
}

/// Lines go to `out` as they are produced (the Output page shows them live). Return the exit code,
/// or an error text when the command could not start.
pub trait Backend: Send + Sync {
    /// Give `word` (`y`, `n`, `always` or `never`) to the command that asked a question. A backend that
    /// cannot answer does nothing, and the command takes its own default when its input ends.
    fn answer(&self, word: &str) {
        let _ = word;
    }

    fn run(&self, command: Command, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        let _ = (here, out);
        Err(format!("{} is {NOT_CONNECTED}", command.word()))
    }

    /// True while the game process of this install runs (Play shows Stop then). Called every frame:
    /// an implementation caches it.
    fn game_running(&self) -> bool {
        false
    }

    /// The process check the plugin manager runs before it changes a plugin folder. A plain
    /// function because the manager's context holds a function pointer. Default: never running.
    fn game_probe(&self) -> fn() -> bool {
        never_running
    }

    /// A network fetcher. The catalogue of this release is read from a local folder, so the window
    /// does not call this; it stays so that earlier backends keep compiling.
    fn fetcher(&self) -> Option<Box<dyn Fetcher + Send>> {
        None
    }

    /// Hosts a network catalogue may use. Not called by this release (see [`Backend::fetcher`]).
    fn allowed_hosts(&self) -> (Vec<String>, Vec<String>) {
        (
            vec!["github.com".into()],
            vec![
                "objects.githubusercontent.com".into(),
                "release-assets.githubusercontent.com".into(),
            ],
        )
    }

    /// The signing keys the catalogue index is checked with. Default: the keys built in. This
    /// release ships none, so the Catalogue tab says no catalogue is published yet.
    fn trust(&self) -> TrustRoot {
        TrustRoot::embedded()
    }

    /// Open a folder or a file in Explorer or its default program. Default: do nothing.
    fn open(&self, path: &Path) {
        let _ = path;
    }

    /// A folder picker for "Add a plugin". `Ok(None)` is a cancel. TEMPORARY: another change is
    /// adding the Windows dialog; until it lands the default answers with a message and the player
    /// types or pastes the path.
    fn pick_folder(&self, title: &str) -> Result<Option<PathBuf>, String> {
        let _ = title;
        Err(format!("the folder picker is {NOT_CONNECTED}"))
    }

    /// Save one plugin setting value for the next game start. TEMPORARY: the host keeps plugin
    /// settings in its own store; the writer is being added elsewhere. The default answers with a
    /// message and the window keeps the draft.
    fn save_plugin_setting(
        &self,
        here: &Path,
        plugin: &str,
        name: &str,
        value: &str,
    ) -> Result<(), String> {
        let _ = (here, plugin, name, value);
        Err(format!("saving plugin settings is {NOT_CONNECTED}"))
    }
}

/// Saves one plugin setting for the next game start, for backends that keep the host's store through
/// `scol_plugin_manager::settings`. It refuses while a game process runs: the host keeps its values in
/// memory and would not see the change.
pub fn save_one_setting(
    here: &Path,
    plugin: &str,
    name: &str,
    value: &str,
    game_running: bool,
) -> Result<(), String> {
    if game_running {
        return Err("The game is running. Close it, then save the setting.".into());
    }
    let mut edits = std::collections::HashMap::new();
    edits.insert(name.to_string(), value.to_string());
    scol_plugin_manager::settings::save(here, plugin, &edits, false)
        .map(|_| ())
        .map_err(|e| e.to_string())
}

fn never_running() -> bool {
    false
}

/// The backend of a window with nothing connected.
pub struct Unconnected;

impl Backend for Unconnected {}
