#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for tools/package_release.py on a synthetic source tree and synthetic build outputs.

    python3 tools/test_package_release.py

No game, no real binaries, no network: the "exes" are small byte strings. This proves the folder
layout, the manifest and the zip round trip; it does not prove that any binary works.
"""
import importlib.util
import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
spec = importlib.util.spec_from_file_location("package_release", HERE / "package_release.py")
assert spec and spec.loader
pr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pr)

COMMIT = "0123456789abcdef0123456789abcdef01234567"
PROFILE = {
    "schema": 1,
    "profiles": {
        "synthetic": {
            "plugins": [
                {"id": "alpha", "artifact_dir": "built/alpha", "module": "alpha.dll", "disabled": False},
                {"id": "beta", "artifact_dir": "built/beta", "module": "beta.dll", "disabled": True},
            ],
            "excluded": {"gamma": "synthetic reason"},
        }
    },
}


def put(path: Path, data=b"x"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())


class PackageRelease(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.src = base / "source"
        self.out = base / "out"
        self.art = base / "artifacts"
        self.bin = base / "bin"
        put(self.src / "src/version.h", '#pragma once\n#define SCO_VERSION  "0.8.0"\n')
        put(self.src / "launcher/sc-offline.ini", "# ini\nmenu = imgui\n")
        put(self.src / "launcher/sc-offline.sh", "#!/bin/sh\n")
        for name in ("README.md", "CHANGELOG.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
            put(self.src / name, name)
        put(self.src / "docs/launcher.md", "docs")
        put(self.src / "data/items.txt", "items")
        put(self.src / "data/ships.txt", "ships")
        put(self.src / "data/npcs.txt", "npcs")
        put(self.src / "data/locations.txt", "locations")
        put(self.src / "data/buildables.txt", "player-owned buildables")
        put(self.src / "data/plugins/build_catalog/plugin.ini", "id = build_catalog\nkind = data\n")
        for plugin, disabled in (("alpha", False), ("beta", True)):
            folder = self.art / "built" / plugin
            manifest = f"id = {plugin}\nkind = native\nentry = {plugin}.dll\n"
            put(folder / "plugin.ini", manifest)
            # The product keeps each plugin's manifest (and its disabled marker) under data/plugins.
            put(self.src / "data/plugins" / plugin / "plugin.ini", manifest)
            if disabled:
                put(self.src / "data/plugins" / plugin / "disabled", "")
            put(folder / f"{plugin}.dll", f"module {plugin}")
            if disabled:
                put(folder / "disabled", "")
        put(self.bin / "rust.exe", b"MZ rust launcher")
        put(self.bin / "classic.exe", b"MZ classic launcher")
        put(self.bin / "menu.dll", b"MZ rust menu")
        put(self.bin / "dinput8.dll", b"MZ mod")
        self.profile = base / "profiles.json"
        self.profile.write_text(json.dumps(PROFILE), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def argv(self, *extra, tag="v0.8.0"):
        return ["--tag", tag, "--out", str(self.out), "--source", str(self.src),
                "--rust-exe", str(self.bin / "rust.exe"), "--classic-exe", str(self.bin / "classic.exe"),
                "--menu-dll", str(self.bin / "menu.dll"), "--dinput8", str(self.bin / "dinput8.dll"),
                "--artifact-root", str(self.art), "--profile", "synthetic", "--profile-file", str(self.profile),
                "--commit", COMMIT, *extra]

    def pkg(self):
        return self.out / "sc-offline-v0.8.0"

    def test_layout_manifest_and_zip(self):
        self.assertEqual(pr.main(self.argv()), 0)
        pkg = self.pkg()
        self.assertEqual((pkg / "sc-offline.exe").read_bytes(), b"MZ rust launcher")
        self.assertEqual((pkg / "sc-offline-classic.exe").read_bytes(), b"MZ classic launcher")
        self.assertEqual((pkg / "sc_offline_menu.dll").read_bytes(), b"MZ rust menu")
        self.assertEqual((pkg / "dinput8.dll").read_bytes(), b"MZ mod")
        for name in ("sc-offline.ini", "sc-offline.sh", "README.md", "CHANGELOG.md", "LICENSE",
                     "THIRD_PARTY_NOTICES.md", "docs/launcher.md", "data/items.txt", "manifest.json"):
            self.assertTrue((pkg / name).is_file(), name)
        # The legacy list is player-owned: only the build_catalog pack carries it in the release.
        self.assertFalse((pkg / "data/buildables.txt").exists())
        self.assertEqual((pkg / "data/plugins/build_catalog/lists/buildables.txt").read_bytes(), b"player-owned buildables")
        self.assertEqual((pkg / "data/plugins/picker_catalog/lists/ships.txt").read_bytes(), b"ships")
        self.assertEqual((pkg / "data/plugins/picker_catalog/lists/npcs.txt").read_bytes(), b"npcs")
        self.assertEqual((pkg / "data/plugins/locations_catalog/lists/locations.txt").read_bytes(), b"locations")
        self.assertTrue((pkg / "data/plugins/alpha/alpha.dll").is_file())
        self.assertFalse((pkg / "data/plugins/alpha/disabled").exists())
        self.assertTrue((pkg / "data/plugins/beta/disabled").is_file())
        self.assertFalse((pkg / "data/plugins/gamma").exists())
        manifest = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual((manifest["format"], manifest["version"], manifest["tag"], manifest["commit"]),
                         (1, "0.8.0", "v0.8.0", COMMIT))
        listed = {entry["path"] for entry in manifest["files"]}
        for name in ("sc-offline.exe", "sc-offline-classic.exe", "sc_offline_menu.dll", "dinput8.dll"):
            self.assertIn(name, listed)
        self.assertNotIn("manifest.json", listed)
        archive = self.out / "sc-offline-v0.8.0.zip"
        with zipfile.ZipFile(archive) as z:
            names = set(z.namelist())
            self.assertEqual(names, {f"sc-offline-v0.8.0/{p}" for p in listed | {"manifest.json"}})
            mode = z.getinfo("sc-offline-v0.8.0/sc-offline.sh").external_attr >> 16
            self.assertEqual(mode & 0o111, 0o111)

    def test_no_zip_builds_the_folder_only(self):
        self.assertEqual(pr.main(self.argv("--no-zip")), 0)
        self.assertTrue(self.pkg().is_dir())
        self.assertFalse((self.out / "sc-offline-v0.8.0.zip").exists())

    def test_tag_must_match_version_h_and_leaves_nothing_behind(self):
        self.assertEqual(pr.main(self.argv(tag="v0.8.1")), 1)
        self.assertFalse((self.out / "sc-offline-v0.8.1").exists())

    def test_bad_tag_missing_binary_and_existing_folder(self):
        self.assertEqual(pr.main(self.argv(tag="0.8.0")), 1)
        (self.bin / "classic.exe").unlink()
        self.assertEqual(pr.main(self.argv()), 1)
        self.assertFalse(self.pkg().exists())
        put(self.bin / "classic.exe", b"MZ classic launcher")
        self.assertEqual(pr.main(self.argv()), 0)
        self.assertEqual(pr.main(self.argv()), 1)  # the folder exists now

    def test_empty_binary_is_refused(self):
        put(self.bin / "menu.dll", b"")
        self.assertEqual(pr.main(self.argv()), 1)
        self.assertFalse(self.pkg().exists())

    def test_plugin_disabled_marker_mismatch_is_refused(self):
        (self.art / "built/beta/disabled").unlink()
        self.assertEqual(pr.main(self.argv()), 1)
        self.assertFalse(self.pkg().exists())

    def test_no_plugins_still_stages_data_packs(self):
        self.assertEqual(pr.main(self.argv("--no-plugins", "--no-zip")), 0)
        self.assertFalse((self.pkg() / "data/plugins/alpha/alpha.dll").exists())
        self.assertTrue((self.pkg() / "data/plugins/build_catalog/plugin.ini").is_file())

    def test_bad_commit_is_refused(self):
        argv = self.argv()
        argv[argv.index("--commit") + 1] = "not-a-sha"
        self.assertEqual(pr.main(argv), 1)

    def test_release_job_uses_the_script_with_the_same_file_set(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        step = workflow.split("name: Package sc-offline-<tag>.zip", 1)[1].split("name: Create Release", 1)[0]
        for token in ("tools/package_release.py", "--rust-exe", "--classic-exe", "--menu-dll", "--dinput8",
                      "--check-tree", "rust-launcher-release"):
            self.assertTrue(token in step or token in workflow, token)
        self.assertEqual([name for _, name in pr.BINARIES],
                         ["sc-offline.exe", "sc-offline-classic.exe", "sc_offline_menu.dll", "dinput8.dll"])

    def test_repository_has_every_top_level_file_the_package_copies(self):
        for src, _ in pr.TOP_FILES:
            self.assertTrue((ROOT / src).is_file(), src)

    def test_real_data_staging_matches_the_tracked_files(self):
        """Stage the real docs/ and data/ of this checkout: the buildables list moves, nothing else is lost."""
        out = Path(self.tmp.name) / "real"
        pr.stage_data(ROOT, out)
        tracked = pr.tracked(ROOT, "data", exclude=(pr.LEGACY_BUILDABLES,))
        for rel in tracked:
            self.assertTrue((out / rel).is_file(), rel)
        self.assertFalse((out / "data/buildables.txt").exists())
        self.assertTrue((out / "data/plugins/build_catalog/lists/buildables.txt").is_file())
        self.assertTrue((out / "docs/launcher.md").is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
