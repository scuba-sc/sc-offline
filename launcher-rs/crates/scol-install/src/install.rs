// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline.exe install` and `uninstall`: the file part, in Rust.
//!
//! This is `PutMod`, `TakeMod`, `Step` and the `install` / `uninstall` branches of `wmain` and
//! `Helper` of `launcher/launcher.cpp`. It copies `dinput8.dll` into Bin64, writes the
//! `sc-offline.installed` marker, sets a foreign `dinput8.dll` aside as `.sc-offline-backup` and puts
//! it back, backs up and restores `default_1.xml`, keeps the backups of a crashed run, and refuses
//! while the game runs. `--dry-run` prints every step and changes nothing (the C++ launcher writes
//! `data\launcher.log` even then, and so does this).
//!
//! What it leaves out, on purpose:
//!   * Every PC change (firewall rules, the hosts line, the EAC rename). `uninstall` asks the caller's
//!     [`PcUndo`] to undo a leftover record, in the same place the C++ code calls `UndoPcChanges`.
//!   * Starting the game, `play`, and the update check (`SettlePreviousUpdate`).
//!   * The elevated helper process. The C++ launcher does the copy in a hidden helper and asks for
//!     administrator rights when the game folder is not writable. Here the copy runs in this
//!     process; [`needs_elevation`] tells the caller when the C++ launcher would have elevated.
//!     The step lines of a real run go to `data\launcher.log` only, because the C++ helper has no
//!     console; a dry run prints them.
//!   * The folder picker of the C++ launcher, and the usage text on an unknown argument.
//!
//! Source and synthetic evidence only. Not run in game.

use crate::checks::{field, game_may_be_running_in, identify};
use crate::fsops::{
    can_write_to, copy_file, create_dir, create_dir_all, delete_file, local_time, move_file,
    write_all, ERROR_FILE_NOT_FOUND,
};
use crate::moddll::{installed_carries, installed_is_ours, menu_from_ini, select_source};
use crate::paths::{parent_dir, GamePaths, GAME_EXE};
use crate::status::{fail, fail_code, find_game, self_checks};
use crate::version::SCO_VERSION;
use crate::win::{is_file, read_all};
use scol_config::ini_edit::drop_ini_key;
use scol_config::{read_config, Config, MenuChoice};
use scol_contract::exit_code;
use scol_contract::files;
use scol_contract::record::{marker_text, marker_time};
use std::io::Write;
use std::os::windows::fs::OpenOptionsExt;
use std::time::Duration;

/// The helper's exit code when a copy failed.
pub const EXIT_COPY_FAILED: i32 = 2;
/// The helper's exit code when `dinput8.dll` could not be removed.
pub const EXIT_REMOVE_FAILED: i32 = 3;

/// Tries the removal of `dinput8.dll` makes, one second apart, before it gives up (the game can
/// still hold the file for a moment after it closes).
pub const REMOVE_TRIES: u32 = 30;

/// `Step`: prints the step and says whether it should really happen (false on `--dry-run`).
fn step(dry: bool, out: &mut dyn Write, text: &str) -> bool {
    let _ = writeln!(
        out,
        "{} {text}",
        if dry { "[dry-run] would" } else { "[+]" }
    );
    !dry
}

/// `PutMod`: copies the mod in, backing up what it replaces, and writes the marker. 0 ok, 2 failed.
/// After a crash the marker is still there: the backups were made then, so they are kept as they are.
/// `here` is the folder of the launcher (`dinput8.dll`, `dinput8-rust-menu.dll` and
/// `data\OfflineDB\default_1.xml` are in it). The `menu` key is read from `sc-offline.ini` there (the
/// elevated helper has no other way to know it); see [`put_mod_with_menu`].
pub fn put_mod(here: &str, g: &GamePaths, mode: &str, dry: bool, out: &mut dyn Write) -> i32 {
    put_mod_with_menu(here, g, mode, menu_from_ini(here), dry, out)
}

