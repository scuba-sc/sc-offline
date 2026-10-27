#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated mining module + deliberate import workflow against a verified SDK archive.

The selected SDK must include sc_mining.h. No fallback header or private include
root is supplied. This is synthetic portable evidence, not native game acceptance.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys
sys.dont_write_bytecode = True
from check_inspector_sdk import (SDK_VERSION, SDK_SHA256, contained, explicit_preflight_pin, run, verify_extract)
ROOT = Path(__file__).resolve().parent.parent


def main(args):
    work = args.work.resolve()
    if contained(work, ROOT):
        raise AssertionError("Mining proof must be outside host checkout")
    pin = explicit_preflight_pin(args)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / "sdk", pin=pin)
    if args.candidate:
        print("Explicit unpublished development compatibility preflight; SHA-256 " + pin[1])
    if (sdk / "include/sc_mining.h").resolve() not in public:
        raise AssertionError("SDK does not publish sc_mining.h; publish the game contract before pin promotion")
    feature = ROOT / "plugins/mining"
    for name in ("plugin.ini", "import_settings.py"):
        if (feature / name).read_bytes() != (ROOT / "data/plugins/mining" / name).read_bytes():
            raise AssertionError("Mining source/release mismatch: " + name)
    source = work / "source"
    shutil.copytree(feature, source / "plugins/mining")
    (source / "tools").mkdir()
    for name in ("mining_test.cpp", "workflow_module.h"):
        shutil.copy2(ROOT / "tools" / name, source / "tools" / name)
    build = work / "build"
    configure = ["cmake", "-S", source / "plugins/mining", "-B", build,
                 "-DSCO_SDK_INCLUDE_DIR=" + str(sdk / "include"), "-DSCO_WORKFLOW_TESTS=ON"]
    if os.name == "nt":
        configure += ["-G", "Visual Studio 18 2026", "-A", "x64"]
    run(configure, work)
    run(["cmake", "--build", build, "--config", "Release", "--target", "sc-offline-mining", "mining-module-test"], work)
    print(run(["ctest", "--test-dir", build, "-C", "Release", "-R", "^mining-workflow$", "--output-on-failure"], work).stdout)
    run(["cmake", "--install", build, "--config", "Release", "--prefix", work / "stage", "--component", "mining"], work)
    folder = work / "stage/data/plugins/mining"
    modules = [p for p in folder.iterdir() if p.suffix in (".dll", ".so", ".dylib")]
    if len(modules) != 1 or {p.name for p in folder.iterdir()} != {"plugin.ini", "import_settings.py", "README.md", modules[0].name}:
        raise AssertionError("Mining installed inventory mismatch")
    for name in ("plugin.ini", "import_settings.py"):
        if (folder / name).read_bytes() != (feature / name).read_bytes():
            raise AssertionError("Mining install mismatch: " + name)
    print("mining isolated module + settings import PASS (synthetic; not run in game)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--sums", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument('--sdk-version', default=SDK_VERSION)
    parser.add_argument('--sdk-sha256', default=SDK_SHA256)
    parser.add_argument('--candidate', action='store_true', help='Explicit unpublished preflight archive')
    parser.add_argument('--archive-sha256', help='Required independent locked SHA-256 with --candidate')
    parser.add_argument('--sdk-source', help='Exact full Core source commit for candidate')
    parser.add_argument('--sdk-profile', help='Candidate SDK profile (legacy)')
    main(parser.parse_args())
