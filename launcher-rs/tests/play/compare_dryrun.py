#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parity check for `play`, `install` and `uninstall` with --dry-run: the built C++ sc-offline.exe against
the Rust sc-offline.exe, on the same synthetic folders.

Only dry runs are compared, because a dry run is the one form of `play` that starts nothing and changes
nothing (it still writes data\\launcher.log, and so does the Rust program). Every scenario builds the same
layout twice under a scratch folder (one world per program), runs the same command line in each and
requires the same exit code, the same text, the same file tree and the same data\\launcher.log.

No real game folder is used: a world holds a fake game (a plain file named StarCitizen.exe, never
started), the launcher folder (here\\: dinput8.dll, sc-offline.ini, data\\) and the ProgramData,
ProgramFiles(x86), APPDATA and SystemRoot folders the launcher reads, which the environment points into
the world. Every game folder is given with --game; the script refuses to run any command line without
--dry-run.

Differences from a byte-for-byte diff, by design:
  * the C++ launcher prints through the C runtime in text mode, so a pipe sees CRLF; line endings are
    normalised.
  * the world path is replaced by @W in text and in the log; the `--- <time>` log header time is
    replaced by <T>; the exe name in the log header line is replaced by <exe> (the Rust program may be
    started under another name).
  * paths are ASCII.

Not run in game. Usage:
  python compare_dryrun.py --cpp <sc-offline.exe> [--rust <sc-offline.exe>] [--work <folder>]
                           [--only <substring>] [--keep]
Exit code 0 when every scenario matches, 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUST_DEFAULT = Path(os.environ.get("SCOL_RUST_EXE") or HERE.parents[1] / "target" / "release" / "sc-offline.exe")
SCRATCH_ROOT = Path(os.environ.get("SCOL_SCRATCH") or Path(tempfile.gettempdir()) / "scol-scratch")
SCRATCH_ROOT.mkdir(parents=True, exist_ok=True)
SCRATCH_ROOT = SCRATCH_ROOT.resolve()

OURS_DLL = b"MZ\x90\x00 padding sc-offline v0.8.0\x00 more bytes"
OTHER_DLL = b"MZ\x90\x00 some other mod's dinput8"
MARKER = b"version=0.8.0\nsha256=abc\nmode=install\ntime=2026-11-17 09:05:03\nxml=none\n"
PC_ALL = (b"time=2026-11-17 09:05\nfirewall=added\nfw_launcher=added\nfw_crash=added\n"
          b"fw_mp=added\nhosts=added\neac=renamed\n")
HOSTS_PLAIN = b"# a hosts file\n127.0.0.1 localhost\n"
HOSTS_EAC = b"127.0.0.1 localhost\n127.0.0.1 modules-cdn.eac-prod.on.epicgames.com # tag\n"
XML = b"<Root>synthetic default_1.xml</Root>\n"


def win(p):
    return str(p).replace("/", "\\")