/// [`put_mod`] with the menu choice given. The source DLL is `dinput8-rust-menu.dll` when `menu` is Rust and
/// that file is beside the launcher, else `dinput8.dll`; either one is written to Bin64 as `dinput8.dll`.
/// An installed DLL that is the other shipped DLL is ours and is replaced; the output says so.
pub fn put_mod_with_menu(
    here: &str,
    g: &GamePaths,
    mode: &str,
    menu: MenuChoice,
    dry: bool,
    out: &mut dyn Write,
) -> i32 {
    let source = select_source(here, menu);
    let dll_src = source.path.clone();
    let xml_src = format!("{here}\\data\\OfflineDB\\{}", files::DEFAULT_XML);
    let prev = read_all(&g.marker);
    let leftover = prev.is_some();
    let mut xml_state = prev.as_deref().map(|p| field(p, "xml")).unwrap_or_default();

    if is_file(&xml_src) {
        if xml_state.is_empty() {
            if is_file(&g.xml_backup) {
                xml_state = "backup".into();
            } else if is_file(&g.xml) {
                xml_state = "backup".into();
                if step(
                    dry,
                    out,
                    &format!("back up your default_1.xml to {}", g.xml_backup),
                ) {
                    if let Err(e) = copy_file(&g.xml, &g.xml_backup, true) {
                        let _ = writeln!(out, "[!] couldn't back up {} (error {e})", g.xml);
                        return EXIT_COPY_FAILED;
                    }
                }
            } else {
                xml_state = "none".into();
            }
        }
        if step(
            dry,
            out,
            &format!("copy data\\OfflineDB\\default_1.xml to {}", g.xml),
        ) {
            create_dir_all(&g.user_dir); // an existing folder is fine
            if let Err(e) = copy_file(&xml_src, &g.xml, false) {
                let _ = writeln!(
                    out,
                    "[!] couldn't copy default_1.xml to {} (error {e})",
                    g.xml
                );
                return EXIT_COPY_FAILED;
            }
        }
    } else {
        let _ = writeln!(
            out,
            "[i] data\\OfflineDB\\default_1.xml is missing; the game's own player data is used"
        );
        if xml_state.is_empty() {
            xml_state = "skip".into();
        }
    }

    let installed = identify(&g.dll);
    let installed_ours = installed_is_ours(here, &installed);
    if let Some(note) = source.note(menu) {
        let _ = writeln!(out, "{note}");
    }
    if let Some(have) = installed_carries(here, &installed) {
        if have != source.carries {
            let _ = writeln!(
                out,
                "[i] the installed {} carries {}; this {} replaces it with {} ({})",
                files::MOD_DLL,
                have.words(),
                if mode == "play" { "Play" } else { "install" },
                source.carries.words(),
                source.file
            );
        }
    }
    if is_file(&g.dll) && !leftover && !installed_ours {
        if is_file(&g.dll_backup) {
            let _ = writeln!(
                out,
                "[!] {} belongs to another mod and {} already exists; move one of them away first",
                g.dll, g.dll_backup
            );
            return EXIT_COPY_FAILED;
        }
        if step(
            dry,
            out,
            &format!("set aside the other mod's dinput8.dll as {}", g.dll_backup),
        ) {
            if let Err(e) = move_file(&g.dll, &g.dll_backup, false) {
                let _ = writeln!(out, "[!] couldn't rename {} (error {e})", g.dll);
                return EXIT_COPY_FAILED;
            }
        }
    }
    if step(dry, out, &format!("copy {} to {}", source.file, g.dll)) {
        if let Err(e) = copy_file(&dll_src, &g.dll, false) {
            let _ = writeln!(
                out,
                "[!] couldn't copy {} to {} (error {e})",
                source.file, g.dll
            );
            return EXIT_COPY_FAILED;
        }
    }

    let (y, mo, d, h, mi, s) = local_time();
    let when = marker_time(y, mo, d, h, mi, s);
    let module = identify(&dll_src);
    let marker = marker_text(
        if module.version.is_empty() {
            &module.what
        } else {
            &module.version
        },
        &module.sha,
        mode,
        &when,
        &xml_state,
    );
    if step(dry, out, &format!("write {}", g.marker)) {
        if let Err(e) = write_all(&g.marker, marker.as_bytes()) {
            let _ = writeln!(
                out,
                "[!] couldn't write {} (error {e}); uninstall still works, but can't tell whether default_1.xml was yours",
                g.marker
            );
        }
    }
    0
}

/// `TakeMod`: takes the mod out and puts back what `put_mod` replaced. 0 ok, 3 `dinput8.dll`
/// couldn't be removed.
pub fn take_mod(g: &GamePaths, dry: bool, out: &mut dyn Write) -> i32 {
    take_mod_with(g, dry, out, REMOVE_TRIES, Duration::from_secs(1))
}

