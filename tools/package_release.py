#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble the sc-offline player release folder and zip from build outputs, without CI.

    python tools/package_release.py --tag v0.8.0 --out dist \\
        --rust-exe <sc-offline.exe of launcher-rs> --classic-exe <C++ sc-offline.exe> \\
        --menu-dll <sc_offline_menu.dll> --dinput8 <dinput8.dll> \\
        --dinput8-rust-menu <dinput8-rust-menu.dll> \\
        [--artifact-root <folder with built-external/, built-inspector/, ...>]

The release job of .github/workflows/build.yml runs this script, so a package built by hand has the
same layout as the CI package. The folder `<out>/sc-offline-<tag>/` holds:

    sc-offline.exe          the Rust launcher (launcher-rs, crate scol-cli)
    sc-offline-classic.exe  the C++ launcher, kept as the fallback (can undo PC changes too)
    sc_offline_menu.dll     the Rust in-game menu (opt-in: `menu = rust` in sc-offline.ini)
    dinput8.dll             the game DLL the launcher installs by default (built with SCO_HOST_MENU_CLIENT=OFF,
                            the configuration run in game; classic menu)
    dinput8-rust-menu.dll   the same sources built with SCO_HOST_MENU_CLIENT=ON and the Rust menu hook (never run in
                            game). The Rust launcher installs it as dinput8.dll only for `menu = rust`
    sc-offline.ini, sc-offline.sh, README.md, CHANGELOG.md, LICENSE, THIRD_PARTY_NOTICES.md
    docs/, data/            tracked files (data/buildables.txt moves into the build_catalog pack)
    data/plugins/<pack>/    data packs: build_catalog, picker_catalog and locations_catalog (copies of base lists)
    data/plugins/<id>/      the plugin profile selected with --profile (tools/stage-release-plugins.py)
    manifest.json           format 1: every other file with its SHA-256 and size

Then it zips the folder, unpacks the zip into a scratch folder and checks the manifest again. With
--check-tree it also runs tools/check_package_tree.py on the folder (needs the plugin sources in
--source). Nothing is uploaded, tagged or pushed. Exit 0 on success, 1 with a reason otherwise.

Files from --source are read with `git ls-files` when --source is a git checkout (so untracked files
never ship) and by walking the folder otherwise.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE_DEFAULT = HERE.parent


def _load(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


manifest_tool = _load("release_manifest", "release-manifest.py")
stage_tool = _load("stage_release_plugins", "stage-release-plugins.py")

# Files copied to the top of the folder: (source relative to --source, name in the package).
TOP_FILES = (
    ("launcher/sc-offline.ini", "sc-offline.ini"),
    ("launcher/sc-offline.sh", "sc-offline.sh"),
    ("README.md", "README.md"),
    ("CHANGELOG.md", "CHANGELOG.md"),
    ("LICENSE", "LICENSE"),
    ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
)
# Built binaries: (argument name, name in the package).
BINARIES = (
    ("rust_exe", "sc-offline.exe"),
    ("classic_exe", "sc-offline-classic.exe"),
    ("menu_dll", "sc_offline_menu.dll"),
    ("dinput8", "dinput8.dll"),
    ("dinput8_rust_menu", "dinput8-rust-menu.dll"),
)
LEGACY_BUILDABLES = "data/buildables.txt"


class PackageError(Exception):
    pass


def tracked(source: Path, subdir: str, exclude: tuple[str, ...] = ()) -> list[str]:
    """Files under source/subdir as '/' paths relative to source: git's list, else a walk."""
    if (source / ".git").exists():
        try:
            out = subprocess.run(["git", "-C", str(source), "ls-files", "-z", "--", subdir],
                                 check=True, capture_output=True).stdout.decode("utf-8")
            files = [f for f in out.split("\0") if f]
            return sorted(f for f in files if f not in exclude and (source / f).is_file())
        except (OSError, subprocess.CalledProcessError):
            pass
    root = source / subdir
    files = []
    for dirpath, _dirs, names in os.walk(root):
        for name in names:
            rel = Path(dirpath, name).relative_to(source).as_posix()
            if rel not in exclude:
                files.append(rel)
    return sorted(files)


def copy_with_parents(source: Path, rels: list[str], dest: Path) -> None:
    for rel in rels:
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / rel, target)


