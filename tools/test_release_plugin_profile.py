#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests the fail-closed player-release plugin selection and staging path."""
from __future__ import annotations

import contextlib
import io
import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
PROFILE_PATH = TOOLS / "release-plugin-profiles.json"
spec = importlib.util.spec_from_file_location("stage_release_plugins", TOOLS / "stage-release-plugins.py")
assert spec and spec.loader
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def offline_profile():
    return stage.load_profile("player-offline", PROFILE_PATH)


def make_artifacts(root: Path, profile: dict) -> None:
    for entry in profile["plugins"]:
        source = root / entry["artifact_dir"]
        source.mkdir(parents=True)
        manifest = (ROOT / "plugins" / entry["id"] / "plugin.ini").read_bytes()
        (source / "plugin.ini").write_bytes(manifest)
        (source / entry["module"]).write_bytes(f"fixture for {entry['id']}; not a PE DLL".encode())
        for filename in entry.get("extra_files", []):
            (source / filename).write_bytes((ROOT / "plugins" / entry["id"] / filename).read_bytes())
        if entry["disabled"]:
            (source / "disabled").write_bytes((ROOT / "plugins" / entry["id"] / "disabled").read_bytes())


class ReleasePluginProfile(unittest.TestCase):
    def test_every_plugin_folder_is_selected_or_has_a_reason(self):
        profile = offline_profile()
        selected = {entry["id"] for entry in profile["plugins"]}
        excluded = set(profile["excluded"])
        repository_plugins = {
            folder.name
            for folder in (ROOT / "plugins").iterdir()
            if folder.is_dir() and (folder / "plugin.ini").is_file()
        }
        self.assertEqual(selected, {
            "creative", "discord", "crew", "spawn", "build", "mining",
            "teleport", "quantum", "inspector", "npc", "contracts",
        })
        self.assertEqual(selected | excluded, repository_plugins)
        self.assertTrue(all(reason.strip() for reason in profile["excluded"].values()))
        self.assertEqual(set(profile["external_exclusions"]), {"sc-mp"})

    def test_product_defaults_match_the_profile(self):
        profile = offline_profile()
        disabled = {entry["id"] for entry in profile["plugins"] if entry["disabled"]}
        self.assertEqual(disabled, {"creative", "discord"})
        for entry in profile["plugins"]:
            source = ROOT / "plugins" / entry["id"]
            if entry["disabled"]:
                self.assertTrue((source / "disabled").is_file(), entry["id"])
            else:
                self.assertFalse((source / "disabled").exists(), entry["id"])
            tracked_manifest = ROOT / "data" / "plugins" / entry["id"] / "plugin.ini"
            if tracked_manifest.exists():
                self.assertEqual(tracked_manifest.read_bytes(), (source / "plugin.ini").read_bytes(), entry["id"])

    def test_profile_stages_only_selected_modules_and_preserves_defaults(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package" / "data" / "plugins"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            stage.stage_profile(profile, artifacts, destination)

            for entry in profile["plugins"]:
                folder = destination / entry["id"]
                self.assertTrue((folder / "plugin.ini").is_file())
                self.assertTrue((folder / entry["module"]).is_file())
                self.assertEqual((folder / "disabled").is_file(), entry["disabled"], entry["id"])
                self.assertEqual((folder / "plugin.ini").read_bytes(), (ROOT / "plugins" / entry["id"] / "plugin.ini").read_bytes())
            for excluded in profile["excluded"]:
                self.assertFalse(any((destination / excluded).glob("*.dll")), excluded)

            dirs = {p.name for p in destination.iterdir() if p.is_dir()}
            self.assertEqual(dirs, {
                "build", "contracts", "creative", "crew", "discord", "inspector",
                "mining", "npc", "picker_catalog", "quantum", "quantum_drive",
                "spawn", "teleport",
            })

    def test_release_cli_stages_the_selected_profile(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package" / "data" / "plugins"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            with contextlib.redirect_stdout(io.StringIO()):
                result = stage.main([
                    "--profile", "player-offline", "--artifact-root", str(artifacts),
                    "--destination", str(destination),
                ])
            self.assertEqual(result, 0)
            self.assertTrue((destination / "contracts" / "contracts.dll").is_file())
            self.assertTrue((destination / "creative" / "disabled").is_file())
            self.assertFalse((destination / "npc" / "disabled").exists())

    def test_missing_module_refuses_the_package(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            first = profile["plugins"][0]
            (artifacts / first["artifact_dir"] / first["module"]).unlink()
            with self.assertRaisesRegex(ValueError, "module is missing"):
                stage.stage_profile(profile, artifacts, destination)

    def test_wrong_default_marker_refuses_the_package(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            enabled = next(entry for entry in profile["plugins"] if not entry["disabled"])
            (artifacts / enabled["artifact_dir"] / "disabled").touch()
            with self.assertRaisesRegex(ValueError, "disabled marker must be absent"):
                stage.stage_profile(profile, artifacts, destination)

    def test_conflicting_existing_product_metadata_refuses_to_stage(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            (destination / "creative" / "plugin.ini").write_text("id = other\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "conflicts with existing product data"):
                stage.stage_profile(profile, artifacts, destination)

    def test_existing_disabled_marker_cannot_change_a_selected_default(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            enabled = next(entry for entry in profile["plugins"] if not entry["disabled"])
            (destination / enabled["id"] / "disabled").touch()
            with self.assertRaisesRegex(ValueError, "destination disabled marker must be absent"):
                stage.stage_profile(profile, artifacts, destination)

    def test_workflow_uses_this_profile_and_not_a_second_feature_list(self):
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text()
        self.assertIn("python3 tools/stage-release-plugins.py --profile player-offline", workflow)
        package_step = workflow.split("name: Package sc-offline-<tag>.zip", 1)[1].split("name: Create Release", 1)[0]
        self.assertNotIn("for feature in creative discord crew spawn build mining teleport quantum", package_step)
        self.assertNotIn("cp built-inspector/inspector", package_step)
        self.assertNotIn("test-plugins", package_step)

    def test_cli_rejects_an_unknown_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "unknown release profile"):
                stage.load_profile("studio-headless", PROFILE_PATH)


if __name__ == "__main__":
    unittest.main(verbosity=2)
