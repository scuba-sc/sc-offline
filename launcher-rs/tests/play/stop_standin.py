#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Runs the real Rust `sc-offline.exe stop` against a stand-in game under D:\\t. The real game is never started.

The stand-in is `scol-play-standin.exe` copied to StarCitizen.exe in a scratch Bin64. With
SCOL_STANDIN_STOPPABLE=1 it creates the two named events of the stop channel for its own process id and opens one
small window, like the game with the mod loaded: it writes standin-stopped.txt when the stop event is set (the mod's
exit work) and exits when its window gets WM_CLOSE. Nothing on this PC changes; no firewall, hosts or EAC file is
touched, and no other process is closed: the script checks that a second stand-in under another folder is left alone.

Scenarios:
  none      no process of the install: "No Star Citizen process ... found to stop", exit 0
  channel   the mod answers: the exit work runs first, then WM_CLOSE closes the window, "The game closed."
  noanswer  the mod does not answer for 10 s: the window is closed anyway and the text says so
  end       the game ignores WM_CLOSE: `stop --end` waits a minute, ends exactly that process (this one takes ~70 s)
  other     a stand-in of another folder is not asked to close

Not run in game. Usage: python stop_standin.py [--rust <sc-offline.exe>] [--standin <scol-play-standin.exe>]
                                               [--only <name>] [--skip-end]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get("SCOL_SCRATCH", "D:/t"))
RUST = Path(os.environ.get("SCOL_RUST_EXE", "D:/t/cargo-play/debug/sc-offline.exe"))
STANDIN = Path(os.environ.get("SCOL_STANDIN_EXE", "D:/t/cargo-play/debug/scol-play-standin.exe"))
OURS_DLL = b"MZ\x90\x00 padding sc-offline v0.8.0\x00 more bytes"


def win(p):
    return str(p).replace("/", "\\")


