#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compose an unpublished Loadout candidate in a private temporary tree.

The tool consumes caller-built artifacts and never builds or substitutes
modules. It writes only below a newly created private child of an explicit
caller-owned staging parent. The staging parent must not be an install path.
The staging parent and its ancestors, and supplied source paths, must not be
concurrently modified by another process. Python's portable path wrappers
cannot defend against a hostile Windows parent-directory replacement.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import stat
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PROFILE_NAME = "player-offline-loadout-candidate"
PROFILE_FILE = Path(__file__).with_name("release-plugin-profiles.json")
PROFILE_STAGE_FILE = Path(__file__).with_name("stage-release-plugins.py")
LIST_STAGE_FILE = Path(__file__).with_name("stage_loadout_lists.py")
DEFAULT_LIST_MANIFEST = ROOT / "plugins" / "loadout" / "lists-plugin.ini"
EXPECTED_PLUGIN_COUNT = 12
EXPECTED_LOADOUT_ARTIFACT = "built-loadout/loadout"
EXPECTED_LOADOUT_MODULE = "loadout.dll"
REPARSE_POINT = 0x400


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load staging helper: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _is_redirect(path: Path) -> bool:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & REPARSE_POINT)


def _reject_redirects(path: Path, *, require_exists: bool = False) -> None:
    """Reject symlink/reparse components; missing leaves are allowed by default."""
    for part in (path, *path.parents):
        if _is_redirect(part):
            raise ValueError(f"refusing redirected or ambiguous path: {part}")
        if require_exists and not part.exists():
            raise ValueError(f"path component is missing: {part}")


def _require_absolute(path: Path, label: str) -> Path:
    if not path.is_absolute():
        raise ValueError(f"{label} must be an absolute path")
    if any(part == ".." for part in str(path).replace("\\", "/").split("/")):
        raise ValueError(f"{label} must not contain parent traversal")
    return path


def _overlaps(first: Path, second: Path) -> bool:
    return first == second or first in second.parents or second in first.parents


def _resolve_directory(path: Path, label: str) -> Path:
    path = _require_absolute(path, label)
    _reject_redirects(path, require_exists=True)
    try:
        resolved = path.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise ValueError(f"cannot resolve {label}: {exc}") from exc
    if not resolved.is_dir():
        raise ValueError(f"{label} is missing or is not a directory: {path}")
    return resolved


def _snapshot_regular_file(path: Path, label: str) -> bytes:
    """Read one regular file and reject redirects or observable mid-read changes.

    The pre/post identity check is useful for accidental changes. It is not a
    substitute for handle-bound traversal against a hostile concurrent writer.
    """
    _reject_redirects(path)
    try:
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode):
            raise ValueError(f"{label} is not a regular file: {path}")
        with path.open("rb") as source:
            data = source.read()
        after = path.lstat()
    except FileNotFoundError as exc:
        raise ValueError(f"{label} is missing or is not a regular file: {path}") from exc
    except OSError as exc:
        raise ValueError(f"cannot snapshot {label}: {path}: {exc}") from exc
    identity_before = (
        before.st_dev, before.st_ino, before.st_size,
        getattr(before, "st_mtime_ns", None), getattr(before, "st_ctime_ns", None),
    )
    identity_after = (
        after.st_dev, after.st_ino, after.st_size,
        getattr(after, "st_mtime_ns", None), getattr(after, "st_ctime_ns", None),
    )
    if identity_before != identity_after or len(data) != after.st_size:
        raise ValueError(f"{label} changed while it was being snapshotted: {path}")
    return data


def _optional_lstat(path: Path):
    _reject_redirects(path)
    try:
        return path.lstat()
    except FileNotFoundError:
        return None


