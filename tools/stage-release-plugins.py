#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage the explicitly selected player plugin profile into a release tree."""
from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path
from typing import Any


PROFILE_FILE = Path(__file__).with_name("release-plugin-profiles.json")
PLUGIN_ID = re.compile(r"^[a-z0-9_]{1,31}$")


def _safe_relative(value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty relative path")
    path = Path(value)
    if "\\" in value or ":" in value or path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"{label} must stay below its root")
    return path


def load_profile(profile_name: str, profile_file: Path = PROFILE_FILE) -> dict[str, Any]:
    try:
        document = json.loads(profile_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read release profile: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise ValueError("unsupported release profile schema")
    profiles = document.get("profiles")
    if not isinstance(profiles, dict) or profile_name not in profiles:
        raise ValueError(f"unknown release profile: {profile_name}")
    profile = profiles[profile_name]
    if not isinstance(profile, dict) or not isinstance(profile.get("plugins"), list):
        raise ValueError(f"invalid release profile: {profile_name}")

    seen: set[str] = set()
    for entry in profile["plugins"]:
        if not isinstance(entry, dict):
            raise ValueError("each profile plugin must be an object")
        plugin_id = entry.get("id")
        if not isinstance(plugin_id, str) or not PLUGIN_ID.fullmatch(plugin_id):
            raise ValueError(f"invalid plugin ID: {plugin_id!r}")
        if plugin_id in seen:
            raise ValueError(f"duplicate plugin ID: {plugin_id}")
        seen.add(plugin_id)
        _safe_relative(entry.get("artifact_dir"), f"{plugin_id}.artifact_dir")
        module = entry.get("module")
        if module != f"{plugin_id}.dll":
            raise ValueError(f"{plugin_id}.module must be {plugin_id}.dll")
        if not isinstance(entry.get("disabled"), bool):
            raise ValueError(f"{plugin_id}.disabled must be a boolean")
        extras = entry.get("extra_files", [])
        if not isinstance(extras, list) or any(
            not isinstance(name, str) or not name or Path(name).name != name or "/" in name or "\\" in name or ":" in name or name in (".", "..", "plugin.ini", module, "disabled")
            for name in extras
        ):
            raise ValueError(f"{plugin_id}.extra_files must contain unique file names")
        if len(set(extras)) != len(extras):
            raise ValueError(f"{plugin_id}.extra_files contains duplicates")

    excluded = profile.get("excluded", {})
    if not isinstance(excluded, dict) or any(
        not isinstance(plugin_id, str) or not PLUGIN_ID.fullmatch(plugin_id) or not isinstance(reason, str) or not reason.strip()
        for plugin_id, reason in excluded.items()
    ):
        raise ValueError("excluded must map plugin IDs to reasons")
    if seen.intersection(excluded):
        raise ValueError("a plugin cannot be both included and excluded")
    return profile


def _regular_file(path: Path, label: str) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{label} is missing or is not a regular file: {path}")
    return path.read_bytes()


def _manifest_fields(text: str, plugin_id: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for key in ("id", "kind", "entry"):
        matches = re.findall(rf"(?im)^\s*{key}\s*=\s*([^\r\n#;]+?)\s*$", text)
        if len(matches) != 1:
            raise ValueError(f"{plugin_id}/plugin.ini must define {key} exactly once")
        fields[key] = matches[0].strip().strip('"')
    if fields["id"] != plugin_id or fields["kind"] != "native" or fields["entry"] != f"{plugin_id}.dll":
        raise ValueError(f"{plugin_id}/plugin.ini identity, kind, or entry does not match the selected profile")
    return fields


def stage_profile(profile: dict[str, Any], artifact_root: Path, destination: Path) -> None:
    artifact_root = artifact_root.resolve()
    if destination.is_symlink():
        raise ValueError(f"refusing to stage into a symlink: {destination}")
    destination.mkdir(parents=True, exist_ok=True)
    plan: list[tuple[Path, Path, list[str]]] = []
    for entry in profile["plugins"]:
        plugin_id = entry["id"]
        source_dir = artifact_root / _safe_relative(entry["artifact_dir"], f"{plugin_id}.artifact_dir")
        try:
            source_dir.resolve().relative_to(artifact_root)
        except ValueError as exc:
            raise ValueError(f"{plugin_id} artifact escapes the artifact root") from exc
        if source_dir.is_symlink() or not source_dir.is_dir():
            raise ValueError(f"{plugin_id} artifact directory is missing or is not a regular directory")

        manifest = _regular_file(source_dir / "plugin.ini", f"{plugin_id} manifest").decode("utf-8-sig")
        _manifest_fields(manifest, plugin_id)
        module = entry["module"]
        _regular_file(source_dir / module, f"{plugin_id} module")
        marker = source_dir / "disabled"
        marker_present = marker.exists() or marker.is_symlink()
        if marker_present != entry["disabled"]:
            expected = "present" if entry["disabled"] else "absent"
            raise ValueError(f"{plugin_id} disabled marker must be {expected}")
        if marker_present:
            _regular_file(marker, f"{plugin_id} disabled marker")

        target_dir = destination / plugin_id
        if target_dir.is_symlink() or (target_dir.exists() and not target_dir.is_dir()):
            raise ValueError(f"refusing to stage into a non-directory: {target_dir}")
        target_marker = target_dir / "disabled"
        target_marker_present = target_marker.exists() or target_marker.is_symlink()
        if target_marker_present != entry["disabled"]:
            expected = "present" if entry["disabled"] else "absent"
            raise ValueError(f"{plugin_id} destination disabled marker must be {expected}")
        filenames = ["plugin.ini", module, *entry.get("extra_files", [])]
        if entry["disabled"]:
            filenames.append("disabled")
        for filename in filenames:
            contents = _regular_file(source_dir / filename, f"{plugin_id}/{filename}")
            target = target_dir / filename
            if target.is_symlink():
                raise ValueError(f"refusing to replace a symlink: {target}")
            if target.exists():
                existing = _regular_file(target, f"existing {plugin_id}/{filename}")
                if existing != contents:
                    raise ValueError(f"staged {plugin_id}/{filename} conflicts with existing product data")
        plan.append((source_dir, target_dir, filenames))

    for source_dir, target_dir, filenames in plan:
        target_dir.mkdir(parents=True, exist_ok=True)
        for filename in filenames:
            target = target_dir / filename
            if not target.exists():
                shutil.copyfile(source_dir / filename, target)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--artifact-root", type=Path, default=Path.cwd())
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        profile = load_profile(args.profile)
        stage_profile(profile, args.artifact_root, args.destination)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"Staged {len(profile['plugins'])} plugins for profile {args.profile}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