class World:
    def __init__(self, root, exe):
        self.root = root
        self.here = root / "here"
        self.sysroot = root / "sysroot"
        self.pd = root / "pd"
        self.pf86 = root / "pf86"
        self.appdata = root / "appdata"
        self.sc = root / "library" / "StarCitizen"
        self.live = self.sc / "LIVE"
        self.bin = self.live / "Bin64"
        for d in (self.here / "data", self.pd, self.pf86, self.appdata, self.bin,
                  self.sysroot / "System32" / "drivers" / "etc"):
            d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exe, self.here / "sc-offline.exe")
        self.exe = self.here / "sc-offline.exe"
        self.put(self.bin / "StarCitizen.exe", b"MZ game exe stand-in")
        t = 1700000000  # fixed time so the exe's date line is the same everywhere
        os.utime(self.bin / "StarCitizen.exe", (t, t))
        self.put(self.here / "dinput8.dll", OURS_DLL)
        self.put(self.sysroot / "System32" / "drivers" / "etc" / "hosts", HOSTS_PLAIN)

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def ini(self, text, with_game=True):
        head = ("game = " + win(self.sc) + "\n") if with_game else ""
        self.put(self.here / "sc-offline.ini", (head + text).encode())

    def env(self, extra):
        e = dict(os.environ)
        e["ProgramData"] = win(self.pd)
        e["ProgramFiles(x86)"] = win(self.pf86)
        e["APPDATA"] = win(self.appdata)
        e["SystemRoot"] = win(self.sysroot)
        e["SC_OFFLINE_GUI"] = "1"  # no "Press Enter" wait, no window
        e.pop("WINEDLLOVERRIDES", None)
        for k in ("SCO_LOADOUT_ASYNC", "SCO_INVENTORY_READ", "SCO_ACTOR_STATS_WRITE",
                  "SCO_CVARS_NATIVE", "SCO_AUDIO_NATIVE", "SCO_ENTITIES_DIAG"):
            e.pop(k, None)
        e.update(extra)
        return e


SCENARIOS = []


def scenario(fn):
    SCENARIOS.append(fn)
    return fn


# A scenario builds the world (same call for both programs) and returns (args, extra env).

@scenario
def play_defaults(w):
    w.ini("check_updates = off\n")
    return ["--dry-run"], {}


@scenario
def play_with_eac_active(w):
    w.ini("check_updates = off\n")
    d = w.pf86 / "EasyAntiCheat_EOS"
    w.put(d / "EasyAntiCheat_EOS.exe", b"eac")
    return ["play", "--dry-run"], {}


@scenario
def play_eac_active_not_handled(w):
    w.ini("check_updates = off\neac_rename = off\n")
    w.put(w.pf86 / "EasyAntiCheat_EOS" / "EasyAntiCheat_EOS.exe", b"eac")
    return ["play", "--dry-run"], {}


@scenario
def play_eac_active_skip(w):
    w.ini("check_updates = off\neac_rename = off\n")
    w.put(w.pf86 / "EasyAntiCheat_EOS" / "EasyAntiCheat_EOS.exe", b"eac")
    return ["play", "--dry-run", "--skip-eac-check"], {}


@scenario
def play_nothing_to_change_on_the_pc(w):
    w.ini("check_updates = off\nblock_network = off\neac_hosts = off\neac_rename = off\n")
    return ["play", "--dry-run"], {}


@scenario
def play_settings_in_the_env_text(w):
    w.ini("check_updates = off\nboot_map = PU\nstart_ship = ANVL_Arrow\nstart = Stanton\n"
          "plugins = off\nrelocation = off\nasop = off\nclean_logs = always\n")
    return ["play", "--dry-run"], {}


@scenario
def play_clean_logs_never(w):
    w.ini("check_updates = off\nclean_logs = never\n")
    return ["play", "--dry-run"], {}


@scenario
def play_capability_switches(w):
    w.ini("check_updates = off\nexp_cvars_native = off\nexp_entities_diag = on\n"
          "exp_loadout_async = off\n")
    return ["play", "--dry-run"], {}


@scenario
def play_inherited_diag_variable_is_removed(w):
    w.ini("check_updates = off\n")
    return ["play", "--dry-run"], {"SCO_ENTITIES_DIAG": "1", "SCO_AUDIO_NATIVE": ""}


@scenario
def play_multiplayer_allow_list(w):
    w.ini("check_updates = off\nmultiplayer_allow = 192.168.1.0/24, 10.1.2.3\n")
    return ["play", "--dry-run"], {}


@scenario
def play_with_a_foreign_dll_and_an_xml(w):
    w.ini("check_updates = off\n")
    w.put(w.bin / "dinput8.dll", OTHER_DLL)
    w.put(w.here / "data" / "OfflineDB" / "default_1.xml", XML)
    w.put(w.live / "user" / "client" / "0" / "default_1.xml", b"<Root>player</Root>\n")
    return ["play", "--dry-run"], {}


