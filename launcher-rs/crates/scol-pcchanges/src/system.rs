// SPDX-License-Identifier: GPL-3.0-only
//! Every effect the PC changes have on the machine goes through [`System`]. There is one real
//! implementation (`real::RealSystem`, Windows only) and one fake ([`crate::fake::FakeSystem`]).
//! All tests use the fake.

/// Local time as the C++ launcher reads it with `GetLocalTime`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LocalTime {
    pub year: u32,
    pub month: u32,
    pub day: u32,
    pub hour: u32,
    pub minute: u32,
}

/// Windows error code, as `GetLastError` gives it.
pub type ErrorCode = u32;

pub trait System {
    /// An environment variable, or None when unset.
    fn env(&self, name: &str) -> Option<String>;
    /// `RunTool`: runs `"<exe>" <args>` without a window and waits up to 60 seconds. Returns the
    /// exit code, or -1 when the process could not start.
    fn run(&mut self, exe: &str, args: &str) -> i32;
    /// `ReadAll`: the whole file, or the error code.
    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, ErrorCode>;
    /// `WriteAll`: creates or truncates the file and writes the bytes.
    fn write_file(&mut self, path: &str, data: &[u8]) -> Result<(), ErrorCode>;
    /// `MoveFileExW`: with `replace` an existing target is replaced, without it the call fails.
    fn rename(&mut self, from: &str, to: &str, replace: bool) -> Result<(), ErrorCode>;
    /// `DeleteFileW`; the result is not used.
    fn delete_file(&mut self, path: &str);
    /// `CreateDirectoryW` for one folder; the result is not used.
    fn create_dir(&mut self, path: &str);
    /// `IsFile`: exists and is not a folder.
    fn is_file(&mut self, path: &str) -> bool;
    /// `CanWriteTo`: a temporary file can be created in the folder.
    fn can_write_to(&mut self, dir: &str) -> bool;
    /// `IsElevated`: the process token is elevated.
    fn is_elevated(&mut self) -> bool;
    /// `OnWine`.
    fn on_wine(&mut self) -> bool;
    /// `GetLocalTime`.
    fn local_time(&mut self) -> LocalTime;
    /// The registry part of `RsiLauncherExe`: the `InstallLocation` of the uninstall entry named
    /// "RSI Launcher", plus `\RSI Launcher.exe`, when that file exists.
    fn registry_rsi_launcher(&mut self) -> Option<String>;
    /// `ResolveDirectories` of the C++ `install_identity_win32.h`: `bin\.` made absolute and
    /// canonical, trailing backslash dropped, then the last component split off. Returns
    /// `(bin, channel)`, or `None` when the path cannot be resolved.
    fn resolve_dirs(&mut self, bin: &str) -> Option<(String, String)>;
}

/// `EnvOr`: the variable when set, not empty and shorter than MAX_PATH (260 UTF-16 units).
pub fn env_or(sys: &dyn System, name: &str, fallback: &str) -> String {
    match sys.env(name) {
        Some(v) if !v.is_empty() && v.encode_utf16().count() < 260 => v,
        _ => fallback.to_string(),
    }
}

/// `<SystemRoot>\System32\<tool>`.
pub fn system32(sys: &dyn System, tool: &str) -> String {
    format!(
        "{}\\System32\\{tool}",
        env_or(sys, "SystemRoot", "C:\\Windows")
    )
}

/// `PcChangesPath`: `%ProgramData%\sc-offline\pc-changes.txt`.
pub fn pc_changes_path(sys: &dyn System) -> String {
    format!(
        "{}\\sc-offline\\{}",
        env_or(sys, "ProgramData", "C:\\ProgramData"),
        scol_contract::files::PC_CHANGES
    )
}

/// `HostsPath`.
pub fn hosts_path(sys: &dyn System) -> String {
    format!(
        "{}\\System32\\drivers\\etc\\hosts",
        env_or(sys, "SystemRoot", "C:\\Windows")
    )
}

/// `EacExe`.
pub fn eac_exe(sys: &dyn System) -> String {
    format!(
        "{}\\EasyAntiCheat_EOS\\EasyAntiCheat_EOS.exe",
        env_or(sys, "ProgramFiles(x86)", "C:\\Program Files (x86)")
    )
}

/// `ParentDir` (`ParentForSuffix`): `...\LIVE\Bin64` gives `...\LIVE`. The path is resolved
/// first (separators, `.`, `..`, trailing backslash); only when that fails is the plain split used.
pub fn parent_dir(sys: &mut dyn System, path: &str) -> String {
    if let Some((_, mut channel)) = sys.resolve_dirs(path) {
        if channel.ends_with('\\') {
            channel.pop();
        }
        return channel;
    }
    match path.rfind(['\\', '/']) {
        Some(i) => path[..i].to_string(),
        None => path.to_string(),
    }
}