/// [`take_mod`] with the retry count and the wait between tries given (tests use a short wait).
pub fn take_mod_with(
    g: &GamePaths,
    dry: bool,
    out: &mut dyn Write,
    tries: u32,
    wait: Duration,
) -> i32 {
    let marker = read_all(&g.marker);
    let have_marker = marker.is_some();
    let xml_state = marker
        .as_deref()
        .map(|m| field(m, "xml"))
        .unwrap_or_default();
    let mut rc = 0;

    if is_file(&g.dll) {
        if have_marker || identify(&g.dll).ours {
            if step(dry, out, &format!("delete {}", g.dll)) {
                let mut gone = false;
                let mut last = 0;
                let mut n = 0;
                while n < tries && !gone {
                    match delete_file(&g.dll) {
                        Ok(()) => gone = true,
                        Err(ERROR_FILE_NOT_FOUND) => gone = true,
                        Err(e) => {
                            last = e;
                            std::thread::sleep(wait);
                        }
                    }
                    n += 1;
                }
                if !gone {
                    let _ = writeln!(out, "[!] couldn't delete {} (error {last})", g.dll);
                    rc = EXIT_REMOVE_FAILED;
                }
            }
        } else {
            let _ = writeln!(out, "[i] {} isn't sc-offline's; left alone", g.dll);
        }
    }
    if rc == 0
        && is_file(&g.dll_backup)
        && (dry || !is_file(&g.dll))
        && step(dry, out, "put the other mod's dinput8.dll back")
    {
        if let Err(e) = move_file(&g.dll_backup, &g.dll, false) {
            let _ = writeln!(out, "[!] couldn't restore {} (error {e})", g.dll_backup);
        }
    }

    if is_file(&g.xml_backup) && (xml_state == "backup" || !have_marker) {
        if step(
            dry,
            out,
            &format!("restore your default_1.xml from {}", g.xml_backup),
        ) {
            if let Err(e) = move_file(&g.xml_backup, &g.xml, true) {
                let _ = writeln!(out, "[!] couldn't restore {} (error {e})", g.xml_backup);
            }
        }
    } else if xml_state == "none" && is_file(&g.xml) {
        if step(
            dry,
            out,
            &format!("delete {} (there was none before sc-offline)", g.xml),
        ) {
            if let Err(e) = delete_file(&g.xml) {
                let _ = writeln!(out, "[!] couldn't delete {} (error {e})", g.xml);
            }
        }
    } else if !have_marker && is_file(&g.xml) {
        let _ = writeln!(
            out,
            "[i] left {} in place: an older sc-offline may have put it there, but there's no record of it",
            g.xml
        );
    }

    if have_marker && rc == 0 && step(dry, out, &format!("delete {}", g.marker)) {
        if let Err(e) = delete_file(&g.marker) {
            let _ = writeln!(out, "[!] couldn't delete {} (error {e})", g.marker);
        }
    }
    rc
}

/// True when the C++ launcher would run its helper elevated: the game folder or the channel folder
/// above it is not writable. (`StartHelper`, for `install` and for `uninstall` without leftover PC
/// changes; with a leftover record it always elevates.) This crate never elevates.
pub fn needs_elevation(g: &GamePaths) -> bool {
    !(can_write_to(&g.bin) && can_write_to(&parent_dir(&g.bin)))
}

/// What `uninstall` calls to undo the PC changes a crashed run left (`UndoPcChanges`). Another part
/// of the port owns the real one. Returns 0 or 5 (a change could not be undone).
pub trait PcUndo {
    fn undo(&mut self, dry: bool, out: &mut dyn Write) -> i32;
}

/// Where [`run`] reads its context and writes.
pub struct Io<'a> {
    /// The launcher's own folder (`ExeDir`): `dinput8.dll`, `sc-offline.ini` and `data` are there.
    pub here: String,
    pub out: &'a mut dyn Write,
    /// `uninstall` undoes leftover PC changes through this. None: a leftover record is reported and
    /// left as it is.
    pub pc: Option<&'a mut dyn PcUndo>,
}

/// Console text and `data\launcher.log` at once, like the C++ `Out`.
struct Tee<'a> {
    out: &'a mut dyn Write,
    log: Option<std::fs::File>,
}

impl Write for Tee<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.out.write_all(buf)?;
        if let Some(l) = self.log.as_mut() {
            let _ = l.write_all(buf);
            let _ = l.flush();
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.out.flush()
    }
}

