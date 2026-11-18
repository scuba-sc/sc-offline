#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Runs the real Rust `sc-offline.exe play` (real helper process, real named events, real process start)
against a synthetic world under D:\\t with a stand-in game: the test exe `scol-play-standin.exe` copied to
StarCitizen.exe, which sleeps and exits. The real game is never started.

Nothing on this PC is changed: the three PC-change switches (block_network, eac_hosts, eac_rename) are off
in the scratch sc-offline.ini, the game folder is under D:\\t (writable, so no administrator prompt) and
ProgramData, ProgramFiles(x86), APPDATA and SystemRoot point into the scratch folder.

Checks: exit 0; while the stand-in runs, the mod DLL and the marker are in Bin64; after it exits both are
gone, the helper's lines are in data\\launcher.log and the log of the previous run is kept as .1.
Not run in game. Usage: python real_standin.py [--rust <sc-offline.exe>] [--standin <exe>]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get("SCOL_SCRATCH", "D:/t"))
RUST = Path("D:/t/cargo-play/debug/sc-offline.exe")
STANDIN = Path("D:/t/cargo-play/debug/scol-play-standin.exe")
OURS_DLL = b"MZ\x90\x00 padding sc-offline v0.7.8.3\x00 more bytes"


def win(p):
    return str(p).replace("/", "\\")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rust", default=str(RUST))
    ap.add_argument("--standin", default=str(STANDIN))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = ROOT / "play-real-standin"
    shutil.rmtree(root, ignore_errors=True)
    here = root / "here"
    sc = root / "library" / "StarCitizen"
    bin64 = sc / "LIVE" / "Bin64"
    for d in (here / "data", bin64, root / "pd", root / "pf86", root / "appdata",
              root / "sysroot" / "System32" / "drivers" / "etc"):
        d.mkdir(parents=True, exist_ok=True)
    shutil.copy2(a.rust, here / "sc-offline.exe")
    shutil.copy2(a.standin, bin64 / "StarCitizen.exe")
    (here / "dinput8.dll").write_bytes(OURS_DLL)
    (here / "sc-offline.ini").write_text(
        f"game = {win(sc)}\ncheck_updates = off\nclean_logs = never\ncrash_reports = off\n"
        "block_network = off\neac_hosts = off\neac_rename = off\n")
    env = dict(os.environ)
    env.update({
        "ProgramData": win(root / "pd"), "ProgramFiles(x86)": win(root / "pf86"),
        "APPDATA": win(root / "appdata"), "SystemRoot": win(root / "sysroot"),
        "SC_OFFLINE_GUI": "1", "SCOL_STANDIN_MS": "2500",
    })
    # SystemRoot must stay real for the child to load system DLLs; the scratch one is only read by the
    # launcher's hosts check, so give the launcher the scratch one through a copy of the environment
    env["SystemRoot"] = os.environ.get("SystemRoot", "C:\\Windows")
    failures = []
    for run in (1, 2):
        p = subprocess.Popen([str(here / "sc-offline.exe"), "play"], cwd=str(root), env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        seen_in = False
        deadline = time.time() + 30
        while p.poll() is None and time.time() < deadline:
            if (bin64 / "dinput8.dll").exists() and (bin64 / "sc-offline.installed").exists():
                seen_in = True
            time.sleep(0.05)
        out = p.communicate(timeout=60)[0].decode("utf-8", "replace").replace("\r\n", "\n")
        print(f"--- run {run}: exit {p.returncode}")
        print(out)
        if p.returncode != 0:
            failures.append(f"run {run}: exit {p.returncode}")
        if not seen_in:
            failures.append(f"run {run}: the mod was never seen in Bin64 while the stand-in ran")
        if (bin64 / "dinput8.dll").exists() or (bin64 / "sc-offline.installed").exists():
            failures.append(f"run {run}: the mod is still in Bin64")
        for needle in ("Mod:      copied into Bin64", "Playing. When the game closes",
                       "Mod:      removed from Bin64; PC changes undone. Safe to go online."):
            if needle not in out:
                failures.append(f"run {run}: missing line: {needle}")
        log = (here / "data" / "launcher.log").read_text(errors="replace")
        if " helper play" not in log or "[+] copy " not in log:
            failures.append(f"run {run}: the helper's lines are not in launcher.log")
    if not (here / "data" / "launcher.log.1").exists():
        failures.append("the first run's log was not kept as launcher.log.1")
    if not a.keep:
        shutil.rmtree(root, ignore_errors=True)
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        return 1
    print("ok: real helper, real events, stand-in game; the mod was in while it ran and gone after")
    return 0


if __name__ == "__main__":
    sys.exit(main())
