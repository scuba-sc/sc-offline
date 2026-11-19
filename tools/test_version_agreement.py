#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The product version lives in several places; they must agree, and for a release candidate the tag must too.

Run: python3 tools/test_version_agreement.py (CI's check job does).

- src/version.h: SCO_VERSION ("0.8.0-rc.1"), the three numbers (0.8.0) and SCO_VERSION_PRERELEASE, which is
  defined exactly when SCO_VERSION has a "-" suffix.
- launcher-rs/Cargo.toml workspace version, and every scol-* package in Cargo.lock.
- scol-install's SCO_VERSION constant.
- The release tag, when RELEASE_TAG (or GITHUB_REF_NAME on a tag run) names one: v<SCO_VERSION>, so a
  release-candidate tag v0.8.0-rc.1 needs SCO_VERSION and the Cargo version "0.8.0-rc.1" and a final tag v0.8.0
  needs both without a suffix. tools/release-manifest.py and tools/package_release.py refuse a tag that differs.

Version-order rules of the two launchers are tested in their own code: crates/scol-update (Rust) and
tools/launcher_update_version_test.cpp (C++).
"""
import os
import re
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+(?:\.\d+)?(?:-[0-9A-Za-z.]+)?)$")


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def define(text, name):
    m = re.search(rf"^#define\s+{name}\s+(.+?)\s*$", text, re.M)
    return m.group(1).strip().strip('"') if m else None


class VersionAgreement(unittest.TestCase):
    def setUp(self):
        self.header = read("src/version.h")
        self.version = define(self.header, "SCO_VERSION")

    def test_version_h_is_internally_consistent(self):
        self.assertTrue(self.version and TAG_RE.match("v" + self.version), self.version)
        numbers = ".".join(define(self.header, f"SCO_VERSION_{n}") for n in ("MAJOR", "MINOR", "PATCH"))
        self.assertEqual(self.version.split("-", 1)[0], numbers, "the numeric fields are the version without its suffix")
        has_suffix = "-" in self.version
        self.assertEqual(define(self.header, "SCO_VERSION_PRERELEASE") is not None, has_suffix,
                         "SCO_VERSION_PRERELEASE is defined exactly for a pre-release version")

    def test_version_resource_flags_a_pre_release(self):
        rc = read("src/version.rc")
        self.assertIn("VS_FF_PRERELEASE", rc)
        self.assertIn("SCO_VERSION_PRERELEASE", rc)

    def test_cargo_workspace_and_lock_carry_the_same_version(self):
        manifest = read("launcher-rs/Cargo.toml")
        m = re.search(r"^\[workspace\.package\]\s*\n(?:.*\n)*?version\s*=\s*\"([^\"]+)\"", manifest, re.M)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), self.version)
        lock = read("launcher-rs/Cargo.lock")
        found = re.findall(r'^name = "(scol-[a-z-]+)"\s*\nversion = "([^"]+)"', lock, re.M)
        self.assertTrue(found, "no scol-* packages in Cargo.lock")
        self.assertEqual({v for _, v in found}, {self.version}, found)

    def test_scol_install_constant_matches(self):
        text = read("launcher-rs/crates/scol-install/src/version.rs")
        m = re.search(r'pub const SCO_VERSION: &str = "([^"]+)";', text)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), self.version)

    def test_the_tag_names_this_version(self):
        tag = os.environ.get("RELEASE_TAG", "")
        if not tag and os.environ.get("GITHUB_REF_TYPE") == "tag":
            tag = os.environ.get("GITHUB_REF_NAME", "")
        if not tag:
            self.skipTest("no release tag named (RELEASE_TAG)")
        m = TAG_RE.match(tag)
        self.assertIsNotNone(m, f"tag {tag!r} is not v<number>.<number>.<number>[-suffix]")
        self.assertEqual(m.group(1), self.version, f"tag {tag} but src/version.h says {self.version}")
        if "-rc." in tag:
            self.assertIn("-rc.", self.version)
            self.assertIsNotNone(define(self.header, "SCO_VERSION_PRERELEASE"))


if __name__ == "__main__":
    unittest.main()