/// `OpenLog`: a new run moves the previous log to `launcher.log.1` (replacing an older one) and
/// starts a new file; every writer appends. The first line is `--- <time> <header>`.
fn open_log(data: &str, append: bool, header: &str) -> Option<std::fs::File> {
    let file = format!("{data}\\launcher.log");
    if !append && std::fs::symlink_metadata(&file).is_ok() {
        let _ = move_file(&file, &format!("{file}.1"), true);
    }
    // FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, as _SH_DENYNO.
    let mut f = std::fs::OpenOptions::new()
        .append(true)
        .create(true)
        .share_mode(7)
        .open(&file)
        .ok()?;
    let (y, mo, d, h, mi, s) = local_time();
    let _ = writeln!(
        f,
        "--- {y:04}-{mo:02}-{d:02} {h:02}:{mi:02}:{s:02} {header}"
    );
    let _ = f.flush();
    Some(f)
}

/// `SetPluginDisabled(here, folder, true)`: the `disabled` marker file of a plugin.
fn disable_plugin(here: &str, folder: &str) -> bool {
    let marker = format!(
        "{here}\\data\\plugins\\{folder}\\{}",
        files::PLUGIN_DISABLED_MARKER
    );
    if std::fs::symlink_metadata(&marker).is_ok() {
        return true;
    }
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .share_mode(1)
        .open(&marker)
        .is_ok()
}

/// The command line the C++ `wmain` accepts for these two commands.
struct Parsed {
    uninstall: bool,
    game_arg: String,
    dry: bool,
    skip_eac: bool,
}

fn parse_args(args: &[String], out: &mut dyn Write) -> Result<Parsed, i32> {
    let mut p = Parsed {
        uninstall: false,
        game_arg: String::new(),
        dry: false,
        skip_eac: false,
    };
    let mut saw_command = false;
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        if a.eq_ignore_ascii_case("--game") {
            i += 1;
            if i >= args.len() {
                return Err(fail(
                    out,
                    "--game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\"",
                ));
            }
            p.game_arg = args[i].clone();
        } else if a.eq_ignore_ascii_case("--dry-run") {
            p.dry = true;
        } else if a.eq_ignore_ascii_case("--console") {
        } else if a.eq_ignore_ascii_case("--skip-eac-check") {
            p.skip_eac = true;
        } else if !saw_command
            && (a.eq_ignore_ascii_case("install") || a.eq_ignore_ascii_case("uninstall"))
        {
            p.uninstall = a.eq_ignore_ascii_case("uninstall");
            saw_command = true;
        } else {
            return Err(fail(out, &format!("unknown argument '{a}'")));
        }
        i += 1;
    }
    if !saw_command {
        return Err(fail(
            out,
            "this entry point runs the install and uninstall commands: pass one of them",
        ));
    }
    Ok(p)
}