class World:
    def __init__(self, name, rust, standin):
        self.root = ROOT / ("stop-standin-" + name)
        shutil.rmtree(self.root, ignore_errors=True)
        self.here = self.root / "here"
        self.lib = self.root / "library" / "StarCitizen"
        self.bin = self.lib / "LIVE" / "Bin64"
        for d in (self.here / "data", self.bin):
            d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(rust, self.here / "sc-offline.exe")
        shutil.copy2(standin, self.bin / "StarCitizen.exe")
        (self.here / "dinput8.dll").write_bytes(OURS_DLL)
        # the mod's marker in Bin64: the launcher then expects the mod to answer the stop event
        (self.bin / "sc-offline.installed").write_text("version=0.8.0\nmode=play\n")
        (self.here / "sc-offline.ini").write_text(f"game = {win(self.lib)}\ncheck_updates = off\n")
        (self.here / "data" / "launcher.log").write_text("")
        self.procs = []

    def start_game(self, exe, **flags):
        env = dict(os.environ)
        env.update({"SCOL_STANDIN_STOPPABLE": "1", "SCOL_STANDIN_MS": "120000"})
        env.update({k: str(v) for k, v in flags.items()})
        p = subprocess.Popen([str(exe)], env=env, cwd=str(Path(exe).parent), stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.procs.append(p)
        time.sleep(1.0)  # let it create its events and its window
        return p

    def stop(self, *args, timeout=150):
        env = dict(os.environ)
        env["SC_OFFLINE_GUI"] = "1"
        t0 = time.time()
        r = subprocess.run([str(self.here / "sc-offline.exe"), "stop", *args], cwd=str(self.root), env=env,
                           stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
        out = r.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
        err = r.stderr.decode("utf-8", "replace").replace("\r\n", "\n")
        return r.returncode, out, err, time.time() - t0

    def log(self):
        return (self.here / "data" / "launcher.log").read_text(errors="replace")

    def cleanup(self):
        for p in self.procs:
            if p.poll() is None:
                p.kill()
                p.wait()


def check(failures, cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def scenario_none(a, failures):
    w = World("none", a.rust, a.standin)
    code, out, err, _ = w.stop()
    print(out, err)
    check(failures, code == 0, "none: exit 0")
    check(failures, out == "No Star Citizen process of this install was found to stop.\n", "none: says nothing was found")
    check(failures, "Stop pressed: no game process of this install found; nothing done" in w.log(), "none: logged")
    w.cleanup()


def scenario_channel(a, failures):
    w = World("channel", a.rust, a.standin)
    game = w.start_game(w.bin / "StarCitizen.exe")
    code, out, err, secs = w.stop()
    print(out, err)
    game_code = game.wait(timeout=10)
    check(failures, code == 0, "channel: exit 0")
    check(failures, "Asked 1 game process(es) to close; 1 window(s) told to close." in out, "channel: one process, one window")
    check(failures, out.endswith("The game closed.\n"), "channel: the game closed")
    check(failures, (w.bin / "standin-stopped.txt").is_file(), "channel: the exit work ran (stop event reached the stand-in)")
    check(failures, game_code == 0, "channel: the stand-in left through WM_CLOSE (exit 0)")
    log = w.log()
    check(failures, "mod acknowledged after" in log, "channel: the log says the mod answered")
    check(failures, log.count("] WM_CLOSE posted to 1 window(s)") == 1, "channel: WM_CLOSE logged once")
    check(failures, log.find("mod acknowledged after") < log.find("WM_CLOSE posted"), "channel: the answer came before WM_CLOSE")
    check(failures, secs < 20, "channel: took %.1f s" % secs)
    w.cleanup()


def scenario_noanswer(a, failures):
    w = World("noanswer", a.rust, a.standin)
    game = w.start_game(w.bin / "StarCitizen.exe", SCOL_STANDIN_NO_ACK=1)
    code, out, err, secs = w.stop()
    print(out, err)
    game.wait(timeout=10)
    check(failures, code == 0, "noanswer: exit 0")
    check(failures, "The mod didn't answer; closing without its save." in out, "noanswer: says so")
    check(failures, out.endswith("The game closed.\n"), "noanswer: the window was closed anyway")
    check(failures, 9 < secs < 30, "noanswer: waited for the answer first (%.1f s)" % secs)
    check(failures, "no acknowledgement after" in w.log(), "noanswer: logged")
    w.cleanup()


def scenario_other(a, failures):
    w = World("other", a.rust, a.standin)
    other = w.root / "elsewhere" / "Bin64"
    other.mkdir(parents=True)
    shutil.copy2(a.standin, other / "StarCitizen.exe")
    mine = w.start_game(w.bin / "StarCitizen.exe")
    theirs = w.start_game(other / "StarCitizen.exe")
    code, out, err, _ = w.stop()
    print(out, err)
    mine.wait(timeout=10)
    time.sleep(0.5)
    check(failures, code == 0, "other: exit 0")
    check(failures, "Asked 1 game process(es) to close" in out, "other: only one process was asked")
    check(failures, theirs.poll() is None, "other: the stand-in of another folder is still running")
    check(failures, not (other / "standin-stopped.txt").exists(), "other: it was not signalled")
    w.cleanup()


def scenario_end(a, failures):
    w = World("end", a.rust, a.standin)
    game = w.start_game(w.bin / "StarCitizen.exe", SCOL_STANDIN_IGNORE_CLOSE=1)
    # without --end and without a console, a game that stays is not ended; that is checked by the unit tests
    # (a minute each); here the explicit --end
    code, out, err, secs = w.stop("--end")
    print(out, err)
    ended = game.wait(timeout=10)
    check(failures, code == 0, "end: exit 0")
    check(failures, out.endswith("Ended the game process.\n"), "end: says it ended the process")
    check(failures, ended == 1, "end: the stand-in was ended (exit 1 from TerminateProcess), got %s" % ended)
    check(failures, 55 < secs < 100, "end: waited the minute first (%.1f s)" % secs)
    check(failures, "pid" in w.log() and "process ended" in w.log(), "end: logged")
    w.cleanup()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rust", default=str(RUST))
    ap.add_argument("--standin", default=str(STANDIN))
    ap.add_argument("--only", default="")
    ap.add_argument("--skip-end", action="store_true", help="skip the ~70 s scenario")
    a = ap.parse_args()
    failures = []
    for name, fn in (("none", scenario_none), ("channel", scenario_channel), ("noanswer", scenario_noanswer),
                     ("other", scenario_other), ("end", scenario_end)):
        if a.only and a.only != name:
            continue
        if name == "end" and a.skip_end:
            continue
        print("=== " + name)
        fn(a, failures)
    if failures:
        print("\n%d check(s) failed" % len(failures))
        return 1
    print("\nall checks passed (synthetic stand-in; not run in game)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
