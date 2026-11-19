// SPDX-License-Identifier: GPL-3.0-only
//! Which game DLL the Rust launcher installs.
//!
//! A 0.8.0 package ships two game DLLs built from the same sources:
//!
//!   * `dinput8.dll`: the host built with `SCO_HOST_MENU_CLIENT=OFF`, the configuration every build run in
//!     game used. It shows the classic menu. This is what gets installed by default.
//!   * `dinput8-rust-menu.dll`: the host built with `SCO_HOST_MENU_CLIENT=ON` and the Rust menu hook. It has
//!     never run in game.
//!
//! The launcher installs the second file, under the game-side name `dinput8.dll`, only when `sc-offline.ini`
//! says `menu = rust` and that file is beside the launcher. In every other case it installs `dinput8.dll`.
//! The C++ launcher always installs `dinput8.dll`. An installed DLL whose bytes equal either shipped file is
//! ours; it is replaced when it is not the selected one, which is how a change of the `menu` key takes
//! effect on the next Play.
//!
//! Source and synthetic evidence only. Not run in game.

use crate::checks::{identify, DllInfo};
use scol_config::{read_config, MenuChoice};
use scol_contract::files;

/// Which menu a shipped game DLL carries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Carries {
    /// The classic in-game menu (`dinput8.dll`, menu client off).
    Classic,
    /// The Rust in-game menu hook (`dinput8-rust-menu.dll`).
    RustMenu,
}

impl Carries {
    /// Plain words for status and dry-run lines.
    pub fn words(self) -> &'static str {
        match self {
            Carries::Classic => "the classic menu",
            Carries::RustMenu => "the new Rust menu",
        }
    }
}

/// The file the launcher copies into the game folder, and why.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModSource {
    /// Full path of the file beside the launcher.
    pub path: String,
    /// Its shipped name: `dinput8.dll` or `dinput8-rust-menu.dll`.
    pub file: &'static str,
    /// The menu the file carries.
    pub carries: Carries,
    /// True when `menu = rust` asked for the Rust menu DLL but it is not beside the launcher, so the
    /// classic one was chosen instead.
    pub rust_missing: bool,
}

impl ModSource {
    /// The plain-words line for `menu = rust`, or none for the default (nothing to say for `imgui`).
    pub fn note(&self, menu: MenuChoice) -> Option<String> {
        match (menu, self.carries, self.rust_missing) {
            (MenuChoice::Rust, Carries::RustMenu, _) => Some(format!(
                "[i] menu = rust: {} is installed as {} and shows {}; it has not been run in game",
                self.file,
                files::MOD_DLL,
                self.carries.words()
            )),
            (MenuChoice::Rust, Carries::Classic, true) => Some(format!(
                "[i] menu = rust, but {} is not beside sc-offline.exe: {} is installed instead and shows {}",
                files::MOD_DLL_RUST_MENU,
                files::MOD_DLL,
                Carries::Classic.words()
            )),
            _ => None,
        }
    }
}

fn is_file(path: &str) -> bool {
    std::fs::metadata(path)
        .map(|m| m.is_file())
        .unwrap_or(false)
}

/// The source DLL for `menu`: `dinput8-rust-menu.dll` when `menu` is Rust and the file is beside the launcher
/// (`here`), else `dinput8.dll`.
pub fn select_source(here: &str, menu: MenuChoice) -> ModSource {
    let rust = format!("{here}\\{}", files::MOD_DLL_RUST_MENU);
    if menu == MenuChoice::Rust {
        if is_file(&rust) {
            return ModSource {
                path: rust,
                file: files::MOD_DLL_RUST_MENU,
                carries: Carries::RustMenu,
                rust_missing: false,
            };
        }
        return ModSource {
            path: format!("{here}\\{}", files::MOD_DLL),
            file: files::MOD_DLL,
            carries: Carries::Classic,
            rust_missing: true,
        };
    }
    ModSource {
        path: format!("{here}\\{}", files::MOD_DLL),
        file: files::MOD_DLL,
        carries: Carries::Classic,
        rust_missing: false,
    }
}

/// The `menu` key of `sc-offline.ini` next to the launcher; `imgui` when the file is missing. The elevated
/// helper uses this: it is a separate process and only knows where the launcher lives.
pub fn menu_from_ini(here: &str) -> MenuChoice {
    read_config(std::path::Path::new(&format!("{here}\\{}", files::INI)))
        .map(|p| p.config.menu)
        .unwrap_or(MenuChoice::Imgui)
}