/// `sc-offline.exe install|uninstall [--game <folder>] [--dry-run] [--skip-eac-check]`: everything
/// `wmain` does for these commands, from the banner to the last line, and the helper's work done in
/// this process. `args` are the arguments after the program name. Returns the exit code: 0 ok,
/// 1 error (also when the helper's part failed: "the helper failed (exit N)"), 2 Easy Anti-Cheat
/// active, 3 the game is running.
pub fn run(args: &[String], io: &mut Io) -> i32 {
    let parsed = match parse_args(args, io.out) {
        Ok(p) => p,
        Err(code) => return code,
    };
    let here = io.here.clone();
    let data = format!("{here}\\data");
    create_dir(&data); // a fresh unzip may not have it yet
    let mut header = String::from("sc-offline.exe");
    for a in args {
        header.push(' ');
        header.push_str(a);
    }
    let log = open_log(&data, false, &header);
    let mut out = Tee {
        out: &mut *io.out,
        log,
    };
    let dry = parsed.dry;
    let _ = write!(
        out,
        "sc-offline launcher {SCO_VERSION}{}\n\n",
        if dry {
            " - dry run, nothing is changed"
        } else {
            ""
        }
    );

    if !can_write_to(&data) {
        let _ = writeln!(
            out,
            "[!] this folder ({here}) can't be written without administrator rights, so the mod can't save\n    your wallet or places. Move the sc-offline folder out of Program Files, e.g. to your Desktop."
        );
    }
    let ini = format!("{here}\\{}", files::INI);
    let cfg = match read_config(std::path::Path::new(&ini)) {
        Some(p) => {
            for m in &p.messages {
                let _ = writeln!(out, "{m}");
            }
            p.config
        }
        None => {
            let _ = writeln!(
                out,
                "[i] no sc-offline.ini next to this exe; using defaults"
            );
            Config::default()
        }
    };
    // 0.7.5: an old `discord_presence = off` turns the discord plugin off once and the line goes.
    if cfg.discord_off
        && !dry
        && disable_plugin(&here, "discord")
        && drop_ini_key(std::path::Path::new(&ini), "discord_presence")
    {
        let _ = writeln!(
            out,
            "[i] discord_presence = off: the Discord plugin is turned off (data\\plugins\\discord\\disabled)"
        );
    }

    // 1. Find the game.
    let (bin, discovered) = match find_game(&here, &parsed.game_arg, &cfg, &mut out) {
        Ok(found) => found,
        Err(code) => return code,
    };
    if discovered && !dry {
        let _ = write_all(
            &format!("{data}\\game-path.txt"),
            format!("{bin}\n").as_bytes(),
        );
    }
    let _ = writeln!(out, "Folder:   {bin}");
    let g = GamePaths::new(&bin);

    // 2. Self-checks.
    let checks = self_checks(&here, &cfg, &g, &mut out);
    let _ = writeln!(out);

    let running = game_may_be_running_in(&g.bin, &mut out);
    if parsed.uninstall {
        if running {
            return fail_code(
                &mut out,
                exit_code::GAME_RUNNING,
                &format!("{GAME_EXE} is running. Close it first."),
            );
        }
        if dry {
            let a = take_mod(&g, true, &mut out);
            let b = undo_pc(&mut io.pc, checks.pc_leftover, true, &mut out);
            return if (a | b) != 0 {
                exit_code::ERROR
            } else {
                exit_code::OK
            };
        }
    } else {
        if !is_file(&format!("{here}\\{}", files::MOD_DLL)) {
            return fail(
                &mut out,
                "dinput8.dll is missing next to sc-offline.exe.\n    Extract the whole zip into one folder and run this from there.",
            );
        }
        if running {
            return fail_code(
                &mut out,
                exit_code::GAME_RUNNING,
                &format!("{GAME_EXE} is already running. Close it first."),
            );
        }
        if checks.eac_active && !parsed.skip_eac {
            return fail_code(
                &mut out,
                exit_code::EAC_ACTIVE,
                "stopped: Easy Anti-Cheat is active (see above). --skip-eac-check overrides this.",
            );
        }
    }

    // 4. Change the game folder.
    if dry {
        if put_mod_with_menu(&here, &g, "install", cfg.menu, true, &mut out) != 0 {
            return exit_code::ERROR;
        }
        return exit_code::OK;
    }

    // The helper's part. Its lines go to data\launcher.log only (its console is hidden in C++).
    let helper_name = if parsed.uninstall {
        "helper uninstall"
    } else {
        "helper install"
    };
    let mut helper_log = open_log(&data, true, helper_name);
    let mut sink = std::io::sink();
    let helper_out: &mut dyn Write = match helper_log.as_mut() {
        Some(f) => f,
        None => &mut sink,
    };
    let code = if parsed.uninstall {
        let a = take_mod(&g, false, helper_out);
        let b = undo_pc(&mut io.pc, checks.pc_leftover, false, helper_out);
        if a != 0 {
            a
        } else {
            b
        }
    } else {
        put_mod_with_menu(&here, &g, "install", cfg.menu, false, helper_out)
    };
    if code != 0 {
        return fail(
            &mut out,
            &format!("the helper failed (exit {code}); see data\\launcher.log"),
        );
    }
    if parsed.uninstall {
        let _ = writeln!(out, "Mod:      removed.");
    } else {
        let _ = writeln!(
            out,
            "Mod:      installed. Start the game your usual way; run `sc-offline.exe uninstall` before going online."
        );
    }
    exit_code::OK
}

/// `UndoPcChanges` through the caller's hook. Without one, a leftover record is only reported.
fn undo_pc(
    pc: &mut Option<&mut dyn PcUndo>,
    leftover: bool,
    dry: bool,
    out: &mut dyn Write,
) -> i32 {
    match pc.as_mut() {
        Some(p) => p.undo(dry, out),
        None => {
            if leftover {
                let _ = writeln!(
                    out,
                    "[!] PC changes from a previous run are still in place; this build does not undo them yet"
                );
            }
            0
        }
    }
}
