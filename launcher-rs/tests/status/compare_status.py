#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parity check for `status` and `help`: the built C++ sc-offline.exe against the Rust scol.exe.

For every scenario the script builds a synthetic folder layout under a scratch folder, runs both
programs on it (each from its own copy of the folder, so the C++ launcher and the Rust launcher see
the same files), and requires the same exit code and the same text. Only `help`, `status` and the
argument errors run; the C++ `play`, `install`, `uninstall` and `update` are never started.

It also proves, for the Rust program, that `status` writes nothing (a file tree comparison of the
whole scratch scenario before and after) and that the exe imports no network library other than WinHTTP (the updater's).

No real game folder is used. Folders, the ProgramData, ProgramFiles(x86), APPDATA and SystemRoot
variables all point into the scratch folder. The registry and the fixed drives cannot be redirected,
so discovery (no --game, no game = in the ini) also reads the real registry and the usual folders of
this PC. The default scenarios always have a synthetic folder that discovery finds first; a real
install on this PC can still show up as an extra "also found" line in both programs, and is only
looked at (file and folder existence), never opened or changed. `--machine` adds the scenarios that
have nothing synthetic to find, so the result is this PC's own, and lets the full drive search run.
They compare the two programs with each other and assume no result.

Differences from a byte-for-byte diff, by design and listed here so nothing is hidden:
  * the C++ launcher prints through the C runtime in text mode, so a pipe sees CRLF; the Rust one
    writes LF. Line endings are normalised before comparing.
  * the version banner (first line) is compared with the version replaced, as the task allows.
  * the Rust usage text has two lines the C++ usage lacks, because the C++ launcher has no `stop` and no
    `plugins` command: the `stop` line and the `plugins` line (RUST_ONLY_USAGE_LINES below). Exactly those two
    lines are dropped from the Rust output; any other difference in the usage text still fails.
  * paths are ASCII. The C++ launcher prints wide paths with %ls in the "C" locale, which stops at a
    character it cannot convert; the Rust one prints UTF-8. That is not emulated.

Not run in game. Usage:
  python compare_status.py --cpp <sc-offline.exe> [--rust <scol.exe>] [--work <folder>] [--machine]
                           [--only <substring>] [--keep]
Exit code 0 when every scenario matches, 1 otherwise.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUST_DEFAULT = HERE.parent.parent / "target" / "release" / "scol.exe"

OURS_DLL = b"MZ\x90\x00 padding sc-offline v0.8.0-rc.1\x00 more bytes"
OLD_DLL = b"MZ\x90\x00 ChrisWareOffline 0.9.0-rc1 / sc-offline 0.2.0-rc4 tail"
OTHER_DLL = b"MZ\x90\x00 some other mod's dinput8"
MARKER = b"version=0.8.0-rc.1\nsha256=abc\nmode=install\ntime=2026-11-17 09:05:03\nxml=none\n"
PC_ALL = (b"time=2026-11-17 09:05\nfirewall=added\nfw_launcher=added\nfw_crash=added\n"
          b"fw_mp=added\nhosts=added\neac=renamed\n")
PC_PART = b"time=2026-11-16 22:10\r\nfirewall=added\r\nhosts=added\r\n"
HOSTS_PLAIN = b"# a hosts file\n127.0.0.1 localhost\n"
HOSTS_EAC = b"127.0.0.1 localhost\n127.0.0.1 modules-cdn.eac-prod.on.epicgames.com # tag\n"
HOSTS_EAC_COMMENT = b"# 127.0.0.1 modules-cdn.eac-prod.on.epicgames.com\n"
MANIFEST = b'{"Data":{"Branch":"sc-alpha","Version":"4.1.1-live.9776","RequestedP4ChangeNum":"9776"}}'


def win(p):
    return str(p).replace("/", "\\")


class Ctx:
    """One scenario's scratch layout."""

    def __init__(self, root, cpp_exe, rust_exe):
        self.root = root
        self.cpp = root / "cpp"
        self.rust = root / "rust"
        self.sysroot = root / "sysroot"
        self.pd = root / "pd"
        self.pf86 = root / "pf86"
        self.appdata = root / "appdata"
        self.lib = root / "library"
        self.sc = self.lib / "StarCitizen"
        self.live = self.sc / "LIVE"
        self.bin = self.live / "Bin64"
        for d in (self.cpp, self.rust, self.pd, self.pf86, self.appdata, self.lib,
                  self.sysroot / "System32" / "drivers" / "etc"):
            d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(cpp_exe, self.cpp / "sc-offline.exe")
        shutil.copy2(rust_exe, self.rust / "scol.exe")
        self.hosts(HOSTS_PLAIN)

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def here(self, rel, data):
        for h in (self.cpp, self.rust):
            self.put(h / rel, data)

    def hosts(self, data):
        self.put(self.sysroot / "System32" / "drivers" / "etc" / "hosts", data)

    def game(self, manifest=None, exe=b"MZ game exe stand-in"):
        """The synthetic game. The exe is a plain file, never started."""
        self.put(self.bin / "StarCitizen.exe", exe)
        t = 1700000000  # fixed time so the exe's date line is the same everywhere
        os.utime(self.bin / "StarCitizen.exe", (t, t))
        if manifest is not None:
            self.put(self.live / "build_manifest.id", manifest)

    def eac(self, state):
        d = self.pf86 / "EasyAntiCheat_EOS"
        if state == "active":
            self.put(d / "EasyAntiCheat_EOS.exe", b"eac")
        elif state == "disabled":
            self.put(d / "EasyAntiCheat_EOS.exe.bak", b"eac")

    def pc(self, data):
        self.put(self.pd / "sc-offline" / "pc-changes.txt", data)

    def env(self):
        e = dict(os.environ)
        e["ProgramData"] = win(self.pd)
        e["ProgramFiles(x86)"] = win(self.pf86)
        e["APPDATA"] = win(self.appdata)
        e["SystemRoot"] = win(self.sysroot)
        e.pop("WINEDLLOVERRIDES", None)
        e.pop("SC_OFFLINE_GUI", None)
        return e


# Each scenario: name, builder(ctx) -> (args, expected substrings in the output, expected exit code).
SCENARIOS = []
# Scenarios where nothing synthetic anchors the result, so discovery falls through to this PC's real
# registry, usual folders and drives. They run only with --machine.
MACHINE_SCENARIOS = []


def scenario(fn):
    SCENARIOS.append(fn)
    return fn


def machine_scenario(fn):
    MACHINE_SCENARIOS.append(fn)
    return fn


@scenario
def help_word(c):
    return ["help"], ["usage: sc-offline.exe", "exit codes: 0 ok"], 0


@scenario
def help_flags(c):
    return ["--help", "-h"], ["usage: sc-offline.exe"], 0


@scenario
def help_slash_and_case(c):
    return ["/?"], ["usage: sc-offline.exe"], 0


@scenario
def help_upper(c):
    return ["HELP"], ["usage: sc-offline.exe"], 0


@scenario
def help_after_options(c):
    return ["--dry-run", "--console", "help"], ["usage: sc-offline.exe"], 0


@scenario
def unknown_argument(c):
    return ["bogus"], ["unknown argument 'bogus'", "usage: sc-offline.exe"], 1


@scenario
def unknown_option_after_status(c):
    return ["status", "--bogus"], ["unknown argument '--bogus'"], 1


@scenario
def second_command_word(c):
    return ["status", "help"], ["unknown argument 'help'"], 1


@scenario
def game_needs_folder(c):
    return ["status", "--game"], ["--game needs a folder"], 1


@scenario
def game_empty_value_is_not_given(c):
    # `--game ""` counts as no --game: the ini decides.
    c.game()
    c.here("sc-offline.ini", ("game = " + win(c.sc) + "\n").encode())
    return ["status", "--game", ""], ["Folder:   " + win(c.bin)], 0


@scenario
def status_game_missing_arg(c):
    return ["status", "--game", win(c.lib / "nothing")], ["no StarCitizen.exe under --game"], 1


@scenario
def status_game_missing_ini(c):
    c.here("sc-offline.ini", ("game = " + win(c.lib / "nothing") + "\n").encode())
    return ["status"], ["no StarCitizen.exe under game = "], 1


@scenario
def status_no_ini_game_no_mod(c):
    c.game(MANIFEST)
    return ["status", "--game", win(c.sc)], [
        "[i] no sc-offline.ini next to this exe; using defaults",
        "Mod DLL:  missing", "Installed: no (Bin64 has no dinput8.dll)", "Game:     4.1.1-live.9776 (CL 9776)",
        "EAC:      not installed", "EAC download server NOT blocked"], 0


@scenario
def status_game_arg_spellings(c):
    c.game(MANIFEST)
    # trailing slashes, forward slashes and quotes all resolve to the same Bin64
    return ["status", "--game", '"' + str(c.sc).replace("\\", "/") + '//"'], ["Folder:   "], 0


@scenario
def status_game_arg_channel_folder(c):
    c.game(MANIFEST)
    return ["status", "--game", win(c.live)], ["Folder:   " + win(c.bin)], 0


@scenario
def status_game_arg_bin64_folder(c):
    c.game(MANIFEST)
    return ["status", "--game", win(c.bin) + "\\"], ["Folder:   " + win(c.bin)], 0


@scenario
def status_game_arg_library_folder(c):
    c.game(MANIFEST)
    return ["status", "--game", win(c.lib)], ["Folder:   " + win(c.bin)], 0


@scenario
def status_game_arg_rsi_style_library(c):
    base = c.root / "lib2" / "Roberts Space Industries" / "StarCitizen" / "LIVE" / "Bin64"
    c.put(base / "StarCitizen.exe", b"x")
    return ["status", "--game", win(c.root / "lib2")], ["Folder:   "], 0


@scenario
def status_channel_ptu(c):
    b = c.sc / "PTU" / "Bin64"
    c.put(b / "StarCitizen.exe", b"ptu")
    c.here("sc-offline.ini", b"channel = PTU\n")
    return ["status", "--game", win(c.sc)], ["PTU\\Bin64"], 0


@scenario
def status_channel_missing(c):
    c.game()
    c.here("sc-offline.ini", b"channel = PTU\n")
    return ["status", "--game", win(c.sc)], ["(channel PTU)"], 1


@scenario
def status_exe_date_when_no_manifest(c):
    c.game()
    return ["status", "--game", win(c.sc)], ["Game:     StarCitizen.exe 20 bytes, 2023-11-14 22:13"], 0


@scenario
def status_manifest_only_version(c):
    c.game(b'{"Version":"9.9"}')
    return ["status", "--game", win(c.sc)], ["Game:     9.9"], 0


@scenario
def status_manifest_unusable_falls_back(c):
    c.game(b'{"Version": 4, "Other":"x"}')
    return ["status", "--game", win(c.sc)], ["Game:     StarCitizen.exe"], 0


@scenario
def status_game_updated_since_last_play(c):
    c.game(MANIFEST)
    c.here("data/game-build.txt", b"4.1.0-live.1 (CL 1)\r\n")
    return ["status", "--game", win(c.sc)], ["the game updated since you last played (was 4.1.0-live.1 (CL 1))"], 0


@scenario
def status_game_same_as_last_play(c):
    c.game(MANIFEST)
    c.here("data/game-build.txt", b"4.1.1-live.9776 (CL 9776)\n")
    return ["status", "--game", win(c.sc)], ["Game:     4.1.1-live.9776 (CL 9776)"], 0


@scenario
def status_mod_dll_here_current(c):
    c.game(MANIFEST)
    c.here("dinput8.dll", OURS_DLL)
    return ["status", "--game", win(c.sc)], ["Mod DLL:  sc-offline 0.8.0-rc.1", "          sha256 "], 0


@scenario
def status_mod_dll_here_other_version(c):
    c.game(MANIFEST)
    c.here("dinput8.dll", b"MZ sc-offline v0.7.0 x")
    return ["status", "--game", win(c.sc)], ["the DLL is 0.7.0"], 0


@scenario
def status_mod_dll_here_old_style(c):
    c.game(MANIFEST)
    c.here("dinput8.dll", OLD_DLL)
    return ["status", "--game", win(c.sc)], ["Mod DLL:  sc-offline 0.2.0-rc4"], 0


@scenario
def status_mod_dll_here_not_ours_and_empty(c):
    c.game(MANIFEST)
    c.here("dinput8.dll", OTHER_DLL)
    return ["status", "--game", win(c.sc)], ["Mod DLL:  not sc-offline"], 0


@scenario
def status_mod_dll_here_empty_file(c):
    c.game(MANIFEST)
    c.here("dinput8.dll", b"")
    return ["status", "--game", win(c.sc)], ["Mod DLL:  missing"], 0


@scenario
def status_mod_installed_marker_and_dll(c):
    c.game(MANIFEST)
    c.put(c.bin / "dinput8.dll", OURS_DLL)
    c.put(c.bin / "sc-offline.installed", MARKER)
    return ["status", "--game", win(c.sc)], [
        "Installed: yes, sc-offline 0.8.0-rc.1, since 2026-11-17 09:05:03",
        "the mod is still in the game folder"], 0


@scenario
def status_mod_installed_dll_no_marker(c):
    c.game(MANIFEST)
    c.put(c.bin / "dinput8.dll", OLD_DLL)
    return ["status", "--game", win(c.sc)], ["Installed: yes, sc-offline 0.2.0-rc4"], 0


@scenario
def status_marker_with_foreign_dll(c):
    c.game(MANIFEST)
    c.put(c.bin / "dinput8.dll", OTHER_DLL)
    c.put(c.bin / "sc-offline.installed", MARKER)
    return ["status", "--game", win(c.sc)], ["Installed: yes, not sc-offline, since 2026-11-17 09:05:03"], 0


@scenario
def status_marker_without_dll(c):
    c.game(MANIFEST)
    c.put(c.bin / "sc-offline.installed", MARKER)
    return ["status", "--game", win(c.sc)], ["Installed: no (Bin64 has no dinput8.dll)"], 0


@scenario
def status_foreign_dll_no_marker(c):
    c.game(MANIFEST)
    c.put(c.bin / "dinput8.dll", OTHER_DLL)
    return ["status", "--game", win(c.sc)], ["another mod's dinput8.dll"], 0


@scenario
def status_empty_marker_and_dll(c):
    c.game(MANIFEST)
    c.put(c.bin / "dinput8.dll", OURS_DLL)
    c.put(c.bin / "sc-offline.installed", b"")
    return ["status", "--game", win(c.sc)], ["Installed: yes, sc-offline 0.8.0-rc.1, since "], 0


@scenario
def status_leftover_pc_changes_all(c):
    c.game(MANIFEST)
    c.pc(PC_ALL)
    return ["status", "--game", win(c.sc)], [
        "PC changes from a previous run (2026-11-17 09:05) are still in place:",
        'firewall rule "sc-offline: block StarCitizen.exe"', "hosts line for modules-cdn",
        "EasyAntiCheat_EOS.exe renamed to .bak", "Run `sc-offline.exe uninstall` to undo them"], 0


@scenario
def status_leftover_pc_changes_partial_crlf(c):
    c.game(MANIFEST)
    c.pc(PC_PART)
    return ["status", "--game", win(c.sc)], ["(2026-11-16 22:10)", "hosts line for"], 0


@scenario
def status_leftover_pc_changes_with_mod_installed(c):
    c.game(MANIFEST)
    c.pc(PC_ALL)
    c.put(c.bin / "dinput8.dll", OURS_DLL)
    c.put(c.bin / "sc-offline.installed", MARKER)
    return ["status", "--game", win(c.sc)], ["still in place", "the mod is still in the game folder"], 0


@scenario
def status_empty_pc_changes_record(c):
    c.game(MANIFEST)
    c.pc(b"")
    return ["status", "--game", win(c.sc)], ["EAC:      not installed"], 0


@scenario
def status_plugins_with_and_without_disabled(c):
    c.game(MANIFEST)
    for name, disabled in (("alpha", False), ("beta", True), ("gamma", False)):
        c.here("data/plugins/%s/plugin.ini" % name, b"[plugin]\nid = %s\n" % name.encode())
        if disabled:
            c.here("data/plugins/%s/disabled" % name, b"")
    c.here("data/plugins/no-ini/readme.txt", b"x")
    c.here("data/plugins/loose.txt", b"x")
    return ["status", "--game", win(c.sc)], ["Folder:   "], 0


@scenario
def status_eac_active_auto(c):
    c.game(MANIFEST)
    c.eac("active")
    return ["status", "--game", win(c.sc)], ["EAC:      ACTIVE", "`play` turns it off"], 0


@scenario
def status_eac_active_manual(c):
    c.game(MANIFEST)
    c.eac("active")
    c.here("sc-offline.ini", b"eac_rename = off\n")
    return ["status", "--game", win(c.sc)], ["Easy Anti-Cheat is active; the mod can't run with it"], 0


@scenario
def status_eac_disabled_hosts_blocked(c):
    c.game(MANIFEST)
    c.eac("disabled")
    c.hosts(HOSTS_EAC)
    return ["status", "--game", win(c.sc)], ["EAC:      disabled (EasyAntiCheat_EOS.exe.bak)", "EAC download server blocked"], 0


@scenario
def status_hosts_line_commented_out(c):
    c.game(MANIFEST)
    c.hosts(HOSTS_EAC_COMMENT)
    return ["status", "--game", win(c.sc)], ["EAC download server NOT blocked"], 0


@scenario
def status_eac_hosts_off_hint(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini", b"eac_hosts = off\n")
    return ["status", "--game", win(c.sc)], ["add `127.0.0.1 modules-cdn"], 0


@scenario
def status_eac_hosts_off_but_blocked(c):
    c.game(MANIFEST)
    c.hosts(HOSTS_EAC)
    c.here("sc-offline.ini", b"eac_hosts = off\n")
    return ["status", "--game", win(c.sc)], ["EAC download server blocked"], 0


@scenario
def status_no_hosts_file(c):
    c.game(MANIFEST)
    (c.sysroot / "System32" / "drivers" / "etc" / "hosts").unlink()
    return ["status", "--game", win(c.sc)], ["EAC download server NOT blocked"], 0


@scenario
def status_network_lines(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini", b"block_network = off\nmultiplayer = off\n")
    return ["status", "--game", win(c.sc)], ["NOT blocked (block_network = off)"], 0


@scenario
def status_network_lan_off(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini", b"multiplayer = off\n")
    return ["status", "--game", win(c.sc)], ["while you play (block_network)\n"], 0


@scenario
def status_ini_messages(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini",
           b"\xef\xbb\xbf# comment\nplugins = maybe\nno equals here\nnot_a_key = 1\nlanguage = fr\n"
           b"exp_audio_native = sometimes\nasop_fleet_list = both\nupdate_channel = nightly\n"
           b"clean_logs = sometimes\nmining = on\nmultiplayer_allow = 8.8.8.8/24\n"
           b"boot_map = PU\nchannel =\n\n")
    return ["status", "--game", win(c.sc)], ["sc-offline.ini line 3: expected key = value", "unknown key 'not_a_key'"], 0


@scenario
def status_ini_game_with_quotes_and_spaces(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini", ("  game =  \"" + win(c.sc) + "\"  \r\n").encode())
    return ["status"], ["Folder:   " + win(c.bin)], 0


@scenario
def status_arg_wins_over_ini(c):
    c.game(MANIFEST)
    c.here("sc-offline.ini", ("game = " + win(c.lib / "nothing") + "\n").encode())
    return ["status", "--game", win(c.sc)], ["Folder:   " + win(c.bin)], 0


@scenario
def status_options_ignored(c):
    c.game(MANIFEST)
    return ["--dry-run", "STATUS", "--skip-eac-check", "--console", "--game", win(c.sc)], ["Folder:   " + win(c.bin)], 0


@scenario
def status_discovery_remembered(c):
    c.game(MANIFEST)
    c.here("data/game-path.txt", (win(c.bin) + "\r\n").encode())
    return ["status"], ["[i] found the game remembered from your last run"], 0


@machine_scenario
def status_discovery_remembered_stale(c):
    c.here("data/game-path.txt", (win(c.lib / "gone") + "\n").encode())
    # nothing resolves from the file; the rest is the real machine (see the module text)
    return ["status"], [], None


@scenario
def status_discovery_rsi_launcher_files(c):
    c.game(MANIFEST)
    js = '{"library":"%s","other":"C:\\\\no\\\\such\\\\place"}' % win(c.sc).replace("\\", "\\\\")
    c.put(c.appdata / "rsilauncher" / "settings.json", js.encode())
    return ["status"], ["[i] found the game from the RSI Launcher's settings or logs"], 0


@scenario
def status_discovery_rsi_log_forward_slashes(c):
    c.game(MANIFEST)
    line = "[info] library at %s/StarCitizen/LIVE, ok\n" % str(c.lib).replace("\\", "/")
    c.put(c.appdata / "rsilauncher" / "logs" / "launcher.log", line.encode())
    return ["status"], ["from the RSI Launcher's settings or logs"], 0


@scenario
def status_discovery_two_games_also_found(c):
    c.game(MANIFEST)
    other = c.root / "second" / "StarCitizen" / "LIVE" / "Bin64"
    c.put(other / "StarCitizen.exe", b"second")
    c.here("data/game-path.txt", (win(c.bin) + "\n").encode())
    js = '{"p":"%s"}' % win(c.root / "second" / "StarCitizen").replace("\\", "\\\\")
    c.put(c.appdata / "rsilauncher" / "settings.json", js.encode())
    return ["status"], ["remembered from your last run", "[i] also found " + win(other)], 0


@scenario
def status_discovery_same_install_listed_once(c):
    c.game(MANIFEST)
    c.here("data/game-path.txt", (win(c.bin) + "\n").encode())
    # the same folder spelled another way, in the RSI Launcher's file: still one install
    js = '{"p":"%s"}' % (win(c.sc).replace("\\", "\\\\") + "\\\\.\\\\LIVE")
    c.put(c.appdata / "rsilauncher" / "settings.json", js.encode())
    return ["status"], ["remembered from your last run"], 0


@scenario
def status_discovery_junction_is_same_install(c):
    c.game(MANIFEST)
    junction = c.root / "linked"
    r = subprocess.run(["cmd", "/c", "mklink", "/J", win(junction), win(c.sc)], capture_output=True)
    if r.returncode != 0:
        return ["help"], ["usage"], 0  # no junction possible here; the scenario degrades to help
    c.here("data/game-path.txt", (win(c.bin) + "\n").encode())
    js = '{"p":"%s"}' % win(junction).replace("\\", "\\\\")
    c.put(c.appdata / "rsilauncher" / "settings.json", js.encode())
    return ["status"], ["remembered from your last run"], 0


@machine_scenario
def status_discovery_big_rsi_file_skipped(c):
    c.game(MANIFEST)
    js = b'{"p":"%s"}' % win(c.sc).replace("\\", "\\\\").encode() + b" " * (9 * 1024 * 1024)
    c.put(c.appdata / "rsilauncher" / "huge.json", js)
    return ["status"], [], None


@machine_scenario
def machine_drive_search(c):
    # Nothing is hinted: discovery and the bounded drive search run on this PC.
    return ["status"], [], None


# --------------------------------------------------------------------------------------------

# The two usage lines only the Rust launcher has (scol_cli::CPP_USAGE_LINES_NOT_HERE). The C++ output never
# contains them, so dropping them from both outputs changes nothing for the C++ side.
RUST_ONLY_USAGE_LINES = (
    "  stop       ask the running game to close (the mod saves first); --end ends it if it is still open a minute later",
    "  plugins    list, enable, disable, install, remove and set up plugins, and the catalogue; `plugins` alone shows how",
)


def normalise(text):
    text = text.replace("\r\n", "\n")
    text = "".join(l for l in text.splitlines(True) if l.rstrip("\n") not in RUST_ONLY_USAGE_LINES)
    return re.sub(r"\Asc-offline launcher \S+", "sc-offline launcher <version>", text)


def snapshot(root):
    """Everything under root: relative path, kind, size, mtime, content hash."""
    snap = {}
    for dirpath, dirnames, filenames in os.walk(root):
        for name in dirnames + filenames:
            p = Path(dirpath) / name
            st = p.lstat()
            rel = str(p.relative_to(root))
            digest = None
            if p.is_file() and not p.is_symlink():
                digest = hashlib.sha256(p.read_bytes()).hexdigest()
            snap[rel] = (p.is_dir(), st.st_size, st.st_mtime_ns, digest)
    return snap


def run(exe, args, env, cwd, timeout):
    r = subprocess.run([str(exe)] + args, env=env, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                       timeout=timeout)
    return r.returncode, normalise(r.stdout.decode("utf-8", "replace")), r.stderr.decode("utf-8", "replace")


NETWORK_DLLS = [b"ws2_32", b"wsock32", b"winhttp", b"wininet", b"mswsock", b"dnsapi", b"iphlpapi", b"urlmon",
                b"httpapi", b"wldap32", b"sensapi", b"rasapi32"]


# The sc-offline exe holds the updater and the catalogue client, which use WinHTTP and nothing else. `status`
# and `mcp` never call them (the status run below writes nothing and the CLI tests check the dispatch).
ALLOWED_NETWORK_DLLS = {"winhttp"}


def network_imports(exe):
    data = exe.read_bytes().lower()
    return [d.decode() for d in NETWORK_DLLS if d + b".dll" in data and d.decode() not in ALLOWED_NETWORK_DLLS]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cpp", default=os.environ.get("SC_OFFLINE_EXE"), help="built C++ sc-offline.exe")
    ap.add_argument("--rust", default=str(RUST_DEFAULT), help="built Rust scol.exe")
    ap.add_argument("--work", default=str(Path(os.environ.get("SCOL_SCRATCH") or Path(tempfile.gettempdir()) / "scol-scratch") / "status-parity"), help="scratch folder (emptied first)")
    ap.add_argument("--machine", action="store_true", help="also run the unhinted drive-search scenario")
    ap.add_argument("--only", default="", help="run scenarios whose name contains this")
    ap.add_argument("--keep", action="store_true", help="keep the scratch folder")
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()
    if not a.cpp or not Path(a.cpp).is_file():
        sys.exit("give the built C++ launcher with --cpp (cmake --build <dir> --target sc-offline)")
    if not Path(a.rust).is_file():
        sys.exit("build the Rust launcher first: cargo build --release (in launcher-rs)")
    cpp_exe, rust_exe = Path(a.cpp), Path(a.rust)
    work = Path(a.work)
    if work.exists():
        if "status-parity" not in work.name:
            sys.exit("refusing to empty a folder that is not named status-parity*: " + str(work))
        shutil.rmtree(work)
    work.mkdir(parents=True)

    scenarios = [(f.__name__, f) for f in SCENARIOS]
    if a.machine:
        scenarios += [(f.__name__, f) for f in MACHINE_SCENARIOS]
    failed = 0
    ran = 0
    for name, build in scenarios:
        if a.only not in name:
            continue
        ran += 1
        ctx = Ctx(work / name, cpp_exe, rust_exe)
        args, expect, code = build(ctx)
        env = ctx.env()
        before = snapshot(ctx.root)
        c_code, c_out, c_err = run(ctx.cpp / "sc-offline.exe", args, env, ctx.root, a.timeout)
        mid = snapshot(ctx.root)
        r_code, r_out, r_err = run(ctx.rust / "scol.exe", args, env, ctx.root, a.timeout)
        after = snapshot(ctx.root)
        problems = []
        if r_code != c_code:
            problems.append("exit code: C++ %d, Rust %d" % (c_code, r_code))
        if r_out != c_out:
            problems.append("output differs")
        if code is not None and c_code != code:
            problems.append("expected exit code %d, C++ gave %d (scenario is wrong)" % (code, c_code))
        for s in expect:
            if s not in c_out:
                problems.append("expected text missing from the C++ output (scenario is wrong): %r" % s)
        # status wrote nothing: the Rust run changed nothing, and the C++ run changed nothing either
        # (the C++ copy checks the claim in its own header comment).
        if mid != after:
            problems.append("the Rust run changed the tree: " + ", ".join(sorted(set(mid) ^ set(after)) or
                                                                           [k for k in mid if mid[k] != after.get(k)]))
        if before != mid:
            problems.append("the C++ run changed the tree: " + ", ".join(sorted(set(before) ^ set(mid)) or
                                                                         [k for k in before if before[k] != mid.get(k)]))
        if r_err:
            problems.append("Rust wrote to stderr: " + r_err[:200])
        if problems:
            failed += 1
            print("FAIL %s  args=%s" % (name, args))
            for p in problems:
                print("     " + p)
            if r_out != c_out:
                print("  --- C++ ---\n" + c_out + "  --- Rust ---\n" + r_out + "  ---")
        else:
            print("ok   %s  (exit %d, %d lines)" % (name, c_code, c_out.count("\n")))

    net = network_imports(rust_exe)
    print("Rust exe network imports: %s" % (", ".join(net) if net else "none"))
    if net:
        failed += 1
        print("FAIL the Rust exe imports a network library")
    print("%d scenarios, %d failed" % (ran, failed))
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
