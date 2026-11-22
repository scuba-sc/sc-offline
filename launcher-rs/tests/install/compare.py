#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parity check for `install` and `uninstall`: the built C++ sc-offline.exe against the Rust library.

For every scenario the script builds the same synthetic layout twice under a scratch folder (one
world for the C++ launcher, one for the Rust probe, `install_probe.exe`, a small example program
of the scol-install crate), runs the same list of commands in each, and after every command
requires the same exit code, the same text, the same file tree and the same data\\launcher.log.

No real game folder is used. A world holds a fake game (a plain file named StarCitizen.exe in
lib\\StarCitizen\\LIVE\\Bin64), the launcher folder (here\\: dinput8.dll, sc-offline.ini, data\\) and
the ProgramData, ProgramFiles(x86), APPDATA and SystemRoot folders the launcher reads, which the
environment points into the world. Every game folder is given with --game or `game =` in the
scratch sc-offline.ini; the script refuses to run a command whose game folder is not under the
scratch folder. The two game-discovery scenarios run with --dry-run only, because discovery also
reads this PC's registry and usual folders and could find a real game first.

Only the file part is compared. These stay out, as in the Rust port: firewall rules, the hosts
line, the EAC rename (no leftover pc-changes.txt record is created) and starting the game.
The C++ install and uninstall run through the launcher's own hidden helper process. Its game
folder and the folder above it are writable in the scratch folder, so it runs unelevated and
asks for nothing; no scenario needs administrator rights or changes this PC.

The running-game scenarios start a copy of ping.exe named StarCitizen.exe inside the scratch
Bin64 (a stand-in process that sends one packet at a time to 127.0.0.1) for the length of one
command and then end it. The process is never the real game.

Differences from a byte-for-byte diff, by design:
  * the C++ launcher prints through the C runtime in text mode, so a pipe sees CRLF; the Rust one
    writes LF. Line endings are normalised.
  * the world path is replaced by @W in text and in the log; times (the marker's time= line, the
    `--- <time>` log headers) are replaced by <T>.
  * paths are ASCII (the C++ launcher prints wide paths with %ls in the "C" locale).
  * the Rust run does the helper's work in its own process, so the step lines of a real run reach
    launcher.log in both and the console in neither. That is the same text in the same place.
  * not compared: file times and attributes.

Not run in game. Usage:
  python compare.py --cpp <sc-offline.exe> [--rust <install_probe.exe>] [--work <folder>]
                    [--only <substring>] [--slow] [--keep]
