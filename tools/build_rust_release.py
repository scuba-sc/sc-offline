#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the release binaries of launcher-rs without the builder's paths inside them.

    python tools/build_rust_release.py --target-dir <scratch folder> --out <folder>

Runs `cargo build --locked --release --workspace` in launcher-rs/ and copies sc-offline.exe and
sc_offline_menu.dll to --out, printing both paths. A plain release build embeds the cargo home (panic
locations of every dependency), the toolchain sysroot and the target folder in the binaries; this script
passes --remap-path-prefix for:

    cargo home (CARGO_HOME, else ~/.cargo)  ->  /cargo
    rustc sysroot (rustc --print sysroot)   ->  /rustc-sysroot
    workspace root (launcher-rs/)           ->  .
    target folder (--target-dir)            ->  /target

The flags launcher-rs/.cargo/config.toml already sets (crt-static, /Brepro, the crates remap) stay in force:
the RUSTFLAGS environment variable would replace them, so the script removes it and appends the extra flags
with `cargo --config` to the same `target.<triple>.rustflags` key the file uses (cargo joins the arrays).
Afterwards both binaries are scanned (ASCII and UTF-16LE) with the same patterns as
tools/check_package_tree.py; any hit fails the build (exit 1). Nothing is installed, tagged or published.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE_DEFAULT = HERE.parent
OUTPUTS = ("sc-offline.exe", "sc_offline_menu.dll")


class BuildError(Exception):
    pass


def _load_tree_checker():
    spec = importlib.util.spec_from_file_location("check_package_tree", HERE / "check_package_tree.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def host_triple() -> str:
    out = subprocess.run(["rustc", "-vV"], check=True, capture_output=True, text=True).stdout
    match = re.search(r"^host:\s*(\S+)\s*$", out, re.MULTILINE)
    if not match:
        raise BuildError("rustc -vV printed no host triple")
    return match.group(1)


def rustflags_key(workspace: Path, triple: str) -> tuple[str, list[str]]:
    """The config key that holds the workspace's rustflags, and the flags in it."""
    config = workspace / ".cargo" / "config.toml"
    if not config.is_file():
        raise BuildError(f"{config} is missing; it carries the flags the release build must keep")
    targets = tomllib.loads(config.read_text(encoding="utf-8")).get("target", {})
    flags = targets.get(triple, {}).get("rustflags")
    if not flags:
        raise BuildError(f"{config} has no [target.{triple}] rustflags; refusing to build without them")
    return f"target.{triple}.rustflags", list(flags)


def cargo_home() -> Path:
    return Path(os.environ.get("CARGO_HOME") or Path.home() / ".cargo")


def sysroot(workspace: Path) -> Path:
    out = subprocess.run(["rustc", "--print", "sysroot"], cwd=workspace, check=True,
                         capture_output=True, text=True).stdout.strip()
    return Path(out)


def _forms(path: Path) -> list[str]:
    """The spellings of a folder rustc may see: native, forward slashes, and the resolved path."""
    seen: list[str] = []
    for candidate in (str(path), str(path).replace("\\", "/"), str(path.resolve()),
                      str(path.resolve()).replace("\\", "/")):
        if candidate not in seen:
            seen.append(candidate)
    return seen


def remap_flags(workspace: Path, target_dir: Path) -> list[str]:
    pairs = ((cargo_home(), "/cargo"), (sysroot(workspace), "/rustc-sysroot"),
             (workspace, "."), (target_dir, "/target"))
    flags: list[str] = []
    for source, replacement in pairs:
        for form in _forms(source):
            flags.append(f"--remap-path-prefix={form}={replacement}")
    return flags


def toml_array(items: list[str]) -> str:
    # JSON strings are valid TOML basic strings (backslashes are escaped).
    return "[" + ", ".join(json.dumps(item) for item in items) + "]"


def cargo_command(workspace: Path, target_dir: Path) -> list[str]:
    key, _ = rustflags_key(workspace, host_triple())
    return ["cargo", "build", "--locked", "--release", "--workspace",
            "--config", f"{key}={toml_array(remap_flags(workspace, target_dir))}"]


def build(args: argparse.Namespace) -> list[Path]:
    source = Path(args.source).resolve()
    workspace = source / "launcher-rs"
    if not (workspace / "Cargo.toml").is_file():
        raise BuildError(f"{workspace}/Cargo.toml is missing")
    target_dir = Path(os.path.abspath(args.target_dir))  # as given: remapped in both spellings
    out = Path(os.path.abspath(args.out))
    command = cargo_command(workspace, target_dir)
    env = dict(os.environ, CARGO_TARGET_DIR=str(target_dir))
    for name in ("RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS"):
        if env.pop(name, None) is not None:
            print(f"build_rust_release: ignoring {name} from the environment (it would replace the "
                  f"flags of launcher-rs/.cargo/config.toml)", file=sys.stderr)
    if args.dry_run:
        print(" ".join(command))
        return []
    target_dir.mkdir(parents=True, exist_ok=True)
    print("+ cargo build --locked --release --workspace (path remapping via --config)", flush=True)
    if subprocess.run(command, cwd=workspace, env=env).returncode != 0:
        raise BuildError("cargo build failed")
    built = []
    for name in OUTPUTS:
        src = target_dir / "release" / name
        if not src.is_file():
            raise BuildError(f"{src} was not produced")
        built.append(src)
    checker = _load_tree_checker()
    problems = []
    for src in built:
        problems += checker.build_path_problems(src.name, src.read_bytes())
    if problems:
        raise BuildError("build-machine paths remain in the binaries: " + "; ".join(problems))
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for src in built:
        dest = out / src.name
        shutil.copyfile(src, dest)
        results.append(dest)
    return results


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="Build launcher-rs release binaries with build-machine paths remapped.")
    ap.add_argument("--target-dir", required=True, help="cargo target folder (CARGO_TARGET_DIR); remapped to /target")
    ap.add_argument("--out", required=True, help="folder that receives sc-offline.exe and sc_offline_menu.dll")
    ap.add_argument("--source", default=str(SOURCE_DEFAULT), help="repository root (default: this checkout)")
    ap.add_argument("--dry-run", action="store_true", help="print the cargo command and build nothing")
    return ap


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        paths = build(args)
    except (BuildError, OSError, subprocess.CalledProcessError) as error:
        print(f"build_rust_release: {error}", file=sys.stderr)
        return 1
    for path in paths:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