@scenario
def play_after_a_crash_leaves_pc_changes(w):
    w.ini("check_updates = off\n")
    w.put(w.bin / "dinput8.dll", OURS_DLL)
    w.put(w.bin / "sc-offline.installed", MARKER)
    w.put(w.pd / "sc-offline" / "pc-changes.txt", PC_ALL)
    w.put(w.sysroot / "System32" / "drivers" / "etc" / "hosts", HOSTS_EAC)
    return ["play", "--dry-run"], {}


@scenario
def play_dll_missing_next_to_the_exe(w):
    w.ini("check_updates = off\n")
    (w.here / "dinput8.dll").unlink()
    return ["play", "--dry-run"], {}


@scenario
def play_game_missing(w):
    w.ini("", with_game=False)
    return ["play", "--dry-run", "--game", win(w.root / "nothing")], {}


@scenario
def play_game_argument_wins(w):
    w.ini("check_updates = off\n", with_game=False)
    return ["play", "--game", win(w.sc), "--dry-run"], {}


@scenario
def play_game_argument_is_bin64(w):
    w.ini("check_updates = off\n", with_game=False)
    return ["--dry-run", "--game", win(w.bin)], {}


@scenario
def play_ini_warnings(w):
    w.ini("check_updates = off\nplugins = maybe\nnonsense = 1\nexp_audio_native = perhaps\n")
    return ["play", "--dry-run"], {}


@scenario
def play_no_ini(w):
    return ["play", "--dry-run", "--game", win(w.sc)], {}


@scenario
def install_dry(w):
    w.ini("check_updates = off\n")
    w.put(w.here / "data" / "OfflineDB" / "default_1.xml", XML)
    return ["install", "--dry-run"], {}


@scenario
def install_dry_with_eac_skip(w):
    w.ini("check_updates = off\n")
    w.put(w.pf86 / "EasyAntiCheat_EOS" / "EasyAntiCheat_EOS.exe", b"eac")
    return ["install", "--dry-run", "--skip-eac-check"], {}


@scenario
def install_dry_with_eac_stops(w):
    w.ini("check_updates = off\n")
    w.put(w.pf86 / "EasyAntiCheat_EOS" / "EasyAntiCheat_EOS.exe", b"eac")
    return ["install", "--dry-run"], {}


@scenario
def uninstall_dry_installed(w):
    w.ini("check_updates = off\n")
    w.put(w.bin / "dinput8.dll", OURS_DLL)
    w.put(w.bin / "sc-offline.installed", MARKER)
    return ["uninstall", "--dry-run"], {}


@scenario
def uninstall_dry_with_pc_changes_left(w):
    w.ini("check_updates = off\n")
    w.put(w.bin / "dinput8.dll", OURS_DLL)
    w.put(w.bin / "sc-offline.installed", MARKER)
    w.put(w.pd / "sc-offline" / "pc-changes.txt", PC_ALL)
    w.put(w.sysroot / "System32" / "drivers" / "etc" / "hosts", HOSTS_EAC)
    return ["uninstall", "--dry-run"], {}


@scenario
def uninstall_dry_nothing_installed(w):
    w.ini("check_updates = off\n")
    return ["uninstall", "--dry-run"], {}


@scenario
def uninstall_dry_with_other_mods_dll_set_aside(w):
    w.ini("check_updates = off\n")
    w.put(w.bin / "dinput8.dll.sc-offline-backup", OTHER_DLL)
    w.put(w.bin / "dinput8.dll", OURS_DLL)
    w.put(w.bin / "sc-offline.installed", MARKER)
    return ["uninstall", "--dry-run"], {}


TIME_RE = re.compile(r"^--- \d{4}-\d\d-\d\d \d\d:\d\d:\d\d ", re.M)


