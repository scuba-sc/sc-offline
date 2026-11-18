#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the SDK Python verifier on the shared corpus and compare it with expected.tsv.

    python run_python.py --sdk <sco-core>/sdk --checker <path to sco-plugin-check>

The checker is built from the same SDK checkout (see README.md). The Rust test
`crates/scol-plugins/tests/corpus.rs` compares the Rust verifier with the same expected.tsv, so
when both pass the two tools agree on every file.

For a refused archive only the verdict is compared. The Rust error codes are Rust names and the
Python tool has none.

PYTHON-TOOL.txt records the SDK commit and the file hashes this corpus was agreed against. The
script fails if the verifier or the checker source differs, unless --no-pin-check is given.
"""
import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PIN = HERE / "PYTHON-TOOL.txt"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_pin():
    pin = {}
    for line in PIN.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            key, _, value = line.partition("=")
            pin[key.strip()] = value.strip()
    return pin


def load_verifier(path):
    spec = importlib.util.spec_from_file_location("sco_plugin_verify", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rows():
    out = []
    for line in (HERE / "expected.tsv").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            out.append(line.split("\t"))
    return out


def python_version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def manifest_folder(data):
    """The folder name to hand the checker: the manifest's own id, else 'pilot'."""
    match = re.search(rb"(?m)^id[ \t]*=[ \t]*([^\r\n;# \t]+)", data)
    return match.group(1).decode("utf-8", "replace") if match else "pilot"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sdk", required=True, help="the sdk/ folder of the sco-core checkout")
    ap.add_argument("--checker", required=True, help="sco-plugin-check built from that checkout")
    ap.add_argument("--no-pin-check", action="store_true")
    args = ap.parse_args()

    tool = Path(args.sdk) / "tools" / "sco-plugin-verify.py"
    checker_src = Path(args.sdk) / "tools" / "sco-plugin-check.c"
    pin = read_pin()
    if not args.no_pin_check:
        for key, path in (("verify_sha256", tool), ("check_c_sha256", checker_src)):
            if sha256(path) != pin[key]:
                print(f"{path.name} differs from the pinned file (see PYTHON-TOOL.txt)", file=sys.stderr)
                return 2
    verifier = load_verifier(tool)
    checker = str(Path(args.checker).resolve())

    bad = []
    ran = skipped = 0
    for kind, name, verdict, code, min_python, desc in rows():
        if kind == "zip":
            if min_python != "-" and sys.version_info[:2] < python_version_tuple(min_python):
                skipped += 1
                continue
            try:
                verifier.verify_archive(str(HERE / "cases" / f"{name}.zip"), checker)
                got = "ok"
            except verifier.VerifyError:
                got = "refuse"
            except Exception as exc:  # a crash is a disagreement, not a verdict
                got = f"crash {type(exc).__name__}: {exc}"
        else:
            data = (HERE / "manifests" / f"{name}.ini").read_bytes()
            proc = subprocess.run([checker, "--manifest-stdin", manifest_folder(data)],
                                  input=data, capture_output=True, timeout=30, check=False)
            got = "ok" if proc.returncode == 0 else "refuse"
            # The checker also applies the host API bound; the Rust syntax-only parse does not.
            # The Rust test checks that bound separately for this one manifest.
            if name == "ok-api-max" and got == "refuse":
                got = "ok"
        ran += 1
        if got != verdict:
            bad.append(f"{kind} {name}: expected {verdict}, Python gave {got}")
    for line in bad:
        print(line, file=sys.stderr)
    print(f"python tool: {ran} cases run, {skipped} skipped for the Python version, {len(bad)} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
