#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests the fail-closed player-release plugin selection and staging path."""
from __future__ import annotations

import contextlib
import io
import importlib.util
import json
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


# Creative lives in its own repository, carried as a submodule; every other plugin is under plugins/.
EXTERNAL_PLUGINS = {"creative": ROOT / "external" / "sc-creative" / "plugins" / "creative"}


def plugin_dir(plugin_id: str) -> Path:
    return EXTERNAL_PLUGINS.get(plugin_id, ROOT / "plugins" / plugin_id)


def offline_profile():
    return stage.load_profile("player-offline", PROFILE_PATH)


def make_artifacts(root: Path, profile: dict) -> None:
    for entry in profile["plugins"]:
        source = root / entry["artifact_dir"]
        source.mkdir(parents=True)
        manifest = (plugin_dir(entry["id"]) / "plugin.ini").read_bytes()
        (source / "plugin.ini").write_bytes(manifest)
        (source / entry["module"]).write_bytes(f"fixture for {entry['id']}; not a PE DLL".encode())
        for filename in entry.get("extra_files", []):
            (source / filename).write_bytes((plugin_dir(entry["id"]) / filename).read_bytes())
        if entry["disabled"]:
            (source / "disabled").write_bytes((plugin_dir(entry["id"]) / "disabled").read_bytes())


