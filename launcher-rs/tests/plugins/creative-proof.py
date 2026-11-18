#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Proof run of the whole plugin chain with the REAL creative plugin, in a scratch install.

    python creative-proof.py --creative-dir <folder with creative.dll and plugin.ini>
                             --index-tools <sc-plugins checkout> --scratch <empty or absent folder>
                             --scol-pm <scol-pm built with --features test-trust>

Chain: build a zip with the SDK layout from the real files, build and sign an index with the
sc-plugins tools (TEST key), then run `catalogue list`, `catalogue install`, `enable`, `list`,
`catalogue list` again, a replayed older index, `remove`, with a file-tree diff after each step
that proves only data/plugins/creative/ and data/launcher/ changed. The creative files are only
read. Not run in game; creative.dll is not loaded or run by any of this.
"""
import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import time
import zipfile

os.environ["PYTHONDONTWRITEBYTECODE"] = "1"


def tree(root):
    out = {}
    for p in sorted(root.rglob("*")):
        rel = p.relative_to(root).as_posix()
        out[rel] = "dir" if p.is_dir() else hashlib.sha256(p.read_bytes()).hexdigest()
    return out


def diff(a, b):
    lines = []
    for k in sorted(set(a) | set(b)):
        if k not in a:
            lines.append(f"  + {k}")
        elif k not in b:
            lines.append(f"  - {k}")
        elif a[k] != b[k]:
            lines.append(f"  ~ {k}")
    return lines


def utc(t):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--creative-dir", required=True)
    ap.add_argument("--index-tools", required=True)
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--scol-pm", required=True)
    ap.add_argument("--transcript")
    a = ap.parse_args()
    scratch = pathlib.Path(a.scratch).resolve()
    tools = pathlib.Path(a.index_tools).resolve()
    creative = pathlib.Path(a.creative_dir).resolve()
    if scratch.exists():
        if scratch.name.startswith("pm-proof"):
            shutil.rmtree(scratch)
        else:
            sys.exit("refusing to reuse a folder not named pm-proof*")
    root = scratch / "launcher"
    cat = scratch / "catalogue"
    work = scratch / "work"
    for d in (root, cat, work):
        d.mkdir(parents=True)
    log = []

    def say(s=""):
        s = s.replace(str(scratch), "<scratch>").replace(str(scratch).replace("\\", "/"), "<scratch>")
        print(s)
        log.append(s)

    # A launcher folder with things that must not change.
    (root / "data/storage").mkdir(parents=True)
    (root / "data/storage/other.db").write_bytes(b"someone else's settings")
    (root / "data/plugins/hand_copied").mkdir(parents=True)
    (root / "data/plugins/hand_copied/plugin.ini").write_text(
        "id = hand_copied\nname = Hand copied\nversion = 1.0.0\napi = 1.1\nkind = data\n"
    )
    (root / "sc-offline.ini").write_text("[sc_offline]\ncheck_updates = off\n")

    # 1. The zip, SDK layout: one folder named by the id, plugin.ini and the entry in it.
    ini = (creative / "plugin.ini").read_bytes()
    dll = (creative / "creative.dll").read_bytes()
    zpath = cat / "creative-1.1.0.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in (("creative/creative.dll", dll), ("creative/plugin.ini", ini)):
            zi = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o100644 << 16
            z.writestr(zi, data)
    zbytes = zpath.read_bytes()
    say(f"zip: creative-1.1.0.zip, {len(zbytes)} bytes, sha256 {hashlib.sha256(zbytes).hexdigest()}")

    # 2. Index with the sc-plugins tools, signed with the TEST key.
    import json

    builds = work / "builds.json"
    builds.write_text(
        json.dumps(
            {
                "schema": 1,
                "builds": {
                    "creative": {
                        "version": "1.1.0",
                        "api": "1.1",
                        "zip_url": "https://github.com/scuba-sc/sc-creative/releases/download/v1.1.0/creative-1.1.0.zip",
                        "zip_sha256": hashlib.sha256(zbytes).hexdigest(),
                        "zip_size": len(zbytes),
                        "evidence": "Proof run: files taken from an existing build, not built or run here, not run in game.",
                    }
                },
            }
        )
    )
    now = int(time.time())

    def build_and_sign(sequence, name):
        out = cat / "index.json"
        r = subprocess.run(
            [
                sys.executable,
                str(tools / "tools/build_index.py"),
                "--builds", str(builds),
                "--sequence", str(sequence),
                "--issued", utc(now - 3600),
                "--expires", utc(now + 30 * 86400),
                "--out", str(out),
            ],
            capture_output=True, text=True, cwd=tools,
        )
        if r.returncode:
            sys.exit(f"build_index failed:\n{r.stdout}{r.stderr}")
        key = tools / "tests/testkeys/TEST-ONLY-private-seed.hex"
        keycopy = work / "test-seed.hex"
        shutil.copyfile(key, keycopy)
        r = subprocess.run(
            [sys.executable, str(tools / "tools/sign_index.py"), "sign", "--test", "--index", str(out), "--key-file", str(keycopy)],
            capture_output=True, text=True, cwd=tools,
        )
        if r.returncode:
            sys.exit(f"sign_index failed:\n{r.stdout}{r.stderr}")
        say(f"index '{name}': sequence {sequence}, {out.stat().st_size} bytes, signed with the TEST key")

    pub = tools / "tests/testkeys/TEST-ONLY-public.hex"

    def pm(*args, expect=0):
        cmd = [a.scol_pm, "--root", str(root), "--test-trust-key", str(pub), *args]
        say(f"$ plugins {' '.join(args)}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            say("  " + line)
        say(f"  exit {r.returncode}")
        if r.returncode != expect:
            sys.exit(f"FAILED: expected exit {expect}")
        return r

    src = ["--source", str(cat)]
    build_and_sign(10, "first")
    snaps = [tree(root)]

    def step(title, allowed):
        now_tree = tree(root)
        d = diff(snaps[-1], now_tree)
        say(f"  file-tree diff after '{title}':" if d else f"  file-tree diff after '{title}': none")
        for line in d:
            say(line)
            rel = line[4:]
            ok = any(rel == p or rel.startswith(p + "/") or p.startswith(rel + "/") for p in allowed)
            if not ok:
                sys.exit(f"FAILED: unexpected change {rel}")
        snaps.append(now_tree)
        say()

    pm("catalogue", "list", *src)
    step("catalogue list", ["data/launcher/catalogue-state.json"])
    pm("catalogue", "install", "creative", *src)
    step("catalogue install", ["data/plugins/creative", "data/launcher"])
    assert (root / "data/plugins/creative/disabled").is_file(), "it must land disabled"
    assert (root / "data/plugins/creative/creative.dll").read_bytes() == dll
    assert (root / "data/plugins/creative/plugin.ini").read_bytes() == ini
    say("  checked: it landed disabled, creative.dll and plugin.ini are byte for byte the originals")
    say()
    pm("list")
    step("list", [])
    pm("enable", "creative")
    step("enable", ["data/plugins/creative/disabled", "data/launcher/installed.json"])
    pm("list")
    step("list", [])
    pm("catalogue", "list", *src)
    step("catalogue list (same index again)", [])

    # Replay: an older, validly signed index, in a new process.
    build_and_sign(9, "older")
    pm("catalogue", "list", *src, expect=1)
    step("replayed older index", [])
    build_and_sign(10, "same sequence, different bytes")
    shutil.copyfile(cat / "index.json", work / "same-seq.json")
    # A different byte stream with the same sequence: change the evidence text and re-sign.
    b = json.loads(builds.read_text())
    b["builds"]["creative"]["evidence"] += " (changed)"
    builds.write_text(json.dumps(b))
    build_and_sign(10, "same sequence, different bytes")
    pm("catalogue", "list", *src, expect=1)
    step("same sequence, different bytes", [])

    pm("remove", "creative")
    step("remove", ["data/plugins/creative", "data/launcher/installed.json"])
    assert not (root / "data/plugins/creative").exists()

    # The whole run, start to end.
    total = diff(snaps[0], snaps[-1])
    say("file-tree diff of the whole run (before the first command, after remove):")
    for line in total:
        say(line)
    allowed = ["data/launcher"]
    for line in total:
        rel = line[4:]
        if not any(rel == p or rel.startswith(p + "/") or p.startswith(rel + "/") for p in allowed):
            sys.exit(f"FAILED: unexpected change {rel}")
    for must in ("sc-offline.ini", "data/storage/other.db", "data/plugins/hand_copied/plugin.ini"):
        assert snaps[0][must] == snaps[-1][must], must
    say("PASS: catalogue list, install, enable, list, remove; only data/plugins/creative/ and data/launcher/ changed;")
    say("      the pinned sequence survived across runs and the replays were refused. Not run in game.")
    if a.transcript:
        pathlib.Path(a.transcript).write_text("\n".join(log) + "\n", encoding="utf-8")


main()
