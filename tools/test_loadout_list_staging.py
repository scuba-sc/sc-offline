#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual Loadout component install in fresh retained directories."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import stat
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = {
    "items.txt": b"# fixture items\r\n[head]\r\nFixtureHelmet\r\n",
    "outfits.txt": b"# fixture outfits\n[Fixture]\nhead FixtureHelmet\n",
    "buildables.txt": b"# fixture builds\n[sq42]\nFixtureObject\n",
}


def run(*args):
    print("+", *map(str, args), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--sdk-include", required=True)
    parser.add_argument("--generator", default=None)
    parser.add_argument("--architecture", default=None)
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix="loadout-list-staging-"))
    print(f"Retained staging evidence: {root}", flush=True)
    for case_name, opt_in, existing in (("default", False, False),
                                         ("opt-in", True, False),
                                         ("existing-default", False, True)):
        case = root / case_name
        source, build, stage = (case / name for name in ("source", "build", "stage"))
        source.mkdir(parents=True)
        names = list(FIXTURES) if opt_in else ["items.txt", "outfits.txt"]
        for name, contents in FIXTURES.items():
            (source / name).write_bytes(contents)
        command = [args.cmake, "-S", ROOT / "plugins/loadout", "-B", build,
                   f"-DSCO_SDK_INCLUDE_DIR={Path(args.sdk_include).resolve()}",
                   f"-DSCO_LOADOUT_BASE_LIST_DIR={source}"]
        if opt_in:
            command.append("-DSCO_LOADOUT_STAGE_BUILDABLES=ON")
        if args.generator:
            command += ["-G", args.generator]
        if args.architecture:
            command += ["-A", args.architecture]
        run(*command)
        run(args.cmake, "--build", build, "--config", "Release", "--target", "sc-offline-loadout")
        if existing:
            existing_file = stage / "data/plugins/loadout_lists/lists/buildables.txt"
            existing_file.parent.mkdir(parents=True)
            existing_file.write_bytes(b"# existing user list\n[kept]\nExistingFixture\n")
        run(args.cmake, "--install", build, "--config", "Release", "--prefix", stage,
            "--component", "loadout")
        pack = stage / "data/plugins/loadout_lists"
        actual = {p.relative_to(pack).as_posix(): p.read_bytes()
                  for p in pack.rglob("*") if p.is_file()}
        expected = {f"lists/{name}": FIXTURES[name] for name in names}
        if existing:
            expected["lists/buildables.txt"] = b"# existing user list\n[kept]\nExistingFixture\n"
        expected["plugin.ini"] = (ROOT / "plugins/loadout/lists-plugin.ini").read_bytes()
        if actual != expected:
            raise AssertionError(f"{case.name} installed list pack differs: {actual.keys()} vs {expected.keys()}")
        print(f"{case.name}: list pack files and bytes match", flush=True)
        # An identical reinstall is admitted.
        run(args.cmake, "--install", build, "--config", "Release", "--prefix", stage,
            "--component", "loadout")
        if opt_in:
            # Each conflict starts with all pack files present, so a refusal
            # must leave every file untouched, including files checked earlier.
            for conflict in ("lists/items.txt", "lists/outfits.txt", "plugin.ini", "lists/buildables.txt"):
                conflict_stage = case / ("conflict-" + Path(conflict).name)
                conflict_pack = conflict_stage / "data/plugins/loadout_lists"
                before = dict(expected)
                before[conflict] = b"user-owned conflicting bytes\n"
                for relative, contents in before.items():
                    destination = conflict_pack / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(contents)
                result = subprocess.run([str(args.cmake), "--install", str(build), "--config", "Release",
                                         "--prefix", str(conflict_stage), "--component", "loadout"],
                                        capture_output=True, text=True)
                if result.returncode == 0 or "Loadout list pack file differs" not in result.stderr:
                    raise AssertionError(f"{conflict}: install did not refuse: {result.stdout} {result.stderr}")
                after = {p.relative_to(conflict_pack).as_posix(): p.read_bytes()
                         for p in conflict_pack.rglob("*") if p.is_file()}
                if after != before:
                    raise AssertionError(f"{conflict}: refusal changed pack files")
                print(f"{conflict}: refused without changing pack", flush=True)
            destdir = case / "destdir"
            dest_pack = destdir / "virtual/data/plugins/loadout_lists"
            dest_item = dest_pack / "lists/items.txt"
            dest_item.parent.mkdir(parents=True)
            dest_item.write_bytes(b"DESTDIR user list\n")
            environment = os.environ.copy()
            environment["DESTDIR"] = destdir.as_posix()
            result = subprocess.run([str(args.cmake), "--install", str(build), "--config", "Release",
                                     "--prefix", "/virtual", "--component", "loadout"],
                                    env=environment, capture_output=True, text=True)
            if result.returncode == 0 or "Loadout list pack file differs" not in result.stderr:
                raise AssertionError(f"DESTDIR conflict did not refuse: {result.stdout} {result.stderr}")
            if dest_item.read_bytes() != b"DESTDIR user list\n" or list(dest_pack.rglob("*.ini")):
                raise AssertionError("DESTDIR conflict changed pack")
            print("DESTDIR: refused without changing pack", flush=True)
    # Inject races at the publication boundary using only fresh synthetic files.
    spec = importlib.util.spec_from_file_location("stage_loadout_lists", ROOT / "tools/stage_loadout_lists.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    real_link = os.link
    for mode in ("race", "failure"):
        pack = root / ("controlled-" + mode) / "pack"
        source = root / "opt-in/source"
        files = [(f"lists/{name}", source / name) for name in ("items.txt", "outfits.txt")]
        def controlled_link(src, dst):
            if Path(dst).name == "outfits.txt":
                if mode == "race":
                    Path(dst).write_bytes(b"concurrent fixture\n")
                    return real_link(src, dst)
                raise OSError("synthetic hard-link failure")
            return real_link(src, dst)
        with mock.patch.object(helper.os, "link", side_effect=controlled_link):
            try:
                helper.stage(pack, files)
            except (OSError, RuntimeError):
                pass
            else:
                raise AssertionError(f"{mode} did not refuse")
        assert (pack / "lists/items.txt").read_bytes() == FIXTURES["items.txt"]
        if mode == "race":
            assert (pack / "lists/outfits.txt").read_bytes() == b"concurrent fixture\n"
        else:
            assert not (pack / "lists/outfits.txt").exists()
        assert not list(pack.rglob(".loadout-stage-*"))
        print(f"{mode}: earlier published file retained; no overwrite", flush=True)
    if os.name == "posix":
        # The helper runs in its own process so setting umask cannot affect the test runner.
        permission_pack = root / "permissions"
        preexisting = permission_pack / "lists/items.txt"
        preexisting.parent.mkdir(parents=True)
        preexisting.write_bytes(FIXTURES["items.txt"])
        preexisting.chmod(0o600)
        script = ("import importlib.util, os; from pathlib import Path; "
                  "s=importlib.util.spec_from_file_location('stage_helper', "
                  + repr(str(ROOT / "tools/stage_loadout_lists.py")) + "); "
                  "m=importlib.util.module_from_spec(s); s.loader.exec_module(m); "
                  "os.umask(0o027); m.stage(Path(" + repr(str(permission_pack)) + "), "
                  + repr([("lists/items.txt", str(root / "opt-in/source/items.txt")),
                          ("lists/outfits.txt", str(root / "opt-in/source/outfits.txt"))]) + ")")
        subprocess.run([sys.executable, "-c", script], check=True)
        assert stat.S_IMODE(preexisting.stat().st_mode) == 0o600
        assert stat.S_IMODE((permission_pack / "lists/outfits.txt").stat().st_mode) == 0o640
        print("POSIX modes: umask applied; existing permissions unchanged", flush=True)
    print("All CMake install cases passed; directories retained.")


if __name__ == "__main__":
    main()