class ReleasePluginProfile(unittest.TestCase):
    def test_release_profile_has_twelve_plugins_with_loadout_enabled(self):
        profile = offline_profile()
        loadout = {
            "id": "loadout",
            "artifact_dir": "built-loadout/loadout",
            "module": "loadout.dll",
            "disabled": False,
        }
        self.assertIn(loadout, profile["plugins"])
        ids = [entry["id"] for entry in profile["plugins"]]
        self.assertEqual(len(ids), 12)
        self.assertEqual(len(ids), len(set(ids)))
        self.assertNotIn("loadout", profile["excluded"])
        self.assertEqual(set(profile["excluded"]), {"gear_presets", "titanlink", "voxel_bridge"})
        self.assertNotIn("player-offline-loadout-candidate",
                         json.loads(PROFILE_PATH.read_text(encoding="utf-8"))["profiles"])

        manifest = (ROOT / "plugins" / "loadout" / "plugin.ini").read_text(encoding="utf-8")
        self.assertRegex(manifest, r"(?m)^id\s*=\s*loadout\s*$")
        self.assertRegex(manifest, r"(?m)^entry\s*=\s*loadout\.dll\s*$")
        self.assertFalse((ROOT / "plugins" / "loadout" / "disabled").exists())

    def test_release_stages_all_twelve_plugins_and_disabled_defaults(self):
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
                self.assertTrue((folder / "plugin.ini").is_file(), entry["id"])
                self.assertTrue((folder / entry["module"]).is_file(), entry["id"])
                self.assertEqual((folder / "disabled").is_file(), entry["disabled"], entry["id"])
                self.assertEqual(
                    (folder / "plugin.ini").read_bytes(),
                    (plugin_dir(entry["id"]) / "plugin.ini").read_bytes(),
                )

            self.assertTrue((destination / "loadout" / "loadout.dll").is_file())
            self.assertFalse((destination / "loadout" / "disabled").exists())
            self.assertEqual(
                {entry["id"] for entry in profile["plugins"]},
                {"build", "contracts", "creative", "crew", "discord", "inspector",
                 "loadout", "mining", "npc", "quantum", "spawn", "teleport"},
            )
            for excluded in profile["excluded"]:
                self.assertFalse(any((destination / excluded).glob("*.dll")), excluded)

    def test_release_keeps_committed_text_when_an_artifact_differs_only_in_line_endings(self):
        # A plugin built on a Windows runner carries CRLF text files; the release job checks out LF.
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package" / "data" / "plugins"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            mining = next(entry for entry in profile["plugins"] if entry["id"] == "mining")
            staged = artifacts / mining["artifact_dir"] / "import_settings.py"
            committed = destination / "mining" / "import_settings.py"
            lf = committed.read_bytes().replace(b"\r\n", b"\n")
            committed.write_bytes(lf)
            staged.write_bytes(lf.replace(b"\n", b"\r\n"))
            self.assertNotEqual(staged.read_bytes(), committed.read_bytes())

            stage.stage_profile(profile, artifacts, destination)
            self.assertEqual(committed.read_bytes(), lf)

            staged.write_bytes(lf.replace(b"\n", b"\r\n") + b"# changed\r\n")
            with self.assertRaisesRegex(ValueError, "conflicts with existing product data"):
                stage.stage_profile(profile, artifacts, destination)

    def test_release_loadout_conflicting_destination_refuses_to_stage(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            loadout_target = destination / "loadout"
            loadout_target.mkdir(parents=True)
            (loadout_target / "plugin.ini").write_text("id = another-plugin\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "conflicts with existing product data"):
                stage.stage_profile(profile, artifacts, destination)

    def test_release_loadout_disabled_marker_must_be_absent(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            (artifacts / "built-loadout" / "loadout" / "disabled").touch()

            with self.assertRaisesRegex(ValueError, "loadout disabled marker must be absent"):
                stage.stage_profile(profile, artifacts, destination)

    def test_release_rejects_wrong_loadout_manifest_without_staging_modules(self):
        profile = offline_profile()
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            artifacts = temp / "artifacts"
            destination = temp / "package"
            make_artifacts(artifacts, profile)
            shutil.copytree(ROOT / "data" / "plugins", destination)
            manifest = artifacts / "built-loadout" / "loadout" / "plugin.ini"
            manifest.write_text(
                manifest.read_text(encoding="utf-8").replace("id = loadout", "id = wrong"),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "identity, kind, or entry does not match"):
                stage.stage_profile(profile, artifacts, destination)

            for entry in profile["plugins"]:
                self.assertFalse((destination / entry["id"] / entry["module"]).exists(), entry["id"])

    def test_every_plugin_folder_is_selected_or_has_a_reason(self):
        profile = offline_profile()
        selected = {entry["id"] for entry in profile["plugins"]}
        excluded = set(profile["excluded"])
        repository_plugins = {
            folder.name
            for folder in [*(ROOT / "plugins").iterdir(), *EXTERNAL_PLUGINS.values()]
            if folder.is_dir() and (folder / "plugin.ini").is_file()
        }
        self.assertEqual(selected, {
            "creative", "discord", "crew", "spawn", "build", "mining",
            "teleport", "quantum", "inspector", "npc", "contracts", "loadout",
        })
        self.assertEqual(selected | excluded, repository_plugins)
        self.assertTrue(all(reason.strip() for reason in profile["excluded"].values()))
        self.assertEqual(set(profile["external_exclusions"]), {"sc-mp"})

    def test_product_defaults_match_the_profile(self):
        profile = offline_profile()
        disabled = {entry["id"] for entry in profile["plugins"] if entry["disabled"]}
        self.assertEqual(disabled, {"discord"})
        for entry in profile["plugins"]:
            source = plugin_dir(entry["id"])
            if entry["disabled"]:
                self.assertTrue((source / "disabled").is_file(), entry["id"])
            else:
                self.assertFalse((source / "disabled").exists(), entry["id"])
            tracked_manifest = ROOT / "data" / "plugins" / entry["id"] / "plugin.ini"
            if tracked_manifest.exists():
                self.assertEqual(tracked_manifest.read_bytes(), (source / "plugin.ini").read_bytes(), entry["id"])

    def test_creative_comes_from_the_submodule_and_ships_enabled(self):
        source = plugin_dir("creative")
        self.assertTrue((source / "plugin.ini").is_file(), "run: git submodule update --init external/sc-creative")
        self.assertFalse((ROOT / "plugins" / "creative").exists(), "creative must not be duplicated under plugins/")
        self.assertFalse((source / "disabled").exists())
        tracked = ROOT / "data" / "plugins" / "creative"
        self.assertEqual({p.name for p in tracked.iterdir()}, {"plugin.ini"}, "no tracked disabled marker")
        self.assertEqual((tracked / "plugin.ini").read_bytes(), (source / "plugin.ini").read_bytes())
        for profile in (offline_profile(),):
            entry = next(e for e in profile["plugins"] if e["id"] == "creative")
            self.assertEqual(entry, {"id": "creative", "artifact_dir": "built-external/creative",
                                     "module": "creative.dll", "disabled": False})

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
                self.assertEqual((folder / "plugin.ini").read_bytes(), (plugin_dir(entry["id"]) / "plugin.ini").read_bytes())
            for excluded in profile["excluded"]:
                self.assertFalse(any((destination / excluded).glob("*.dll")), excluded)

            dirs = {p.name for p in destination.iterdir() if p.is_dir()}
            self.assertEqual(dirs, {
                "build", "contracts", "creative", "crew", "discord", "inspector", "loadout",
                "build_catalog", "locations_catalog", "mining", "npc", "picker_catalog", "quantum", "quantum_drive",
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
            self.assertFalse((destination / "creative" / "disabled").exists())
            self.assertTrue((destination / "discord" / "disabled").is_file())
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
        packager = (TOOLS / "package_release.py").read_text()
        # The release job packages through tools/package_release.py, which stages the profile.
        package_step = workflow.split("name: Package sc-offline-<tag>.zip", 1)[1].split("name: Create Release", 1)[0]
        self.assertIn("python3 tools/package_release.py", package_step)
        self.assertIn("stage_tool.stage_profile", packager)
        self.assertIn('default="player-offline"', packager)
        self.assertNotIn("for feature in creative discord crew spawn build mining teleport quantum", package_step)
        self.assertNotIn("cp built-inspector/inspector", package_step)
        self.assertNotIn("test-plugins", package_step)

    def test_cli_rejects_an_unknown_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "unknown release profile"):
                stage.load_profile("studio-headless", PROFILE_PATH)


if __name__ == "__main__":
    unittest.main(verbosity=2)
