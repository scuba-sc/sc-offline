#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for tools/check_release_gate.py and the way the release job uses it.

    python3 tools/test_release_gate.py (CI's check job does)
"""
import importlib.util
import io
import re
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
spec = importlib.util.spec_from_file_location("check_release_gate", HERE / "check_release_gate.py")
assert spec and spec.loader
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

FULL = "draft=false prerelease=false"
PRE = "draft=false prerelease=true"
DRAFT = "draft=true prerelease=false"


class ReleaseGate(unittest.TestCase):
    def ok(self, product, sdk, state=None, version="2.4.1"):
        return gate.check(product, sdk, version, state)

    def refused(self, product, sdk, state=None, version="2.4.1"):
        with self.assertRaises(gate.Refused) as caught:
            gate.check(product, sdk, version, state)
        return str(caught.exception)

    def test_a_final_product_tag_needs_the_final_published_sdk(self):
        self.assertEqual(self.ok("v0.8.0", "sdk-v2.4.1", FULL), "release")
        self.assertIn("is a pre-release", self.refused("v0.8.0", "sdk-v2.4.1", PRE))
        self.assertIn("draft", self.refused("v0.8.0", "sdk-v2.4.1", DRAFT))

    def test_a_final_product_tag_never_pins_an_sdk_candidate(self):
        message = self.refused("v0.8.0", "sdk-v2.4.1-rc.1")
        self.assertIn("final release but pins the SDK candidate", message)
        # not even when the candidate is published as a pre-release
        self.assertIn("final release but pins the SDK candidate", self.refused("v0.8.0", "sdk-v2.4.1-rc.1", PRE))
        self.assertIn("final release", self.refused("v0.8.1", "sdk-v2.4.1-rc.2", PRE))

    def test_a_candidate_product_tag_may_pin_a_published_sdk_pre_release(self):
        self.assertEqual(self.ok("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", PRE), "candidate")
        self.assertEqual(self.ok("v0.8.0-rc.2", "sdk-v2.4.1-rc.1", PRE), "candidate")
        self.assertEqual(self.ok("v0.8.0-rc.1", "sdk-v2.4.1-rc.12", PRE), "candidate")

    def test_a_candidate_product_tag_may_also_pin_the_final_sdk(self):
        self.assertEqual(self.ok("v0.8.0-rc.1", "sdk-v2.4.1", FULL), "release")
        self.assertIn("is a pre-release", self.refused("v0.8.0-rc.1", "sdk-v2.4.1", PRE))

    def test_an_sdk_candidate_must_be_a_pre_release_and_not_a_draft(self):
        self.assertIn("not marked as a pre-release", self.refused("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", FULL))
        self.assertIn("draft", self.refused("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", DRAFT))
        self.assertIn("draft", self.refused("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", "draft=true prerelease=true"))
        self.assertIn("cannot read", self.refused("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", ""))
        self.assertIn("cannot read", self.refused("v0.8.0-rc.1", "sdk-v2.4.1-rc.1", "draft=false"))

    def test_other_sdk_tags_are_refused(self):
        for sdk in ("sdk-v2.4.0", "sdk-v2.4.2", "sdk-v2.4.1-beta", "sdk-v2.4.1-rc", "sdk-v2.4.1-rc.", "sdk-v2.4.1-rc.1x",
                    "sdk-v2.4.10", "sdk-v2.4.1-rc.1-x", "v2.4.1", ""):
            self.assertIn("neither", self.refused("v0.8.0-rc.1", sdk, PRE), sdk)
        # a candidate of another SDK version
        self.assertIn("neither", self.refused("v0.8.0-rc.1", "sdk-v2.4.2-rc.1", PRE))

    def test_the_product_tag_must_be_a_version_tag(self):
        for tag in ("0.8.0", "v0.8", "main", "v0.8.0/x", "refs/tags/v0.8.0", ""):
            self.assertIn("product tag", self.refused(tag, "sdk-v2.4.1", FULL), tag)

    def test_what_counts_as_a_candidate_product_tag(self):
        for tag in ("v0.8.0-rc.1", "v1.0.0-rc.3", "v0.8.0.1-rc.2"):
            self.assertTrue(gate.product_is_candidate(tag), tag)
        for tag in ("v0.8.0", "v0.8.0-rc1", "v0.8.0-rc.x", "v0.8.0-dev241.2", "v0.8.0-rc"):
            self.assertFalse(gate.product_is_candidate(tag), tag)

    def test_tag_pairing_alone_needs_no_state(self):
        self.assertEqual(self.ok("v0.8.0-rc.1", "sdk-v2.4.1-rc.1"), "candidate")
        self.assertEqual(self.ok("v0.8.0", "sdk-v2.4.1"), "release")

    def test_command_line_exit_codes_and_output(self):
        def run(*argv):
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                code = gate.main(list(argv))
            return code, out.getvalue().strip(), err.getvalue().strip()

        self.assertEqual(run("--product-tag", "v0.8.0-rc.1", "--sdk-tag", "sdk-v2.4.1-rc.1", "--sdk-version", "2.4.1",
                             "--state", PRE), (0, "candidate", ""))
        code, out, err = run("--product-tag", "v0.8.0", "--sdk-tag", "sdk-v2.4.1-rc.1", "--sdk-version", "2.4.1")
        self.assertEqual((code, out), (1, ""))
        self.assertTrue(err.startswith("Not packaging:"), err)

    def test_the_release_job_runs_the_gate_before_fetching_and_again_with_the_state(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        job = workflow.split("\n  release:", 1)[1]
        step = job.split("name: Refuse packaging unless Core is a published SDK release", 1)[1].split("name: Download DLL", 1)[0]
        self.assertIn('python3 tools/bump_sdk_pin.py --print-tag', step)
        calls = [m.start() for m in re.finditer(r"tools/check_release_gate\.py", step)]
        self.assertEqual(len(calls), 2, "tag pairing first, release state second")
        self.assertLess(calls[0], step.index("git -C external/sco-core fetch"))
        self.assertGreater(calls[1], step.index("gh release view"))
        self.assertIn('--product-tag "$GITHUB_REF_NAME"', step)
        self.assertIn('--state "$state"', step)
        self.assertNotIn("sdk_tag=\"sdk-v$sdk_version\"", step)
        self.assertNotIn("draft=false prerelease=false\" ]", step)   # the state rule lives in the script now

    def test_the_release_job_publishes_a_candidate_as_a_pre_release_with_its_own_text(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        job = workflow.split("\n  release:", 1)[1]
        candidate = job.split("name: Create Release (release candidate)", 1)[1].split("name: Create Release\n", 1)[0]
        final = job.split("name: Create Release\n", 1)[1]
        self.assertIn("if: contains(github.ref_name, '-rc.')", candidate)
        self.assertIn("prerelease: true", candidate)
        self.assertIn("make_latest: false", candidate)
        for needle in ("Release candidate for testers", "stable update channel", "sc-offline-classic.exe uninstall", "0.7.7",
                       "Not run in game.", "mod.log", "Test first", "Report", "Go back"):
            self.assertIn(needle, candidate, needle)
        self.assertIn("if: ${{ !contains(github.ref_name, '-rc.') }}", final)
        self.assertIn("Every runtime path in a release is unproven", final)
        # the final step cannot mark an -rc. tag as a full release: v1.x candidates stay pre-releases
        self.assertIn("contains(github.ref_name, '-rc.')", final.split("prerelease:", 1)[1].split("\n", 1)[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
