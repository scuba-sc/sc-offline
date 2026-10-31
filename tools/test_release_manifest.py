#!/usr/bin/env python3
"""Tests for tools/release-manifest.py. Run: python3 tools/test_release_manifest.py (CI's check job does)."""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True  # no tools/__pycache__ left in the tree
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("release_manifest", os.path.join(HERE, "release-manifest.py"))
assert spec and spec.loader
rm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rm)

COMMIT = "0123456789abcdef0123456789abcdef01234567"


class ReleaseManifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = os.path.join(self.tmp.name, "sc-offline-v0.7.0")
        os.makedirs(os.path.join(self.dir, "data", "OfflineDB"))
        self.put("sc-offline.exe", b"MZ launcher")
        self.put("dinput8.dll", b"MZ mod")
        self.put("data/items.txt", b"a\r\nb\r\n")
        self.put("data/OfflineDB/default_1.xml", b"<x/>")
        self.vh = os.path.join(self.tmp.name, "version.h")
        self.version("0.7.0")

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, data):
        with open(os.path.join(self.dir, *rel.split("/")), "wb") as f:
            f.write(data)

    def version(self, v):
        with open(self.vh, "w") as f:
            f.write(f'#pragma once\n#define SCO_VERSION  "{v}"\n')

    def manifest(self):
        with open(os.path.join(self.dir, rm.NAME)) as f:
            return f.read()

    def test_write_lists_every_file_sorted_with_hashes(self):
        version, n = rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertEqual((version, n), ("0.7.0", 4))
        man = json.loads(self.manifest())
        self.assertEqual((man["format"], man["version"], man["tag"], man["commit"]), (1, "0.7.0", "v0.7.0", COMMIT))
        paths = [e["path"] for e in man["files"]]
        self.assertEqual(paths, sorted(paths))
        self.assertEqual(set(paths), {"sc-offline.exe", "dinput8.dll", "data/items.txt", "data/OfflineDB/default_1.xml"})
        self.assertNotIn(rm.NAME, paths)
        e = next(e for e in man["files"] if e["path"] == "data/items.txt")
        self.assertEqual(e["sha256"], "58055bdcc73787eb88c78d36f0b4939e9c5dc1c3ad17e25cc85a6833cf1a0cab")  # sha256(b"a\r\nb\r\n")
        self.assertEqual(e["size"], 6)

    def test_external_inspector_files_are_covered(self):
        folder = os.path.join(self.dir, "data", "plugins", "inspector")
        os.makedirs(folder)
        with open(os.path.join(HERE, "..", "plugins", "inspector", "plugin.ini"), "rb") as f:
            self.put("data/plugins/inspector/plugin.ini", f.read())
        self.put("data/plugins/inspector/inspector.dll", b"synthetic module fixture, not a PE binary")
        _, count = rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertEqual(count, 6)
        paths = {e["path"] for e in json.loads(self.manifest())["files"]}
        self.assertIn("data/plugins/inspector/plugin.ini", paths)
        self.assertIn("data/plugins/inspector/inspector.dll", paths)
        self.assertNotIn("data/plugins/inspector/disabled", paths)
        self.assertEqual(rm.check(self.dir)[1], [])
        os.remove(os.path.join(folder, "inspector.dll"))
        self.assertEqual(rm.check(self.dir)[1], ["data/plugins/inspector/inspector.dll: missing"])

    def test_external_npc_files_are_covered(self):
        folder = os.path.join(self.dir, "data", "plugins", "npc")
        os.makedirs(folder)
        with open(os.path.join(HERE, "..", "plugins", "npc", "plugin.ini"), "rb") as f:
            self.put("data/plugins/npc/plugin.ini", f.read())
        self.put("data/plugins/npc/npc.dll", b"synthetic module fixture, not a PE binary")
        _, count = rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertEqual(count, 6)
        paths = {e["path"] for e in json.loads(self.manifest())["files"]}
        self.assertIn("data/plugins/npc/plugin.ini", paths)
        self.assertIn("data/plugins/npc/npc.dll", paths)
        self.assertNotIn("data/plugins/npc/disabled", paths)
        self.assertEqual(rm.check(self.dir)[1], [])
        os.remove(os.path.join(folder, "npc.dll"))
        self.assertEqual(rm.check(self.dir)[1], ["data/plugins/npc/npc.dll: missing"])

    def test_external_contracts_files_are_covered(self):
        folder = os.path.join(self.dir, "data", "plugins", "contracts")
        os.makedirs(folder)
        with open(os.path.join(HERE, "..", "plugins", "contracts", "plugin.ini"), "rb") as f:
            self.put("data/plugins/contracts/plugin.ini", f.read())
        self.put("data/plugins/contracts/contracts.dll", b"synthetic module fixture, not a PE binary")
        _, count = rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertEqual(count, 6)
        paths = {e["path"] for e in json.loads(self.manifest())["files"]}
        self.assertIn("data/plugins/contracts/plugin.ini", paths)
        self.assertIn("data/plugins/contracts/contracts.dll", paths)
        self.assertNotIn("data/plugins/contracts/disabled", paths)
        self.assertEqual(rm.check(self.dir)[1], [])
        os.remove(os.path.join(folder, "contracts.dll"))
        self.assertEqual(rm.check(self.dir)[1], ["data/plugins/contracts/contracts.dll: missing"])

    def test_tracked_docs_data_staging_excludes_fleet_test_fixture(self):
        # Mirror the release job's git ls-files docs/data copy, not a full release build.
        root = os.path.dirname(HERE)
        tracked = subprocess.check_output(
            ["git", "ls-files", "-z", "--", "docs", "data"], cwd=root
        ).decode("utf-8").split("\0")[:-1]
        self.assertTrue(tracked)
        self.assertIn("docs/build.md", tracked)
        for rel in tracked:
            dest = os.path.join(self.dir, *rel.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(os.path.join(root, *rel.split("/")), dest)
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        paths = {e["path"] for e in json.loads(self.manifest())["files"]}
        self.assertTrue(set(tracked).issubset(paths))
        self.assertNotIn("tools/test_fleet_identity.py", paths)
        self.assertFalse(any(p.startswith("tools/") for p in paths))
        archive = os.path.join(self.tmp.name, "release.zip")
        with zipfile.ZipFile(archive, "w") as z:
            for rel in sorted(paths | {rm.NAME}):
                z.write(os.path.join(self.dir, *rel.split("/")), rel)
        extracted = os.path.join(self.tmp.name, "roundtrip")
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(set(z.namelist()), paths | {rm.NAME})
            z.extractall(extracted)
        self.assertEqual(rm.check(extracted)[1], [])

    def test_one_file_per_line(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        file_lines = [l for l in self.manifest().splitlines() if '"path"' in l]
        self.assertEqual(len(file_lines), 4)
        for l in file_lines:
            self.assertRegex(l.strip(), r'^\{"path": "[^"]+", "sha256": "[0-9a-f]{64}", "size": \d+\},?$')

    def test_rewrite_ignores_old_manifest(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        _, n = rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertEqual(n, 4)

    def test_tag_must_match_version_h(self):
        self.version("0.6.1")
        with self.assertRaisesRegex(ValueError, "SCO_VERSION"):
            rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.assertFalse(os.path.exists(os.path.join(self.dir, rm.NAME)))

    def test_prerelease_tag(self):
        self.version("0.7.1-rc1")
        version, _ = rm.write(self.dir, "v0.7.1-rc1", COMMIT, self.vh)
        self.assertEqual(version, "0.7.1-rc1")

    def test_four_component_tag(self):
        self.version("0.7.8.1")
        version, _ = rm.write(self.dir, "v0.7.8.1", COMMIT, self.vh)
        self.assertEqual(version, "0.7.8.1")
        man = json.loads(self.manifest())
        self.assertEqual((man["version"], man["tag"]), ("0.7.8.1", "v0.7.8.1"))
        self.assertEqual(rm.check(self.dir)[1], [])
        self.version("0.7.8")
        with self.assertRaisesRegex(ValueError, "SCO_VERSION"):
            rm.write(self.dir, "v0.7.8.1", COMMIT, self.vh)

    def test_bad_tag_and_commit(self):
        for tag in ("0.7.0", "v0.7", "vfoo", "v0.7.0/x", "v0.7.8.1.2", "v0.7.8."):
            with self.assertRaises(ValueError):
                rm.write(self.dir, tag, COMMIT, None)
        with self.assertRaises(ValueError):
            rm.write(self.dir, "v0.7.0", "not-a-sha", None)

    def test_bad_paths(self):
        for rel in ("../x", "a/../b", "/abs", "C:/x", "a:stream", "a\\b", "a//b", "./a", "", 'a"b'):
            self.assertIsNotNone(rm.bad_path(rel), rel)
        for rel in ("sc-offline.exe", "data/OfflineDB/default_1.xml", "docs/launcher.md"):
            self.assertIsNone(rm.bad_path(rel), rel)

    def test_check_passes_on_untouched_folder(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        _, problems = rm.check(self.dir)
        self.assertEqual(problems, [])

    def test_check_refuses_tampered_file(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.put("dinput8.dll", b"MZ evil")
        _, problems = rm.check(self.dir)
        self.assertEqual(problems, ["dinput8.dll: SHA-256 differs"])

    def test_check_refuses_unlisted_and_missing_files(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        self.put("extra.dll", b"MZ")
        os.remove(os.path.join(self.dir, "data", "items.txt"))
        _, problems = rm.check(self.dir)
        self.assertEqual(problems, ["extra.dll: not in the manifest", "data/items.txt: missing"])

    def test_check_refuses_version_tag_mismatch_and_bad_paths(self):
        rm.write(self.dir, "v0.7.0", COMMIT, self.vh)
        man = json.loads(self.manifest())
        man["version"] = "0.6.1"
        man["files"].append({"path": "../evil.exe", "sha256": "0" * 64, "size": 1})
        with open(os.path.join(self.dir, rm.NAME), "w") as f:
            json.dump(man, f)
        _, problems = rm.check(self.dir)
        self.assertIn("version '0.6.1' doesn't match tag 'v0.7.0'", problems)
        self.assertTrue(any(p.startswith("'../evil.exe'") for p in problems), problems)

    def test_cli_exit_codes(self):
        self.assertEqual(rm.main(["write", self.dir, "--tag", "v0.7.0", "--commit", COMMIT, "--version-h", self.vh]), 0)
        self.assertEqual(rm.main(["check", self.dir]), 0)
        self.put("sc-offline.exe", b"MZ other")
        self.assertEqual(rm.main(["check", self.dir]), 1)
        self.assertEqual(rm.main(["write", self.dir, "--tag", "v9.9.9", "--version-h", self.vh]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