Exit code 0 when every scenario matches, 1 otherwise.
"""
import argparse
import difflib
import hashlib
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUST_DEFAULT = HERE.parent.parent / "target" / "debug" / "examples" / "install_probe.exe"
SCRATCH_ROOT = Path(os.environ.get("SCOL_SCRATCH") or Path(tempfile.gettempdir()) / "scol-scratch")
SCRATCH_ROOT.mkdir(parents=True, exist_ok=True)
SCRATCH_ROOT = SCRATCH_ROOT.resolve()
PING = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32" / "PING.EXE"

OURS_DLL = b"MZ\x90\x00 padding sc-offline v0.8.0-rc.2\x00 more bytes"
OLD_DLL = b"MZ\x90\x00 ChrisWareOffline 0.9.0-rc1 / sc-offline 0.2.0-rc4 tail"
OTHER_DLL = b"MZ\x90\x00 some other mod's dinput8"
OTHER_DLL_2 = b"MZ\x90\x00 a second foreign dinput8"
STAND_IN_EXE = b"MZ game exe stand-in"
XML_MOD = b"<OfflineDB>the mod's default_1</OfflineDB>\n"
XML_PLAYER = b"<Player>the player's own default_1</Player>\n"
XML_BACKUP_OLD = b"<Player>the backup made by a crashed run</Player>\n"
MARKER_BACKUP = b"version=0.8.0-rc.2\nsha256=abc\nmode=install\ntime=2026-11-17 09:05:03\nxml=backup\n"
MARKER_NONE = b"version=0.8.0-rc.2\nsha256=abc\nmode=play\ntime=2026-11-17 09:05:03\nxml=none\n"
MARKER_SKIP = b"version=0.8.0-rc.2\nsha256=abc\nmode=install\ntime=2026-11-17 09:05:03\nxml=skip\n"
MARKER_EMPTY_XML = b"version=0.8.0-rc.2\ntime=2026-11-17 09:05:03\n"
HOSTS_PLAIN = b"# a hosts file\n127.0.0.1 localhost\n"

BIN = "lib/StarCitizen/LIVE/Bin64"
USER = "lib/StarCitizen/LIVE/user/client/0"


def win(p):
    return str(p).replace("/", "\\")


class World:
    def __init__(self, root):
        self.root = root
        for d in ("here", "pd", "pf86", "appdata", "sysroot/System32/drivers/etc", BIN, USER):
            (root / d).mkdir(parents=True, exist_ok=True)
        self.put("sysroot/System32/drivers/etc/hosts", HOSTS_PLAIN)

    def put(self, rel, data):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def env(self):
        e = dict(os.environ)
        e["ProgramData"] = win(self.root / "pd")
        e["ProgramFiles(x86)"] = win(self.root / "pf86")
        e["APPDATA"] = win(self.root / "appdata")
        e["SystemRoot"] = win(self.root / "sysroot")
        e.pop("WINEDLLOVERRIDES", None)
        e.pop("SC_OFFLINE_GUI", None)
        return e


class Ctx:
    """One scenario: the same edits go to the C++ world and the Rust world."""

    def __init__(self, root, cpp_exe):
        self.cpp = World(root / "cpp")
        self.rust = World(root / "rust")
        self.worlds = (self.cpp, self.rust)
        shutil.copy2(cpp_exe, self.cpp.root / "here" / "sc-offline.exe")
        self.game()
        self.here("dinput8.dll", OURS_DLL)

    def put(self, rel, data):
        for w in self.worlds:
            w.put(rel, data)

    def here(self, rel, data):
        self.put("here/" + rel, data)

    def game(self):
        self.put(BIN + "/StarCitizen.exe", STAND_IN_EXE)

    def bin(self, name, data):
        self.put(BIN + "/" + name, data)

    def user(self, name, data):
        self.put(USER + "/" + name, data)

    def eac(self, state):
        d = "pf86/EasyAntiCheat_EOS/"
        if state == "active":
            self.put(d + "EasyAntiCheat_EOS.exe", b"eac")
        elif state == "disabled":
            self.put(d + "EasyAntiCheat_EOS.exe.bak", b"eac")

    def remove(self, rel):
        for w in self.worlds:
            (w.root / rel).unlink()

    def mkdir(self, rel):
        for w in self.worlds:
            (w.root / rel).mkdir(parents=True, exist_ok=True)

    def readonly(self, rel, on=True):
        for w in self.worlds:
            os.chmod(w.root / rel, stat.S_IREAD if on else stat.S_IWRITE | stat.S_IREAD)


def S(args, text=(), code=0, running=False, log=(), pre=None):
    """One command. `text`: substrings the output must hold. `log`: the same for launcher.log.
    `pre(ctx)` runs on both worlds before the command. `@GAME` is the world's StarCitizen folder."""
    return dict(args=list(args), text=list(text), code=code, running=running, log=list(log), pre=pre)


SCENARIOS = []
SLOW = []


def scenario(fn):
    SCENARIOS.append(fn)
    return fn


def slow(fn):
    SLOW.append(fn)
    return fn


G = ["--game", "@GAME"]
INSTALLED = "Mod:      installed. Start the game your usual way; run `sc-offline.exe uninstall` before going online."
REMOVED = "Mod:      removed."