def _snapshot_tree(root: Path) -> tuple[set[Path], dict[Path, bytes]]:
    root = _resolve_directory(root, "tracked product plugin defaults")
    directories: set[Path] = set()
    files: dict[Path, bytes] = {}

    def walk_error(error: OSError) -> None:
        raise error

    for current, names, filenames in os.walk(root, topdown=True, followlinks=False, onerror=walk_error):
        current_path = Path(current)
        checked_dirs: list[str] = []
        for name in names:
            path = current_path / name
            _reject_redirects(path, require_exists=True)
            try:
                info = path.lstat()
            except OSError as exc:
                raise ValueError(f"cannot inspect tracked default directory: {path}: {exc}") from exc
            if not stat.S_ISDIR(info.st_mode):
                raise ValueError(f"tracked product defaults contain a non-directory: {path}")
            directories.add(path.relative_to(root))
            checked_dirs.append(name)
        names[:] = checked_dirs
        for name in filenames:
            path = current_path / name
            relative = path.relative_to(root)
            files[relative] = _snapshot_regular_file(path, f"tracked default {relative.as_posix()}")
    return directories, files


def _candidate_profile(stage_plugins: Any) -> dict[str, Any]:
    profile = stage_plugins.load_profile(PROFILE_NAME, PROFILE_FILE)
    entries = profile["plugins"]
    if len(entries) != EXPECTED_PLUGIN_COUNT:
        raise ValueError(f"{PROFILE_NAME} must select exactly {EXPECTED_PLUGIN_COUNT} plugins")
    loadout = [entry for entry in entries if entry["id"] == "loadout"]
    if len(loadout) != 1:
        raise ValueError(f"{PROFILE_NAME} must select Loadout exactly once")
    entry = loadout[0]
    if (entry["artifact_dir"] != EXPECTED_LOADOUT_ARTIFACT
            or entry["module"] != EXPECTED_LOADOUT_MODULE
            or entry["disabled"]):
        raise ValueError("candidate Loadout must use built-loadout/loadout/loadout.dll enabled")
    return profile


def _snapshot_artifacts(
    profile: dict[str, Any], artifact_root: Path, stage_plugins: Any,
) -> tuple[dict[str, dict[str, bytes]], dict[str, str]]:
    snapshots: dict[str, dict[str, bytes]] = {}
    module_hashes: dict[str, str] = {}
    for entry in profile["plugins"]:
        plugin_id = entry["id"]
        relative = stage_plugins._safe_relative(entry["artifact_dir"], f"{plugin_id}.artifact_dir")
        source_dir = artifact_root / relative
        _reject_redirects(source_dir, require_exists=True)
        try:
            resolved_source = source_dir.resolve(strict=True)
            resolved_source.relative_to(artifact_root)
            info = source_dir.lstat()
        except (OSError, RuntimeError, ValueError) as exc:
            raise ValueError(f"{plugin_id} artifact path escapes or cannot be resolved under the artifact root") from exc
        if not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"{plugin_id} artifact directory is not a regular directory: {source_dir}")

        marker_path = source_dir / "disabled"
        marker_info = _optional_lstat(marker_path)
        if (marker_info is not None) != entry["disabled"]:
            expected = "present" if entry["disabled"] else "absent"
            raise ValueError(f"{plugin_id} disabled marker must be {expected}")

        names = ["plugin.ini", entry["module"], *entry.get("extra_files", [])]
        if entry["disabled"]:
            names.append("disabled")
        plugin_files = {
            name: _snapshot_regular_file(source_dir / name, f"{plugin_id}/{name}")
            for name in names
        }
        try:
            manifest = plugin_files["plugin.ini"].decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            raise ValueError(f"{plugin_id}/plugin.ini is not UTF-8") from exc
        stage_plugins._manifest_fields(manifest, plugin_id)
        if not plugin_files[entry["module"]]:
            raise ValueError(f"{plugin_id} module is empty: {source_dir / entry['module']}")
        snapshots[plugin_id] = plugin_files
        module_hashes[plugin_id] = hashlib.sha256(plugin_files[entry["module"]]).hexdigest()
    return snapshots, module_hashes