/// Which shipped DLL (beside the launcher) the installed bytes equal, by SHA-256. None when they equal
/// neither, or when the installed file is not there.
pub fn installed_carries(here: &str, installed: &DllInfo) -> Option<Carries> {
    if !installed.present {
        return None;
    }
    for (file, carries) in [
        (files::MOD_DLL, Carries::Classic),
        (files::MOD_DLL_RUST_MENU, Carries::RustMenu),
    ] {
        let shipped = identify(&format!("{here}\\{file}"));
        if shipped.present && shipped.sha == installed.sha {
            return Some(carries);
        }
    }
    None
}

/// True when the installed DLL is sc-offline's: its text says so, or its bytes equal a shipped DLL.
pub fn installed_is_ours(here: &str, installed: &DllInfo) -> bool {
    installed.ours || installed_carries(here, installed).is_some()
}

/// Lines for the Status panel and `status`, in plain words: which menu the installed DLL carries and when
/// the next Play switches it. Empty in the default case (classic DLL installed, `menu = imgui`), so the
/// output of a default install stays the C++ launcher's.
pub fn status_lines(here: &str, menu: MenuChoice, installed: &DllInfo) -> Vec<String> {
    let src = select_source(here, menu);
    let mut lines = Vec::new();
    if let Some(n) = src.note(menu) {
        lines.push(format!("Menu:     {}", n.trim_start_matches("[i] ")));
    }
    if let Some(have) = installed_carries(here, installed) {
        if have != src.carries {
            lines.push(format!(
                "Menu:     the installed DLL carries {}; the next Play switches it to {} ({})",
                have.words(),
                src.carries.words(),
                src.file
            ));
        } else if have == Carries::RustMenu {
            lines.push(format!(
                "Menu:     the installed DLL carries {}; the next Play keeps it",
                have.words()
            ));
        }
    }
    lines
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir(name: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("scol-moddll-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn info(path: &std::path::Path) -> DllInfo {
        identify(path.to_str().unwrap())
    }

    #[test]
    fn selection_follows_the_decision() {
        let d = dir("select");
        let here = d.to_str().unwrap();
        std::fs::write(d.join("dinput8.dll"), b"classic").unwrap();
        // Default and imgui: dinput8.dll, nothing to say.
        let s = select_source(here, MenuChoice::Imgui);
        assert_eq!(
            (s.file, s.carries, s.rust_missing),
            ("dinput8.dll", Carries::Classic, false)
        );
        assert!(s.note(MenuChoice::Imgui).is_none());
        // menu = rust without the file: dinput8.dll and the reason.
        let s = select_source(here, MenuChoice::Rust);
        assert_eq!((s.file, s.rust_missing), ("dinput8.dll", true));
        assert!(s
            .note(MenuChoice::Rust)
            .unwrap()
            .contains("is not beside sc-offline.exe"));
        // With the file: the second DLL for rust only.
        std::fs::write(d.join("dinput8-rust-menu.dll"), b"rust").unwrap();
        let s = select_source(here, MenuChoice::Rust);
        assert_eq!(
            (s.file, s.carries),
            ("dinput8-rust-menu.dll", Carries::RustMenu)
        );
        assert_eq!(select_source(here, MenuChoice::Imgui).file, "dinput8.dll");
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn status_lines_say_which_menu_is_installed_and_when_it_switches() {
        let d = dir("status");
        let here = d.to_str().unwrap();
        std::fs::write(d.join("dinput8.dll"), b"classic sc-offline v0.8.0").unwrap();
        std::fs::write(d.join("dinput8-rust-menu.dll"), b"rust sc-offline v0.8.0").unwrap();
        let classic = info(&d.join("dinput8.dll"));
        let rust = info(&d.join("dinput8-rust-menu.dll"));
        // The default case stays silent.
        assert!(status_lines(here, MenuChoice::Imgui, &classic).is_empty());
        assert!(status_lines(here, MenuChoice::Imgui, &DllInfo::default()).is_empty());
        // Rust DLL installed, menu back to imgui: switches on the next Play.
        let l = status_lines(here, MenuChoice::Imgui, &rust);
        assert_eq!(l.len(), 1);
        assert!(l[0]
            .contains("carries the new Rust menu; the next Play switches it to the classic menu"));
        // Classic installed, menu = rust: switches on the next Play.
        let l = status_lines(here, MenuChoice::Rust, &classic);
        assert!(l.iter().any(|x| x
            .contains("carries the classic menu; the next Play switches it to the new Rust menu")));
        // Rust installed and selected: kept.
        let l = status_lines(here, MenuChoice::Rust, &rust);
        assert!(l.iter().any(|x| x.contains("the next Play keeps it")));
        let _ = std::fs::remove_dir_all(&d);
    }
}