@scenario
def install_fresh_no_xml(c):
    # No default_1.xml in the package: marker xml=skip, nothing under user\.
    return [S(["install"] + G, [INSTALLED], 0, log=["[i] data\\OfflineDB\\default_1.xml is missing",
                                                       "[+] copy dinput8.dll to", "[+] write "]),
            S(["uninstall"] + G, [REMOVED], 0, log=["[+] delete "])]


@scenario
def install_fresh_with_xml_none_before(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    shutil.rmtree(c.cpp.root / USER)
    shutil.rmtree(c.rust.root / USER)
    return [S(["install"] + G, [INSTALLED], 0, log=["[+] copy data\\OfflineDB\\default_1.xml to"]),
            S(["uninstall"] + G, [REMOVED], 0, log=["there was none before sc-offline"])]


@scenario
def install_xml_backup_and_restore(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_PLAYER)
    return [S(["install"] + G, [INSTALLED], 0, log=["[+] back up your default_1.xml to"]),
            S(["uninstall"] + G, [REMOVED], 0, log=["[+] restore your default_1.xml from"])]


@scenario
def install_xml_existing_backup_kept(c):
    # A backup is already there (a crash): it is kept, the player's current file is not copied over it.
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_MOD)
    c.user("default_1.xml.sc-offline-backup", XML_BACKUP_OLD)
    return [S(["install"] + G, [INSTALLED], 0),
            S(["uninstall"] + G, [REMOVED], 0, log=["[+] restore your default_1.xml from"])]


@scenario
def install_foreign_dll_set_aside_and_back(c):
    c.bin("dinput8.dll", OTHER_DLL)
    return [S(["install"] + G, [INSTALLED], 0, log=["[+] set aside the other mod's dinput8.dll as"]),
            S(["uninstall"] + G, [REMOVED], 0, log=["[+] put the other mod's dinput8.dll back"])]


@scenario
def install_foreign_dll_with_backup_refuses(c):
    c.bin("dinput8.dll", OTHER_DLL)
    c.bin("dinput8.dll.sc-offline-backup", OTHER_DLL_2)
    return [S(["install"] + G, ["[!] the helper failed (exit 2); see data\\launcher.log"], 1,
              log=["belongs to another mod and"])]


@scenario
def install_crash_leftover_keeps_backups(c):
    # The marker, our DLL and both backups are still there after a crash: install again must not
    # set the DLL aside again nor re-make the xml backup.
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.bin("sc-offline.installed", MARKER_BACKUP)
    c.bin("dinput8.dll", OURS_DLL + b" older")
    c.bin("dinput8.dll.sc-offline-backup", OTHER_DLL)
    c.user("default_1.xml", XML_MOD)
    c.user("default_1.xml.sc-offline-backup", XML_BACKUP_OLD)
    return [S(["install"] + G, [INSTALLED], 0),
            S(["uninstall"] + G, [REMOVED], 0, log=["[+] put the other mod's dinput8.dll back"])]


@scenario
def install_leftover_marker_with_foreign_dll_not_set_aside(c):
    # A marker means a crash: whatever dinput8.dll is there is not treated as "another mod's".
    c.bin("sc-offline.installed", MARKER_SKIP)
    c.bin("dinput8.dll", OTHER_DLL)
    return [S(["install"] + G, [INSTALLED], 0)]


@scenario
def install_marker_without_xml_field(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_PLAYER)
    c.bin("sc-offline.installed", MARKER_EMPTY_XML)
    return [S(["install"] + G, [INSTALLED], 0)]