def _compose_plugin_tree(
    profile: dict[str, Any],
    artifact_files: dict[str, dict[str, bytes]],
    default_directories: set[Path],
    default_files: dict[Path, bytes],
) -> tuple[set[Path], dict[Path, bytes]]:
    directories = {Path("data") / "plugins" / relative for relative in default_directories}
    files = {Path("data") / "plugins" / relative: data for relative, data in default_files.items()}
    plugin_root = Path("data") / "plugins"
    directories.add(Path("data"))
    directories.add(plugin_root)

    for entry in profile["plugins"]:
        plugin_id = entry["id"]
        plugin_directory = plugin_root / plugin_id
        if plugin_directory in files:
            raise ValueError(f"{plugin_id} destination conflicts with non-directory product data")
        directories.add(plugin_directory)
        marker_target = plugin_directory / "disabled"
        marker_exists = marker_target in files
        if marker_target in directories:
            raise ValueError(f"{plugin_id} destination disabled marker is not a regular file")
        if marker_exists != entry["disabled"]:
            expected = "present" if entry["disabled"] else "absent"
            raise ValueError(f"{plugin_id} destination disabled marker must be {expected}")

        names = ["plugin.ini", entry["module"], *entry.get("extra_files", [])]
        if entry["disabled"]:
            names.append("disabled")
        for filename in names:
            target = plugin_directory / filename
            data = artifact_files[plugin_id][filename]
            if target in directories:
                raise ValueError(f"{plugin_id}/{filename} conflicts with a product data directory")
            existing = files.get(target)
            if existing is not None:
                if existing != data:
                    raise ValueError(f"staged {plugin_id}/{filename} conflicts with existing product data")
                continue
            files[target] = data
            parent = target.parent
            while parent != Path("."):
                directories.add(parent)
                parent = parent.parent

    for file_path in files:
        parent = file_path.parent
        while parent != Path("."):
            if parent in files:
                raise ValueError(f"candidate file path collides with product data: {parent}")
            directories.add(parent)
            parent = parent.parent
    return directories, files


def _normalize_list_inputs(
    lists_source: Path | None, lists_manifest: Path | None,
) -> tuple[Path | None, Path | None, dict[str, bytes] | None]:
    if lists_source is None and lists_manifest is not None:
        raise ValueError("--lists-manifest requires --lists-source")
    if lists_source is None:
        return None, None, None
    source = _resolve_directory(lists_source, "list source")
    manifest_path = DEFAULT_LIST_MANIFEST if lists_manifest is None else lists_manifest
    manifest_path = _require_absolute(manifest_path, "list manifest")
    _reject_redirects(manifest_path, require_exists=True)
    manifest_real = manifest_path.resolve(strict=True)
    if not manifest_real.is_file():
        raise ValueError(f"list manifest is not a regular file: {manifest_path}")
    snapshots = {
        "items.txt": _snapshot_regular_file(source / "items.txt", "Loadout items.txt"),
        "outfits.txt": _snapshot_regular_file(source / "outfits.txt", "Loadout outfits.txt"),
        "plugin.ini": _snapshot_regular_file(manifest_real, "Loadout list-pack manifest"),
    }
    return source, manifest_real, snapshots


def _mkdir_exclusive(path: Path) -> None:
    path.mkdir()
    _reject_redirects(path, require_exists=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode):
        raise ValueError(f"created staging directory is not a regular directory: {path}")


def _write_exclusive(root: Path, relative: Path, data: bytes) -> None:
    if relative.is_absolute() or not relative.parts or any(part in ("", ".", "..") for part in relative.parts):
        raise ValueError(f"unsafe staging relative path: {relative}")
    parent = root
    for component in relative.parts[:-1]:
        parent = parent / component
        if not parent.exists():
            _mkdir_exclusive(parent)
        _reject_redirects(parent, require_exists=True)
        if not parent.is_dir():
            raise ValueError(f"staging parent is not a directory: {parent}")
    target = parent / relative.parts[-1]
    _reject_redirects(target)
    try:
        target.lstat()
    except FileNotFoundError:
        pass
    else:
        raise FileExistsError(f"refusing to replace candidate staging file: {target}")

    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_BINARY", 0)
    descriptor = os.open(str(target), flags, 0o600)
    with os.fdopen(descriptor, "wb") as destination:
        destination.write(data)
        destination.flush()