def stage_data(source: Path, pkg: Path) -> None:
    """docs/ and data/ as the player gets them, with the two list packs the release adds."""
    copy_with_parents(source, tracked(source, "docs"), pkg)
    copy_with_parents(source, tracked(source, "data", exclude=(LEGACY_BUILDABLES,)), pkg)
    # Ordinary public catalog pack; originals remain user-editable sources.
    picker = pkg / "data/plugins/picker_catalog/lists"
    picker.mkdir(parents=True, exist_ok=True)
    for name in ("ships.txt", "npcs.txt"):
        shutil.copyfile(source / "data" / name, picker / name)
    # The Travel places quantum.travel needs: a required list, so it ships in its own ordinary pack.
    locations = pkg / "data/plugins/locations_catalog/lists"
    locations.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "data/locations.txt", locations / "locations.txt")
    # The legacy top-level list is player-owned, including for older launchers.
    build = pkg / "data/plugins/build_catalog"
    (build / "lists").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "data/plugins/build_catalog/plugin.ini", build / "plugin.ini")
    shutil.copyfile(source / LEGACY_BUILDABLES, build / "lists/buildables.txt")


def stage_binaries(args: argparse.Namespace, pkg: Path) -> None:
    for attr, name in BINARIES:
        src = Path(getattr(args, attr))
        if not src.is_file() or src.is_symlink():
            raise PackageError(f"{name}: {src} is not a regular file")
        if src.stat().st_size == 0:
            raise PackageError(f"{name}: {src} is empty")
        shutil.copyfile(src, pkg / name)


def zip_folder(pkg: Path, archive: Path) -> None:
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in sorted(manifest_tool.walk(pkg) + [manifest_tool.NAME]):
            info = zipfile.ZipInfo.from_file(pkg / rel, f"{pkg.name}/{rel}")
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if rel.endswith(".sh") else 0o644) << 16
            with open(pkg / rel, "rb") as f:
                z.writestr(info, f.read())


