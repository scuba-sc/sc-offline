#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests candidate composition with isolated synthetic, non-distributable peers."""
from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
SYNTHETIC_MODULE = b"SYNTHETIC TEST FIXTURE ONLY; NOT A PE DLL; NON-DISTRIBUTABLE\n"
PROVEN_LOADOUT_MODULE_SHA256 = "a0d515118876f7cbcbf265ccb4be9b1f6061eaafcb0181e19d93cf98d442180b"
PROVEN_LOADOUT_MODULE_ENV = "SCO_CANDIDATE_PROVEN_LOADOUT_MODULE"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


candidate = load_module("stage_loadout_candidate_test", TOOLS / "stage_loadout_candidate.py")
profile_stager = load_module("stage_release_plugins_candidate_test", TOOLS / "stage-release-plugins.py")
list_stager = load_module("stage_loadout_lists_candidate_test", TOOLS / "stage_loadout_lists.py")


def candidate_profile() -> dict:
    return profile_stager.load_profile(candidate.PROFILE_NAME, candidate.PROFILE_FILE)


def make_synthetic_artifacts(root: Path) -> None:
    """Create isolated fake peers whose bytes are explicitly not distributable."""
    for entry in candidate_profile()["plugins"]:
        source = root / entry["artifact_dir"]
        source.mkdir(parents=True)
        plugin_root = ROOT / "plugins" / entry["id"]
        shutil.copyfile(plugin_root / "plugin.ini", source / "plugin.ini")
        (source / entry["module"]).write_bytes(SYNTHETIC_MODULE)
        for filename in entry.get("extra_files", []):
            shutil.copyfile(plugin_root / filename, source / filename)
        if entry["disabled"]:
            shutil.copyfile(plugin_root / "disabled", source / "disabled")


def make_lists(root: Path) -> None:
    root.mkdir()
    (root / "items.txt").write_text("# synthetic fixture only\n[head]\nFixtureHelmet\n", encoding="utf-8")
    (root / "outfits.txt").write_text("# synthetic fixture only\n[Fixture]\nhead FixtureHelmet\n", encoding="utf-8")
    (root / "buildables.txt").write_text("# must not be staged\n[fixture]\nFixtureBuildable\n", encoding="utf-8")