def _create_private_staging_root(staging_parent: Path, protected: list[Path]) -> Path:
    staging_parent = _resolve_directory(staging_parent, "caller-selected staging parent")
    for path in protected:
        if _overlaps(staging_parent, path):
            raise ValueError(f"staging parent must be disjoint from protected input: {path}")
    created = Path(tempfile.mkdtemp(prefix="sco-loadout-candidate-", dir=staging_parent))
    try:
        _reject_redirects(created, require_exists=True)
        resolved = created.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise ValueError(f"cannot verify private candidate staging directory {created}: {exc}") from exc
    for path in protected:
        if _overlaps(resolved, path):
            raise ValueError(f"generated staging directory overlaps protected input: {path}; created {created}")
    return resolved


def _write_candidate_tree(output: Path, directories: set[Path], files: dict[Path, bytes]) -> None:
    for relative in sorted(directories, key=lambda path: (len(path.parts), path.as_posix().casefold())):
        if relative == Path("."):
            continue
        current = output
        for component in relative.parts:
            current = current / component
            try:
                current.lstat()
            except FileNotFoundError:
                _mkdir_exclusive(current)
            else:
                _reject_redirects(current, require_exists=True)
                if not current.is_dir():
                    raise ValueError(f"staging path is not a directory: {current}")
    for relative, data in sorted(files.items(), key=lambda item: item[0].as_posix().casefold()):
        _write_exclusive(output, relative, data)


def _stage_lists_from_snapshot(output: Path, staging_parent: Path, snapshots: dict[str, bytes]) -> None:
    helper = _load_module("stage_loadout_lists_candidate", LIST_STAGE_FILE)
    old_argv = sys.argv
    had_destdir = "DESTDIR" in os.environ
    old_destdir = os.environ.get("DESTDIR")
    errors = io.StringIO()
    with tempfile.TemporaryDirectory(prefix="sco-loadout-list-input-", dir=staging_parent) as temporary:
        source = Path(temporary)
        for filename in ("items.txt", "outfits.txt"):
            _write_exclusive(source, Path(filename), snapshots[filename])
        manifest = source / "plugin.ini"
        _write_exclusive(source, Path("plugin.ini"), snapshots["plugin.ini"])
        sys.argv = [
            str(LIST_STAGE_FILE),
            "--prefix", str(output),
            "--source", str(source),
            "--manifest", str(manifest),
            "--buildables", "OFF",
        ]
        try:
            os.environ.pop("DESTDIR", None)
            with contextlib.redirect_stderr(errors):
                result = helper.main()
        finally:
            sys.argv = old_argv
            if had_destdir:
                assert old_destdir is not None
                os.environ["DESTDIR"] = old_destdir
            else:
                os.environ.pop("DESTDIR", None)
    if result != 0:
        detail = errors.getvalue().strip() or f"helper returned {result}"
        raise RuntimeError(detail)