@scenario
def install_over_old_ours_dll_no_marker(c):
    c.bin("dinput8.dll", OLD_DLL)
    return [S(["install"] + G, [INSTALLED], 0),
            S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def install_dry_run_fresh(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_PLAYER)
    return [S(["install", "--dry-run"] + G,
              ["dry run, nothing is changed", "[dry-run] would back up your default_1.xml to",
               "[dry-run] would copy data\\OfflineDB\\default_1.xml to", "[dry-run] would copy dinput8.dll to",
               "[dry-run] would write "], 0)]


@scenario
def install_dry_run_foreign_dll(c):
    c.bin("dinput8.dll", OTHER_DLL)
    return [S(["install", "--dry-run"] + G, ["[dry-run] would set aside the other mod's dinput8.dll as"], 0)]


@scenario
def install_dry_run_foreign_dll_with_backup(c):
    c.bin("dinput8.dll", OTHER_DLL)
    c.bin("dinput8.dll.sc-offline-backup", OTHER_DLL_2)
    return [S(["install", "--dry-run"] + G, ["belongs to another mod and"], 1)]


@scenario
def uninstall_dry_run_after_install(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_PLAYER)
    c.bin("dinput8.dll", OTHER_DLL)
    return [S(["install"] + G, [INSTALLED], 0),
            S(["uninstall", "--dry-run"] + G,
              ["[dry-run] would delete ", "[dry-run] would put the other mod's dinput8.dll back",
               "[dry-run] would restore your default_1.xml from"], 0),
            S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def uninstall_nothing_installed(c):
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def uninstall_foreign_dll_left_alone(c):
    c.bin("dinput8.dll", OTHER_DLL)
    return [S(["uninstall"] + G, [REMOVED], 0, log=["isn't sc-offline's; left alone"])]


@scenario
def uninstall_old_install_without_marker_leaves_xml(c):
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml", XML_MOD)
    return [S(["uninstall"] + G, ["Mod:      removed."], 0, log=["left ", "in place: an older sc-offline may have put it"])]


@scenario
def uninstall_without_marker_restores_backup(c):
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml", XML_MOD)
    c.user("default_1.xml.sc-offline-backup", XML_PLAYER)
    return [S(["uninstall"] + G, [REMOVED], 0, log=["[+] restore your default_1.xml from"])]


@scenario
def uninstall_marker_xml_none_deletes_xml(c):
    c.bin("sc-offline.installed", MARKER_NONE)
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml", XML_MOD)
    return [S(["uninstall"] + G, [REMOVED], 0, log=["(there was none before sc-offline)"])]


@scenario
def uninstall_marker_xml_skip_keeps_xml(c):
    c.bin("sc-offline.installed", MARKER_SKIP)
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml", XML_PLAYER)
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def uninstall_marker_xml_skip_with_backup_keeps_backup(c):
    c.bin("sc-offline.installed", MARKER_SKIP)
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml.sc-offline-backup", XML_BACKUP_OLD)
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def uninstall_marker_only_no_dll(c):
    c.bin("sc-offline.installed", MARKER_BACKUP)
    c.user("default_1.xml.sc-offline-backup", XML_PLAYER)
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def install_twice(c):
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.user("default_1.xml", XML_PLAYER)
    return [S(["install"] + G, [INSTALLED], 0), S(["install"] + G, [INSTALLED], 0),
            S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def install_dinput8_missing_next_to_exe(c):
    c.remove("here/dinput8.dll")
    return [S(["install"] + G, ["[!] dinput8.dll is missing next to sc-offline.exe."], 1)]


@scenario
def uninstall_does_not_need_dinput8_next_to_exe(c):
    c.remove("here/dinput8.dll")
    c.bin("dinput8.dll", OURS_DLL)
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def eac_active_stops_install(c):
    c.eac("active")
    return [S(["install"] + G, ["stopped: Easy Anti-Cheat is active"], 2)]


@scenario
def eac_active_skip_check(c):
    c.eac("active")
    return [S(["install", "--skip-eac-check"] + G, [INSTALLED], 0)]


@scenario
def eac_active_does_not_stop_uninstall(c):
    c.eac("active")
    c.bin("dinput8.dll", OURS_DLL)
    return [S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def game_running_refuses_install(c):
    return [S(["install"] + G, ["StarCitizen.exe is already running. Close it first."], 3, running=True)]


@scenario
def game_running_refuses_uninstall(c):
    c.bin("dinput8.dll", OURS_DLL)
    return [S(["uninstall"] + G, ["StarCitizen.exe is running. Close it first."], 3, running=True)]


@scenario
def game_running_dry_run_uninstall_refuses_too(c):
    c.bin("dinput8.dll", OURS_DLL)
    return [S(["uninstall", "--dry-run"] + G, ["is running. Close it first."], 3, running=True)]


@scenario
def game_missing_under_game_arg(c):
    return [S(["install", "--game", "@LIB/nothing"], ["no StarCitizen.exe under --game"], 1)]


@scenario
def game_arg_needs_a_folder(c):
    return [S(["install", "--game"], ["--game needs a folder"], 1)]


@scenario
def game_arg_as_bin64_and_live(c):
    return [S(["install", "--game", "@BIN64"], [INSTALLED], 0), S(["uninstall", "--game", "@LIVE"], [REMOVED], 0)]


@scenario
def game_from_ini(c):
    c.here("sc-offline.ini", b"game = @GAME\n")
    return [S(["install"], [INSTALLED], 0), S(["uninstall"], [REMOVED], 0)]


@scenario
def ini_game_missing(c):
    c.here("sc-offline.ini", b"game = @LIB/nowhere\n")
    return [S(["install"], ["no StarCitizen.exe under game ="], 1)]


@scenario
def ini_warnings_are_printed(c):
    c.here("sc-offline.ini", b"block_network = maybe\nbogus_key = 1\n")
    return [S(["install"] + G, [INSTALLED], 0)]


@scenario
def discord_presence_off_migrates(c):
    c.here("sc-offline.ini", b"discord_presence = off\nboot_map = PU\n")
    c.here("data/plugins/discord/plugin.ini", b"")
    return [S(["install"] + G, ["discord_presence = off: the Discord plugin is turned off", INSTALLED], 0)]


@scenario
def discovery_dry_run_remembered(c):
    # Dry run only: discovery also reads this PC's registry and folders. Nothing is written but the log.
    c.here("data/game-path.txt", b"@BIN64\n")
    return [S(["install", "--dry-run"], ["Folder:   "], None)]


@scenario
def previous_log_is_rotated(c):
    c.here("data/launcher.log", b"--- old run\nold line\n")
    return [S(["install"] + G, [INSTALLED], 0), S(["uninstall"] + G, [REMOVED], 0)]


@scenario
def install_dll_copy_fails_leaves_xml_behind(c):
    # Known limit shared with the C++ launcher: no rollback. The mod's default_1.xml is put in place
    # before the DLL copy; when the copy fails (a folder named dinput8.dll), the xml stays, no marker is
    # written, and uninstall finds no record of it. Rust must do exactly the same as C++.
    c.here("data/OfflineDB/default_1.xml", XML_MOD)
    c.mkdir(BIN + "/dinput8.dll")
    return [S(["install"] + G, [], 1), S(["uninstall"] + G, [], 0)]


@scenario
def case_of_command_and_flags(c):
    return [S(["INSTALL", "--DRY-RUN", "--Game", "@GAME"], ["dry run, nothing is changed"], 0)]


@slow
def uninstall_locked_dll_gives_up_after_30_tries(c):
    # A read-only DLL cannot be deleted: 30 tries, one second apart, then exit 3 and the marker stays.
    c.bin("sc-offline.installed", MARKER_SKIP)
    c.bin("dinput8.dll", OURS_DLL)
    c.user("default_1.xml.sc-offline-backup", XML_PLAYER)
    c.readonly(BIN + "/dinput8.dll")
    return [S(["uninstall"] + G, ["[!] the helper failed (exit 3); see data\\launcher.log"], 1,
              log=["couldn't delete "]),
            S(["uninstall"] + G, ["[!] the helper failed (exit 3)"], 1, pre=lambda c: None)]


def expand(c, world, arg):
    for token, rel in (("@GAME", "lib/StarCitizen"), ("@BIN64", BIN), ("@LIVE", "lib/StarCitizen/LIVE"),
                       ("@LIB", "lib")):
        if token in arg:
            arg = arg.replace(token, win(world.root / rel))
    return arg


def norm_text(world, data):
    t = data.decode("utf-8", "replace").replace("\r\n", "\n")
    for form in (win(world.root), str(world.root).replace("\\", "/")):
        t = re.sub(re.escape(form), "@W", t, flags=re.I)
    t = re.sub(r"(?m)^(--- )\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ", r"\1<T> ", t)
    t = re.sub(r"(since )\d{4}-\d\d-\d\d \d\d:\d\d:\d\d", r"\1<T>", t)   # the marker's time in `Installed:`
    return t


def snapshot(world):
    """relative path -> sha256 of the content (dirs: '<dir>'). The exe and the logs are left out."""
    skip = {"here/sc-offline.exe", "here/data/launcher.log", "here/data/launcher.log.1"}
    tree = {}
    for dp, dns, fns in os.walk(world.root):
        for n in dns:
            tree[str((Path(dp) / n).relative_to(world.root)).replace("\\", "/")] = "<dir>"
        for n in fns:
            p = Path(dp) / n
            rel = str(p.relative_to(world.root)).replace("\\", "/")
            if rel in skip:
                continue
            data = p.read_bytes()
            if n == "sc-offline.installed":
                data = re.sub(rb"(?m)^time=.*$", b"time=<T>", data)
            if n in ("sc-offline.ini", "game-path.txt"):   # files that hold the world's own path
                data = re.sub(re.escape(win(world.root).encode()), b"@W", data, flags=re.I)
            tree[rel] = hashlib.sha256(data).hexdigest()[:16] + " %d bytes" % len(data)
    return tree


def read_log(world, name="launcher.log"):
    p = world.root / "here" / "data" / name
    return norm_text(world, p.read_bytes()) if p.exists() else "<no file>"


def run_one(world, argv, timeout):
    p = subprocess.run(argv, cwd=world.root, env=world.env(), stdin=subprocess.DEVNULL, capture_output=True,
                       timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def with_stand_in(world, fn):
    exe = world.root / BIN / "StarCitizen.exe"
    exe.write_bytes(PING.read_bytes())
    proc = subprocess.Popen([str(exe), "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    try:
        return fn()
    finally:
        proc.kill()
        proc.wait()
        exe.write_bytes(STAND_IN_EXE)


def check_under_scratch(world, args):
    for a in args:
        if re.match(r"^[A-Za-z]:[\\/]", a) and not str(Path(a).resolve()).lower().startswith(str(SCRATCH_ROOT).lower()):
            raise SystemExit("refusing to run with a path outside " + str(SCRATCH_ROOT) + ": " + a)


def run_scenario(fn, work, cpp_exe, rust_exe, keep):
    root = work / fn.__name__
    if root.exists():
        shutil.rmtree(root, onerror=lambda f, p, e: (os.chmod(p, stat.S_IWRITE), f(p)))
    c = Ctx(root, cpp_exe)
    steps = fn(c)
    # `@GAME`-style tokens inside files (the ini and game-path.txt) get each world's path.
    for w in c.worlds:
        for rel in ("here/sc-offline.ini", "here/data/game-path.txt"):
            p = w.root / rel
            if p.exists():
                t = p.read_bytes().decode()
                for token, sub in (("@GAME", "lib/StarCitizen"), ("@BIN64", BIN), ("@LIB", "lib")):
                    t = t.replace(token, win(w.root / sub))
                p.write_bytes(t.encode())
    problems = []
    for i, st in enumerate(steps, 1):
        if st["pre"]:
            st["pre"](c)
        results = []
        for w, exe_argv in ((c.cpp, [str(c.cpp.root / "here" / "sc-offline.exe")]),
                            (c.rust, [str(rust_exe), win(c.rust.root / "here")])):
            args = [expand(c, w, a) for a in st["args"]]
            check_under_scratch(w, args)
            argv = exe_argv + args
            timeout = 240 if fn in SLOW else 90
            if st["running"]:
                code, out = with_stand_in(w, lambda: run_one(w, argv, timeout))
            else:
                code, out = run_one(w, argv, timeout)
            results.append((code, norm_text(w, out), snapshot(w), read_log(w), read_log(w, "launcher.log.1")))
        (cc, ct, ctree, clog, clog1), (rc, rt, rtree, rlog, rlog1) = results
        tag = "step %d (%s)" % (i, " ".join(st["args"]))
        if cc != rc:
            problems.append("%s: exit code C++ %s, Rust %s" % (tag, cc, rc))
        if ct != rt:
            problems.append("%s: output differs\n%s" % (tag, "".join(difflib.unified_diff(
                ct.splitlines(True), rt.splitlines(True), "C++", "Rust"))))
        if ctree != rtree:
            keys = sorted(set(ctree) | set(rtree))
            problems.append("%s: file tree differs\n%s" % (tag, "\n".join(
                "  %s: C++ %s | Rust %s" % (k, ctree.get(k), rtree.get(k)) for k in keys if ctree.get(k) != rtree.get(k))))
        if clog != rlog:
            problems.append("%s: launcher.log differs\n%s" % (tag, "".join(difflib.unified_diff(
                clog.splitlines(True), rlog.splitlines(True), "C++", "Rust"))))
        if clog1 != rlog1:
            problems.append("%s: launcher.log.1 differs" % tag)
        # The scenario's own expectations (so two identical wrong results do not pass).
        if st["code"] is not None and cc != st["code"]:
            problems.append("%s: expected exit code %s, got %s (both programs)" % (tag, st["code"], cc))
        for s in st["text"]:
            s = s.replace("@GAME", "")
            if s not in ct:
                problems.append("%s: output lacks %r" % (tag, s))
        for s in st["log"]:
            if s not in clog:
                problems.append("%s: launcher.log lacks %r" % (tag, s))
    if not keep and not problems:
        shutil.rmtree(root, onerror=lambda f, p, e: (os.chmod(p, stat.S_IWRITE), f(p)))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpp", required=True, help="the built C++ sc-offline.exe")
    ap.add_argument("--rust", default=str(RUST_DEFAULT), help="install_probe.exe (cargo build -p scol-install --example install_probe)")
    ap.add_argument("--work", default=str(SCRATCH_ROOT / "install-parity"))
    ap.add_argument("--only", default="")
    ap.add_argument("--slow", action="store_true", help="also run the scenarios that wait out the 30 delete tries")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    work = Path(a.work).resolve()
    if not str(work).lower().startswith(str(SCRATCH_ROOT).lower() + os.sep):
        raise SystemExit("--work must be under " + str(SCRATCH_ROOT))
    for exe in (a.cpp, a.rust):
        if not Path(exe).is_file():
            raise SystemExit("not found: " + exe)
    if work.exists():
        shutil.rmtree(work, onerror=lambda f, p, e: (os.chmod(p, stat.S_IWRITE), f(p)))
    work.mkdir(parents=True)
    todo = [f for f in SCENARIOS + (SLOW if a.slow else []) if a.only in f.__name__]
    failed = 0
    for fn in todo:
        problems = run_scenario(fn, work, Path(a.cpp), Path(a.rust), a.keep)
        print(("ok   " if not problems else "FAIL ") + fn.__name__)
        for p in problems:
            print("     " + p.replace("\n", "\n     "))
        failed += bool(problems)
    print("\n%d scenarios, %d failed. Source and synthetic evidence only. Not run in game." % (len(todo), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