class LoadoutCandidateComposition(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="loadout-candidate-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / "artifacts"
        self.artifacts.mkdir()
        make_synthetic_artifacts(self.artifacts)
        self.stage_temp = tempfile.TemporaryDirectory(prefix="loadout-candidate-output-test-")
        self.addCleanup(self.stage_temp.cleanup)
        self.staging_parent = Path(self.stage_temp.name).resolve()
        self.output = self.staging_parent / "candidate-inspection-tree"

    def create_test_output(self, _staging_parent, _protected):
        self.output.mkdir(mode=0o700)
        return self.output

    def stage(self, *, lists_source=None, lists_manifest=None):
        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=self.create_test_output):
            return candidate.stage_candidate(
                self.artifacts,
                staging_parent=self.staging_parent,
                lists_source=lists_source,
                lists_manifest=lists_manifest,
            )

    def test_composes_all_twelve_supplied_artifacts_and_preserves_defaults(self):
        receipt = self.stage()
        self.assertEqual(receipt["profile"], "player-offline-loadout-candidate")
        self.assertEqual(len(receipt["plugins"]), 12)
        self.assertFalse(receipt["lists"])
        self.assertFalse(receipt["buildables"])
        plugin_root = self.output / "data" / "plugins"
        expected_ids = {entry["id"] for entry in candidate_profile()["plugins"]}
        actual_dirs = {path.name for path in plugin_root.iterdir() if path.is_dir()}
        tracked_defaults = {path.name for path in (ROOT / "data" / "plugins").iterdir() if path.is_dir()}
        self.assertEqual(actual_dirs, expected_ids | tracked_defaults)
        self.assertEqual(len(expected_ids), 12)
        for entry in candidate_profile()["plugins"]:
            staged = plugin_root / entry["id"]
            self.assertEqual((staged / "disabled").is_file(), entry["disabled"], entry["id"])
            self.assertEqual((staged / entry["module"]).read_bytes(), SYNTHETIC_MODULE, entry["id"])
            self.assertEqual(
                (staged / "plugin.ini").read_bytes(),
                (ROOT / "plugins" / entry["id"] / "plugin.ini").read_bytes(),
            )
            if entry["disabled"]:
                self.assertEqual(
                    (staged / "disabled").read_bytes(),
                    (ROOT / "data" / "plugins" / entry["id"] / "disabled").read_bytes(),
                )
        loadout = plugin_root / "loadout"
        self.assertTrue((loadout / "loadout.dll").is_file())
        self.assertFalse((loadout / "disabled").exists())
        self.assertFalse((plugin_root / "loadout_lists").exists())
        for entry in receipt["plugins"]:
            self.assertEqual(entry["sha256"], hashlib.sha256(SYNTHETIC_MODULE).hexdigest())

    def test_controlled_default_fixture_preserves_existing_lists_and_disabled_markers(self):
        default_root = self.root / "controlled-defaults" / "data" / "plugins"
        marker_bytes = {}
        for plugin_id in ("creative", "discord"):
            marker = default_root / plugin_id / "disabled"
            marker.parent.mkdir(parents=True, exist_ok=True)
            source_marker = ROOT / "data" / "plugins" / plugin_id / "disabled"
            marker_bytes[plugin_id] = source_marker.read_bytes()
            marker.write_bytes(marker_bytes[plugin_id])
        list_source = self.root / "controlled-list-source"
        make_lists(list_source)
        _, _, list_snapshots = candidate._normalize_list_inputs(list_source, None)
        assert list_snapshots is not None
        list_bytes = {
            "lists/items.txt": list_snapshots["items.txt"],
            "lists/outfits.txt": list_snapshots["outfits.txt"],
            "plugin.ini": list_snapshots["plugin.ini"],
        }
        for relative, data in list_bytes.items():
            path = default_root / "loadout_lists" / Path(relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        optional_marker = b"controlled default list-pack marker bytes\n"
        (default_root / "loadout_lists" / "disabled").write_bytes(optional_marker)

        profile = candidate._candidate_profile(profile_stager)
        artifact_files, _ = candidate._snapshot_artifacts(profile, self.artifacts, profile_stager)
        default_directories, default_files = candidate._snapshot_tree(default_root)
        directories, files = candidate._compose_plugin_tree(
            profile, artifact_files, default_directories, default_files
        )
        output = self.staging_parent / "controlled-default-composition"
        output.mkdir(mode=0o700)
        candidate._write_candidate_tree(output, directories, files)
        candidate._stage_lists_from_snapshot(output, self.staging_parent, list_snapshots)

        for plugin_id, expected in marker_bytes.items():
            self.assertEqual((output / "data" / "plugins" / plugin_id / "disabled").read_bytes(), expected)
        for relative, expected in list_bytes.items():
            self.assertEqual(
                (output / "data" / "plugins" / "loadout_lists" / Path(relative)).read_bytes(), expected
            )
        self.assertEqual(
            (output / "data" / "plugins" / "loadout_lists" / "disabled").read_bytes(), optional_marker
        )

    def test_generated_output_is_child_of_selected_parent_and_disjoint(self):
        receipt = candidate.stage_candidate(self.artifacts, staging_parent=self.staging_parent)
        output = receipt["output"]
        self.assertEqual(output.parent, self.staging_parent)
        self.assertNotEqual(output, self.artifacts)
        self.assertNotIn(self.artifacts, output.parents)
        self.assertNotIn(output, self.artifacts.parents)
        self.assertTrue(output.name.startswith("sco-loadout-candidate-"))

    def test_staging_parent_inside_artifact_root_is_rejected_before_writing(self):
        before = {path.relative_to(self.artifacts) for path in self.artifacts.rglob("*")}
        unsafe_parent = self.artifacts / "candidate-staging-parent"
        unsafe_parent.mkdir()
        before.add(unsafe_parent.relative_to(self.artifacts))
        with self.assertRaisesRegex(ValueError, "staging parent must be disjoint"):
            candidate.stage_candidate(self.artifacts, staging_parent=unsafe_parent)
        self.assertEqual({path.relative_to(self.artifacts) for path in self.artifacts.rglob("*")}, before)

    def test_cli_reports_generated_candidate_only_tree_and_artifact_hashes(self):
        output = io.StringIO()
        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=self.create_test_output):
            with contextlib.redirect_stdout(output):
                result = candidate.main([
                    "--artifact-root", str(self.artifacts),
                    "--staging-parent", str(self.staging_parent),
                ])
        self.assertEqual(result, 0)
        report = output.getvalue()
        self.assertIn("Staged 12 plugins for player-offline-loadout-candidate", report)
        self.assertIn(str(self.output), report)
        self.assertIn("Validated caller-selected staging parent", report)
        self.assertIn(str(self.artifacts / "built-loadout" / "loadout" / "loadout.dll"), report)
        self.assertIn("sha256=" + hashlib.sha256(SYNTHETIC_MODULE).hexdigest(), report)
        self.assertIn("does not establish runtime, upgrade-preservation, parity, or release acceptance", report)
        self.assertIn("not concurrently modified", report)

    def test_cli_does_not_accept_a_user_selected_output_path(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                candidate.main([
                    "--artifact-root", str(self.artifacts),
                    "--staging-parent", str(self.staging_parent),
                    "--output", str(self.output),
                ])
        self.assertFalse(self.output.exists())

    def test_artifact_module_symlink_is_rejected_before_output_creation(self):
        module = self.artifacts / "built-loadout" / "loadout" / "loadout.dll"
        target = self.root / "module-target.bin"
        target.write_bytes(SYNTHETIC_MODULE)
        module.unlink()
        try:
            module.symlink_to(target)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"file symlink creation unavailable: {exc}")
        with self.assertRaisesRegex(ValueError, "redirected or ambiguous"):
            self.stage()
        self.assertFalse(self.output.exists())
        self.assertEqual(target.read_bytes(), SYNTHETIC_MODULE)

    def test_source_change_after_snapshot_does_not_change_staged_bytes(self):
        module = self.artifacts / "built-loadout" / "loadout" / "loadout.dll"

        def mutate_after_snapshot(staging_parent, protected):
            module.write_bytes(b"changed after snapshot\n")
            return self.create_test_output(staging_parent, protected)

        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=mutate_after_snapshot):
            candidate.stage_candidate(self.artifacts, staging_parent=self.staging_parent)
        staged = self.output / "data" / "plugins" / "loadout" / "loadout.dll"
        self.assertEqual(staged.read_bytes(), SYNTHETIC_MODULE)
        self.assertEqual(module.read_bytes(), b"changed after snapshot\n")

    def test_exclusive_publication_preserves_a_racing_conflicting_file(self):
        real_open = os.open
        injected = {"path": None}

        def insert_conflict(path, flags, mode=0o777):
            target = Path(path)
            if target.name == "loadout.dll" and flags & os.O_EXCL:
                fd = real_open(
                    str(target),
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0),
                    0o600,
                )
                os.write(fd, b"concurrent user-owned sentinel\n")
                os.close(fd)
                injected["path"] = target
            return real_open(path, flags, mode)

        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=self.create_test_output):
            with mock.patch.object(candidate.os, "open", side_effect=insert_conflict):
                with self.assertRaisesRegex(RuntimeError, "no files were replaced"):
                    candidate.stage_candidate(self.artifacts, staging_parent=self.staging_parent)
        self.assertIsNotNone(injected["path"])
        self.assertEqual(injected["path"].read_bytes(), b"concurrent user-owned sentinel\n")

    def test_product_manifest_conflict_is_rejected_before_output_creation(self):
        manifest = self.artifacts / "built-external" / "creative" / "plugin.ini"
        manifest.write_bytes(manifest.read_bytes() + b"\n# different candidate data\n")
        with self.assertRaisesRegex(ValueError, "conflicts with existing product data"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_disabled_marker_bytes_are_preserved_and_mismatch_refused(self):
        marker = self.artifacts / "built-external" / "creative" / "disabled"
        marker.write_bytes(b"different default marker\n")
        with self.assertRaisesRegex(ValueError, "disabled conflicts with existing product data"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_loadout_disabled_marker_is_refused(self):
        (self.artifacts / "built-loadout" / "loadout" / "disabled").write_bytes(b"fixture marker\n")
        with self.assertRaisesRegex(ValueError, "disabled marker must be absent"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_optional_lists_stage_only_items_and_outfits_with_buildables_off(self):
        lists = self.root / "explicit-lists"
        make_lists(lists)
        receipt = self.stage(lists_source=lists)
        self.assertTrue(receipt["lists"])
        self.assertFalse(receipt["buildables"])
        pack = self.output / "data" / "plugins" / "loadout_lists"
        actual = {path.relative_to(pack).as_posix() for path in pack.rglob("*") if path.is_file()}
        self.assertEqual(actual, {"lists/items.txt", "lists/outfits.txt", "plugin.ini"})
        self.assertEqual((pack / "lists/items.txt").read_bytes(), (lists / "items.txt").read_bytes())
        self.assertEqual((pack / "lists/outfits.txt").read_bytes(), (lists / "outfits.txt").read_bytes())
        self.assertFalse((pack / "lists/buildables.txt").exists())

    def test_late_optional_list_failure_reports_partial_output(self):
        lists = self.root / "explicit-lists"
        make_lists(lists)
        original_loader = candidate._load_module

        def controlled_loader(name: str, path: Path):
            if path == candidate.LIST_STAGE_FILE:
                return list_stager
            return original_loader(name, path)

        real_link = os.link

        def fail_on_outfits(source, destination, *args, **kwargs):
            if Path(destination).name == "outfits.txt":
                raise OSError("synthetic fixture failure after items publication")
            return real_link(source, destination, *args, **kwargs)

        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=self.create_test_output):
            with mock.patch.object(candidate, "_load_module", side_effect=controlled_loader):
                with mock.patch.object(list_stager.os, "link", side_effect=fail_on_outfits):
                    with self.assertRaisesRegex(RuntimeError, "items/outfits pack may be partial"):
                        candidate.stage_candidate(self.artifacts, staging_parent=self.staging_parent, lists_source=lists)

        pack = self.output / "data" / "plugins" / "loadout_lists"
        self.assertTrue((self.output / "data" / "plugins" / "loadout" / "loadout.dll").is_file())
        self.assertEqual((pack / "lists/items.txt").read_bytes(), (lists / "items.txt").read_bytes())
        self.assertFalse((pack / "lists/outfits.txt").exists())
        self.assertFalse((pack / "plugin.ini").exists())

    def test_conflicting_list_file_is_preserved(self):
        lists = self.root / "explicit-lists"
        make_lists(lists)
        original_loader = candidate._load_module

        def controlled_loader(name: str, path: Path):
            if path == candidate.LIST_STAGE_FILE:
                return list_stager
            return original_loader(name, path)

        real_link = os.link
        sentinel = b"concurrent user list bytes\n"

        def conflict_on_items(source, destination, *args, **kwargs):
            if Path(destination).name == "items.txt":
                Path(destination).write_bytes(sentinel)
                raise FileExistsError(str(destination))
            return real_link(source, destination, *args, **kwargs)

        with mock.patch.object(candidate, "_create_private_staging_root", side_effect=self.create_test_output):
            with mock.patch.object(candidate, "_load_module", side_effect=controlled_loader):
                with mock.patch.object(list_stager.os, "link", side_effect=conflict_on_items):
                    with self.assertRaisesRegex(RuntimeError, "items/outfits pack may be partial"):
                        candidate.stage_candidate(self.artifacts, staging_parent=self.staging_parent, lists_source=lists)
        target = self.output / "data" / "plugins" / "loadout_lists" / "lists" / "items.txt"
        self.assertEqual(target.read_bytes(), sentinel)

    def test_missing_peer_artifact_refuses_composition(self):
        (self.artifacts / "built-npc" / "npc" / "npc.dll").unlink()
        with self.assertRaisesRegex(ValueError, r"npc/npc\.dll is missing"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_optional_real_c591_loadout_module_is_copied_byte_for_byte(self):
        # This digest came from a separate c591 legacy SDK source proof; this
        # integration check verifies staging byte identity only.
        supplied = os.environ.get(PROVEN_LOADOUT_MODULE_ENV)
        if not supplied:
            self.skipTest(f"set {PROVEN_LOADOUT_MODULE_ENV} to the source-bound c591 module to run this integration check")
        real_module = Path(supplied)
        if not real_module.is_absolute() or not real_module.is_file() or real_module.is_symlink():
            self.fail(f"{PROVEN_LOADOUT_MODULE_ENV} must name an absolute regular module file")
        module_bytes = real_module.read_bytes()
        actual_sha256 = hashlib.sha256(module_bytes).hexdigest()
        self.assertEqual(actual_sha256, PROVEN_LOADOUT_MODULE_SHA256)
        real_manifest = real_module.with_name("plugin.ini")
        self.assertTrue(real_manifest.is_file())
        self.assertEqual(
            real_manifest.read_bytes(),
            (ROOT / "plugins" / "loadout" / "plugin.ini").read_bytes(),
        )

        artifact = self.artifacts / "built-loadout" / "loadout"
        shutil.copyfile(real_module, artifact / "loadout.dll")
        shutil.copyfile(real_manifest, artifact / "plugin.ini")
        receipt = self.stage()
        staged = receipt["output"] / "data" / "plugins" / "loadout" / "loadout.dll"
        self.assertEqual(hashlib.sha256(staged.read_bytes()).hexdigest(), PROVEN_LOADOUT_MODULE_SHA256)
        self.assertEqual(staged.read_bytes(), module_bytes)
        self.assertEqual(len(receipt["plugins"]), 12)


if __name__ == "__main__":
    unittest.main(verbosity=2)
