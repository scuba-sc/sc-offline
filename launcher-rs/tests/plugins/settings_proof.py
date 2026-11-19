#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Proof that `sc-offline.exe plugins settings` keeps a plugin's [settings] values in the format the host and
the C++ launcher use, checked with a second SQLite implementation (Python's sqlite3, not the one in the exe).

Scratch folder under SCOL_SCRATCH (else the system temp folder) only; the real game and the real launcher folder are never touched. Steps:
  1. a synthetic plugin with five typed settings; `plugins settings demo` lists the defaults
  2. `plugins settings demo speed=7 mode=c fast=true` writes data\\storage\\demo.db
  3. Python opens that file: table sco_kv(key TEXT PRIMARY KEY, value BLOB) WITHOUT ROWID, keys
     `sco.settings.<name>`, values the blobs `<type>:<canonical text>`
  4. Python writes a row in the same shape (what the host or the C++ window would write); the exe reads it back
  5. a value the declaration refuses (out of range, wrong type, unknown name) changes nothing
  6. `--reset` deletes only the kept rows

Not run in game. Usage: python settings_proof.py [--rust <sc-offline.exe>]
"""
import argparse
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get("SCOL_SCRATCH") or Path(tempfile.gettempdir()) / "scol-scratch")
RUST = Path(os.environ.get("SCOL_RUST_EXE") or HERE.parents[1] / "target" / "release" / "sc-offline.exe")

PLUGIN_INI = (
    "id = demo\nname = Demo\nversion = 1.0.0\nauthor = tests\napi = 1.1\nkind = native\nentry = demo.dll\n"
    "[settings]\n"
    'speed = int default 5 min 1 max 9 label "Speed" help "how fast"\n'
    "ratio = float default 0.5 min 0 max 1\n"
    "fast = bool default false\n"
    "mode = enum(a,b,c) default b\n"
    "note = string default hi\n"
)

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rust", default=str(RUST))
    a = ap.parse_args()
    root = ROOT / "plugin-settings-proof"
    shutil.rmtree(root, ignore_errors=True)
    plug = root / "data" / "plugins" / "demo"
    plug.mkdir(parents=True)
    (plug / "plugin.ini").write_text(PLUGIN_INI)
    (plug / "demo.dll").write_bytes(b"MZ-demo")
    exe = root / "sc-offline.exe"
    shutil.copy2(a.rust, exe)
    env = dict(os.environ, SC_OFFLINE_GUI="1")

    def run(*args):
        r = subprocess.run([str(exe), "plugins", *args], cwd=str(root), env=env, stdin=subprocess.DEVNULL,
                           capture_output=True)
        return r.returncode, r.stdout.decode().replace("\r\n", "\n"), r.stderr.decode().replace("\r\n", "\n")

    code, out, err = run("settings", "demo")
    print(out, err)
    check(code == 0 and "speed = 5  (int, default)  Speed" in out, "1: defaults are listed")
    check(not (root / "data" / "storage").exists(), "1: listing writes nothing")

    code, out, err = run("settings", "demo", "speed=7", "mode=c", "fast=true")
    print(out, err)
    if "the game is running" in err:
        sys.exit("a StarCitizen.exe is running on this PC (a stand-in of another test?); try again when it is gone")
    check(code == 0 and "saved; the game reads these values at its next start" in out, "2: values saved")
    db = root / "data" / "storage" / "demo.db"
    check(db.is_file(), "2: data\\storage\\demo.db exists")

    c = sqlite3.connect(str(db))
    cols = c.execute("PRAGMA table_info(sco_kv)").fetchall()
    check([(x[1], x[2].upper(), x[5]) for x in cols] == [("key", "TEXT", 1), ("value", "BLOB", 0)],
          "3: table sco_kv(key TEXT PRIMARY KEY, value BLOB)")
    sql = c.execute("SELECT sql FROM sqlite_master WHERE name = 'sco_kv'").fetchone()[0]
    check("WITHOUT ROWID" in sql, "3: WITHOUT ROWID")
    rows = {k: (v, t) for k, v, t in c.execute("SELECT key, value, typeof(value) FROM sco_kv")}
    check(rows.get("sco.settings.speed", (None,))[0] == b"int:7", "3: speed is the blob int:7")
    check(rows.get("sco.settings.mode", (None,))[0] == b"enum:c", "3: mode is the blob enum:c")
    check(rows.get("sco.settings.fast", (None,))[0] == b"bool:true", "3: fast is the blob bool:true")
    check(all(t == "blob" for _, t in rows.values()), "3: every value is stored as a blob")
    check(set(rows) == {"sco.settings.speed", "sco.settings.mode", "sco.settings.fast"},
          "3: only changed values are stored")

    # 4: a row written the way the host writes it is read back
    c.execute("INSERT OR REPLACE INTO sco_kv(key, value) VALUES(?, ?)", ("sco.settings.note", b"string:hello there"))
    c.execute("INSERT OR REPLACE INTO sco_kv(key, value) VALUES(?, ?)", ("sco.settings.ratio", b"float:0.25"))
    c.commit()
    c.close()
    code, out, err = run("settings", "demo")
    print(out, err)
    check("note = hello there  (string, kept)" in out, "4: a string row from another writer is read")
    check("ratio = 0.25  (float, kept)" in out, "4: a float row from another writer is read")
    check("speed = 7  (int, kept)" in out and "mode = c  (enum, kept; one of a, b, c)" in out, "4: ours are read back")

    # 5: refusals change nothing
    before = db.read_bytes()
    for args, needle in ((("speed=10",), "Speed: at most 9"), (("speed=abc",), "Speed: a whole number"),
                         (("mode=z",), "pick one of the choices"), (("nope=1",), "has no setting nope"),
                         (("ratio=2",), "at most 1")):
        code, out, err = run("settings", "demo", *args)
        check(code == 1 and needle in err, "5: %s refused (%s)" % (args[0], needle))
    check(db.read_bytes() == before, "5: the database file is byte for byte the same after the refusals")

    # 6: reset deletes the kept rows only
    code, out, err = run("settings", "demo", "--reset")
    print(out, err)
    check(code == 0 and "speed back to its default" in out, "6: reset says so")
    c = sqlite3.connect(str(db))
    left = c.execute("SELECT count(*) FROM sco_kv").fetchone()[0]
    c.close()
    check(left == 0, "6: the kept rows are gone (%d left)" % left)
    code, out, err = run("settings", "demo")
    check("speed = 5  (int, default)" in out, "6: defaults are back")

    if failures:
        print("\n%d check(s) failed" % len(failures))
        return 1
    print("\nall checks passed (synthetic plugin; not run in game)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