def stage_candidate(
    artifact_root: Path,
    *,
    staging_parent: Path,
    lists_source: Path | None = None,
    lists_manifest: Path | None = None,
    on_staging_parent_validated=None,
) -> dict[str, Any]:
    """Snapshot, validate, then exclusively compose a candidate inspection tree."""
    stage_plugins = _load_module("stage_release_plugins_candidate", PROFILE_STAGE_FILE)
    profile = _candidate_profile(stage_plugins)
    artifact_root = _resolve_directory(artifact_root, "artifact root")
    list_source, list_manifest, list_snapshots = _normalize_list_inputs(lists_source, lists_manifest)
    staging_parent = _resolve_directory(staging_parent, "caller-selected staging parent")

    protected = [ROOT.resolve(), artifact_root]
    if list_source is not None:
        protected.append(list_source)
    if list_manifest is not None:
        protected.append(list_manifest.parent)
    for path in protected:
        if _overlaps(staging_parent, path):
            raise ValueError(f"staging parent must be disjoint from protected input: {path}")

    default_root = _resolve_directory(ROOT / "data" / "plugins", "tracked product plugin defaults")
    default_directories, default_files = _snapshot_tree(default_root)
    artifact_snapshots, module_hashes = _snapshot_artifacts(profile, artifact_root, stage_plugins)
    directories, files = _compose_plugin_tree(profile, artifact_snapshots, default_directories, default_files)

    if default_root not in protected:
        protected.append(default_root)
    if on_staging_parent_validated is not None:
        on_staging_parent_validated(staging_parent)
    output = _create_private_staging_root(staging_parent, protected)
    try:
        _write_candidate_tree(output, directories, files)
    except (OSError, RuntimeError, ValueError) as exc:
        raise RuntimeError(f"candidate composition incomplete at {output}; no files were replaced: {exc}") from exc

    if list_snapshots is not None:
        try:
            _stage_lists_from_snapshot(output, staging_parent, list_snapshots)
        except (OSError, RuntimeError, ValueError) as exc:
            raise RuntimeError(
                f"optional list-pack staging failed after plugin composition at {output}; "
                f"the items/outfits pack may be partial; no user files are rolled back or replaced: {exc}"
            ) from exc

    return {
        "profile": PROFILE_NAME,
        "artifact_root": artifact_root,
        "output": output,
        "plugins": [
            {
                "id": entry["id"],
                "artifact": artifact_root / entry["artifact_dir"] / entry["module"],
                "sha256": module_hashes[entry["id"]],
                "disabled": entry["disabled"],
            }
            for entry in profile["plugins"]
        ],
        "lists": list_snapshots is not None,
        "buildables": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", required=True, type=Path,
                        help="absolute root containing all 12 caller-built profile artifacts")
    parser.add_argument("--staging-parent", required=True, type=Path,
                        help="absolute caller-owned, non-install directory disjoint from source and artifact roots")
    parser.add_argument("--lists-source", type=Path,
                        help="optional directory containing explicitly supplied items.txt and outfits.txt")
    parser.add_argument("--lists-manifest", type=Path, default=None,
                        help="optional list-pack manifest (defaults to the tracked Loadout manifest)")
    args = parser.parse_args(argv)

    def announce_parent(path: Path) -> None:
        print(f"Validated caller-selected staging parent (must be a non-install path): {path}", flush=True)

    try:
        receipt = stage_candidate(
            args.artifact_root,
            staging_parent=args.staging_parent,
            lists_source=args.lists_source,
            lists_manifest=args.lists_manifest,
            on_staging_parent_validated=announce_parent,
        )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"Loadout candidate staging failed: {exc}", file=sys.stderr)
        return 1

    print(f"Staged {len(receipt['plugins'])} plugins for {receipt['profile']}.")
    print(f"Private temporary inspection tree: {receipt['output']}")
    print(f"Artifact root: {receipt['artifact_root']}")
    for entry in receipt["plugins"]:
        default = "disabled" if entry["disabled"] else "enabled"
        print(f"  {entry['id']}: {entry['artifact']} sha256={entry['sha256']} ({default} default)")
    if receipt["lists"]:
        print("Loadout items/outfits list pack staged with buildables OFF.")
    print("Inspection tree only; this does not establish runtime, upgrade-preservation, parity, or release acceptance.")
    print("Assumption: staging parent ancestors and supplied inputs are not concurrently modified; hostile parent replacement is outside this tool's guarantee.")
    print("The caller must choose a non-install staging parent; this tool cannot identify arbitrary game install roots.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