def norm(text, root):
    t = text.replace("\r\n", "\n")
    t = t.replace(win(root), "@W").replace(str(root).replace("\\", "/"), "@W")
    t = TIME_RE.sub("--- <T> ", t)
    t = re.sub(r"^(--- <T> )\S*sc-offline\.exe", r"\1<exe>", t, flags=re.M)
    return t


def tree(root, skip):
    out = {}
    for p in sorted(root.rglob("*")):
        rel = p.relative_to(root).as_posix()
        if rel in skip:
            continue
        out[rel] = None if p.is_dir() else p.read_bytes()
    return out


def run_one(world, args, extra, timeout):
    if "--dry-run" not in args:
        raise SystemExit("refusing to run without --dry-run: " + " ".join(args))
    if not str(world.root.resolve()).lower().startswith(str(SCRATCH_ROOT).lower().replace("/", "\\")):
        raise SystemExit("world is not under the scratch folder")
    args = [a for a in args]
    r = subprocess.run([str(world.exe)] + args, cwd=str(world.root), env=world.env(extra),
                       capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
    out = r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")
    log = world.here / "data" / "launcher.log"
    logtext = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    return r.returncode, out, logtext


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpp", required=True, help="built C++ sc-offline.exe")
    ap.add_argument("--rust", default=str(RUST_DEFAULT), help="built Rust sc-offline.exe")
    ap.add_argument("--work", default=str(SCRATCH_ROOT / "play-parity"))
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--timeout", type=int, default=60)
    a = ap.parse_args()
    work = Path(a.work)
    if not str(work.resolve()).lower().startswith(str(SCRATCH_ROOT).lower().replace("/", "\\")):
        raise SystemExit("--work must be under " + str(SCRATCH_ROOT))
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    failures = 0
    ran = 0
    for fn in SCENARIOS:
        if a.only and a.only not in fn.__name__:
            continue
        ran += 1
        results = {}
        for which, exe in (("cpp", a.cpp), ("rust", a.rust)):
            w = World(work / fn.__name__ / which, exe)
            args, extra = fn(w)
            results[which] = (w, run_one(w, args, extra, a.timeout))
        (wc, (cc, oc, lc)), (wr, (cr, orr, lr)) = results["cpp"], results["rust"]
        oc, orr = norm(oc, wc.root), norm(orr, wr.root)
        lc, lr = norm(lc, wc.root), norm(lr, wr.root)
        skip_c = {"here/sc-offline.exe", "here/data/launcher.log"}
        tc, tr = tree(wc.root, skip_c), tree(wr.root, skip_c)
        # the file contents may name the world path (ini game line): compare after the same replace
        def fold(t, root):
            return {k: (None if v is None else norm(v.decode("utf-8", "replace"), root)) for k, v in t.items()}
        tc, tr = fold(tc, wc.root), fold(tr, wr.root)
        problems = []
        if cc != cr:
            problems.append(f"exit code: cpp {cc}, rust {cr}")
        if oc != orr:
            problems.append("output differs:\n" + diff(oc, orr))
        if lc != lr:
            problems.append("launcher.log differs:\n" + diff(lc, lr))
        if tc != tr:
            problems.append("file tree differs: " + ", ".join(sorted(set(tc) ^ set(tr)) or ["(contents)"]))
        if problems:
            failures += 1
            print(f"FAIL {fn.__name__}")
            for p in problems:
                print("   " + p.replace("\n", "\n   "))
        else:
            print(f"ok   {fn.__name__}  (exit {cc}, {len(oc.splitlines())} lines)")
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    print(f"{ran - failures} of {ran} scenarios match")
    return 1 if failures or ran == 0 else 0


def diff(x, y):
    import difflib
    return "\n".join(difflib.unified_diff(x.splitlines(), y.splitlines(), "cpp", "rust", lineterm="", n=1))


if __name__ == "__main__":
    sys.exit(main())
