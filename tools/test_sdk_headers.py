#!/usr/bin/env python3
"""Tests for tools/sdk-headers.py's SDK header list. Run: python3 tools/test_sdk_headers.py (CI's sdk-headers job does).

Covers both sources the gate reads (sco-core's sdk/components.json and the legacy CONTENT table in
sdk/package.py) and that every unreadable form fails closed.
"""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("sdk_headers", HERE / "sdk-headers.py")
assert spec and spec.loader
sh = importlib.util.module_from_spec(spec)
sys.argv = [sys.argv[0]]
spec.loader.exec_module(sh)


def manifest(components, profiles=("core", "game-sc")):
    return {"schema_version": 1, "profiles": {p: {} for p in profiles}, "components": components}


GOOD = [
    {"id": "core-abi", "source": "include/sco_api.h", "destination": "include/sco_api.h",
     "profiles": ["core", "game-sc"], "public_header": True},
    {"id": "core-cpp", "source": "include/scosdk", "destination": "include/scosdk",
     "profiles": ["core", "game-sc"], "public_header": True},
    {"id": "game-actors", "source": "include/sc_actors.h", "destination": "include/sc_actors.h",
     "profiles": ["game-sc"], "public_header": True},
    {"id": "imgui", "source": "sdk/third_party/imgui", "destination": "third_party/imgui",
     "profiles": ["core"], "public_header": True, "files": ["imgui.h", "imconfig.h", "LICENSE.txt"]},
    {"id": "docs", "source": "sdk/docs", "destination": "docs", "profiles": ["core"]},
    {"id": "bridge", "source": "include/sc_titanlink.h", "destination": "include/sc_titanlink.h",
     "profiles": ["bridges"], "public_header": True},
]


class Manifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "components.json"

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, data):
        self.path.write_text(data if isinstance(data, str) else json.dumps(data), encoding="utf-8")

    def test_union_of_profiles(self):
        self.write(manifest(GOOD + [{"id": "x", "source": "s", "destination": "d", "profiles": ["bridges"]}],
                            ("core", "game-sc", "bridges")))
        top, dirs, imgui = sh.headers_from_manifest(self.path)
        self.assertEqual(top, {"sco_api.h", "sc_actors.h"})   # game-sc counts, bridges doesn't
        self.assertEqual(dirs, {"scosdk"})
        self.assertEqual(imgui, {"imgui.h", "imconfig.h"})

    def test_non_public_components_ignored(self):
        self.write(manifest(GOOD))
        top, _, _ = sh.headers_from_manifest(self.path)
        self.assertNotIn("docs", top)
        self.assertNotIn("sc_titanlink.h", top)

    def test_core_only_excludes_game(self):
        self.write(manifest(GOOD))
        top, _, _ = sh.headers_from_manifest(self.path, profiles=("core",))
        self.assertNotIn("sc_actors.h", top)

    def fails(self, data, why):
        self.write(data)
        with self.assertRaises(sh.CheckError) as cm:
            sh.headers_from_manifest(self.path)
        self.assertIn(why, str(cm.exception))

    def test_malformed_json(self):
        self.fails("{not json", "can't read it")

    def test_wrong_schema(self):
        self.fails({**manifest(GOOD), "schema_version": 2}, "schema_version must be 1")

    def test_no_components(self):
        self.fails(manifest([]), "no components")

    def test_profile_not_declared(self):
        self.fails(manifest(GOOD, ("core",)), "game-sc not declared")

    def test_component_missing_fields(self):
        self.fails(manifest(GOOD + [{"id": "half", "profiles": ["core"]}]), "needs id, destination and profiles")

    def test_imgui_without_files(self):
        bad = [dict(c) for c in GOOD]
        del bad[3]["files"]
        self.fails(manifest(bad), "lists its headers in 'files'")

    def test_public_header_outside_include(self):
        self.fails(manifest(GOOD + [{"id": "odd", "source": "a", "destination": "tools/a.h",
                                     "profiles": ["core"], "public_header": True}]), "outside include/")

    def test_empty_selection(self):
        self.fails(manifest([GOOD[4]]), "declare no headers")


LEGACY = '''CONTENT = [
    ("include/sco_api.h", "include/sco_api.h"),
    ("include/sc_actors.h", "include/sc_actors.h"),
    ("include/scosdk", "include/scosdk"),
    ("sdk/third_party/imgui", "third_party/imgui"),
]
'''


class Legacy(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "imgui").mkdir()
        (self.dir / "imgui" / "imgui.h").write_text("", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_reads_content(self):
        p = self.dir / "package.py"
        p.write_text("import os\n" + LEGACY + "def main(): pass\n", encoding="utf-8")
        top, dirs, imgui = sh.headers_from_package(p, self.dir / "imgui")
        self.assertEqual(top, {"sco_api.h", "sc_actors.h"})
        self.assertEqual(dirs, {"scosdk"})
        self.assertEqual(imgui, {"imgui.h"})

    def test_ignores_include_outside_content(self):
        p = self.dir / "package.py"
        p.write_text(LEGACY + 'X = [("include/sco/internal.h", "x")]\n', encoding="utf-8")
        _, dirs, _ = sh.headers_from_package(p, self.dir / "imgui")
        self.assertNotIn("sco", dirs)

    def test_no_content_fails(self):
        p = self.dir / "package.py"
        p.write_text("PROFILES = {}\n", encoding="utf-8")
        with self.assertRaises(sh.CheckError):
            sh.headers_from_package(p, self.dir / "imgui")


class Gate(unittest.TestCase):
    """The real gate on this checkout exits 0, and a broken manifest makes it exit 2, never 0."""

    def test_this_checkout(self):
        r = subprocess.run([sys.executable, str(HERE / "sdk-headers.py")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_broken_manifest_exits_2(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            (root / "src" / "builtins").mkdir(parents=True)
            (root / "src" / "builtins" / "a.cpp").write_text('#include "sco_api.h"\n', encoding="utf-8")
            sdk = root / "external" / "sco-core" / "sdk"
            sdk.mkdir(parents=True)
            (sdk / "components.json").write_text("[]", encoding="utf-8")
            (sdk / "package.py").write_text(LEGACY, encoding="utf-8")   # must NOT be used as a fallback
            r = subprocess.run([sys.executable, str(HERE / "sdk-headers.py"), str(root)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 2, r.stdout)
            self.assertIn("schema_version must be 1", r.stdout)

    def test_missing_both_exits_2(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            (root / "src" / "builtins").mkdir(parents=True)
            (root / "src" / "builtins" / "a.cpp").write_text("", encoding="utf-8")
            r = subprocess.run([sys.executable, str(HERE / "sdk-headers.py"), str(root)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 2, r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