def build(args: argparse.Namespace) -> tuple[Path, Path]:
    source = Path(args.source).resolve()
    tag = args.tag
    if not manifest_tool.TAG_RE.match(tag):
        raise PackageError(f"tag {tag!r} isn't v<major>.<minor>.<patch>[.<build>][-pre]")
    for src, _ in TOP_FILES:
        if not (source / src).is_file():
            raise PackageError(f"{src} is missing from {source}")
    out = Path(args.out).resolve()
    pkg = out / f"sc-offline-{tag}"
    if pkg.exists():
        raise PackageError(f"{pkg} already exists; remove it or choose another --out")
    pkg.mkdir(parents=True)
    try:
        stage_binaries(args, pkg)
        for src, name in TOP_FILES:
            shutil.copyfile(source / src, pkg / name)
        stage_data(source, pkg)
        if not args.no_plugins:
            profile = stage_tool.load_profile(args.profile, Path(args.profile_file))
            stage_tool.stage_profile(profile, Path(args.artifact_root), pkg / "data/plugins")
        version_h = args.version_h or str(source / "src/version.h")
        commit = args.commit
        if commit is None:
            commit = ""
            if (source / ".git").exists():
                try:
                    commit = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"], check=True,
                                            capture_output=True, text=True).stdout.strip()
                except (OSError, subprocess.CalledProcessError):
                    commit = ""
        version, count = manifest_tool.write(str(pkg), tag, commit, version_h,
                                             require=manifest_tool.LAUNCHER_REQUIRED)
        print(f"manifest.json: {tag} (version {version}), {count} files")
        archive = out / f"{pkg.name}.zip"
        if not args.no_zip:
            zip_folder(pkg, archive)
            print(f"{archive.name}: {archive.stat().st_size} bytes")
            with tempfile.TemporaryDirectory(prefix="sco-roundtrip-") as tmp:
                with zipfile.ZipFile(archive) as z:
                    bad = z.testzip()
                    if bad:
                        raise PackageError(f"zip entry {bad} is damaged")
                    z.extractall(tmp)
                _, problems = manifest_tool.check(str(Path(tmp) / pkg.name))
                if problems:
                    raise PackageError("the zip does not match its manifest: " + "; ".join(problems))
            print("zip round trip: manifest matches")
        else:
            _, problems = manifest_tool.check(str(pkg))
            if problems:
                raise PackageError("the folder does not match its manifest: " + "; ".join(problems))
        if args.check_tree:
            run = subprocess.run([sys.executable, str(HERE / "check_package_tree.py"), "--tree", str(pkg),
                                  "--source", str(source)], capture_output=True, text=True)
            sys.stdout.write(run.stdout)
            if run.returncode != 0:
                sys.stderr.write(run.stderr)
                raise PackageError("tools/check_package_tree.py failed on the package folder")
        return pkg, archive
    except Exception:
        shutil.rmtree(pkg, ignore_errors=True)
        raise


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="Assemble the sc-offline release folder and zip from build outputs.")
    ap.add_argument("--tag", required=True, help="release tag, for example v0.8.0; must equal SCO_VERSION in src/version.h")
    ap.add_argument("--out", default="dist", help="folder that receives sc-offline-<tag>/ and the zip (default: dist)")
    ap.add_argument("--rust-exe", required=True, help="the Rust sc-offline.exe (launcher-rs, release build)")
    ap.add_argument("--classic-exe", required=True, help="the C++ sc-offline.exe; shipped as sc-offline-classic.exe")
    ap.add_argument("--menu-dll", required=True, help="sc_offline_menu.dll (launcher-rs, release build)")
    ap.add_argument("--dinput8", required=True,
                    help="dinput8.dll of the mod, built with SCO_HOST_MENU_CLIENT=OFF (the default install)")
    ap.add_argument("--dinput8-rust-menu", required=True,
                    help="dinput8-rust-menu.dll: the same sources built with the Rust menu hook (SCO_BUILD_RUST_MENU_HOST)")
    ap.add_argument("--artifact-root", default=".", help="folder that holds the plugin build folders named in the profile")
    ap.add_argument("--profile", default="player-offline", help="plugin profile in tools/release-plugin-profiles.json")
    ap.add_argument("--profile-file", default=str(HERE / "release-plugin-profiles.json"))
    ap.add_argument("--no-plugins", action="store_true", help="stage no plugin folders (the data packs still go in)")
    ap.add_argument("--source", default=str(SOURCE_DEFAULT), help="repository root (default: this checkout)")
    ap.add_argument("--version-h", default=None, help="version header to check the tag against (default: <source>/src/version.h)")
    ap.add_argument("--commit", default=None, help="40 hex commit for the manifest (default: git HEAD of --source, else empty)")
    ap.add_argument("--no-zip", action="store_true", help="build the folder only")
    ap.add_argument("--check-tree", action="store_true", help="run tools/check_package_tree.py on the folder")
    return ap


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.commit is not None and not re.fullmatch(r"[0-9a-f]{40}|", args.commit):
        print("package_release: --commit must be 40 lowercase hex", file=sys.stderr)
        return 1
    try:
        pkg, archive = build(args)
    except (PackageError, OSError, ValueError) as error:
        print(f"package_release: {error}", file=sys.stderr)
        return 1
    print(f"package folder: {pkg}")
    if not args.no_zip:
        print(f"package zip:    {archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
