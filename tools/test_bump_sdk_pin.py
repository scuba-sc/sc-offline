#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for tools/bump_sdk_pin.py on a synthetic tree. It never touches this repository.

    python3 tools/test_bump_sdk_pin.py
"""
import importlib.util
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bump_sdk_pin", HERE / "bump_sdk_pin.py")
assert spec and spec.loader
bump = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bump)

OLD_COMBINED = "0" * 63 + "1"
OLD_CORE_SRC = "0" * 63 + "2"
OLD_HOST_SRC = "0" * 63 + "3"
NEW_COMBINED = "a" * 63 + "1"
NEW_CORE_SRC = "a" * 63 + "2"
NEW_HOST_SRC = "a" * 63 + "3"
OLD_COMMIT = "1" * 40
NEW_COMMIT = "2" * 40

INSPECTOR = f"""# Published stable SDK 2.4.0; gameplay acceptance remains separate.
SDK_VERSION = '2.4.0'
SDK_ROOT = f'sco-sdk-{{SDK_VERSION}}'
SDK_SHA256 = '{OLD_COMBINED}'
help_text = 'Exact SDK archive version (default: published stable 2.4.0)'
if sdk_root == 'sco-sdk-2.4.0':
    pass
"""
WORKFLOW = f"""name: build
on:
  workflow_dispatch:
    inputs:
      qualify:
        description: 'Qualify the published sco-core SDK 2.4.0 release'
jobs:
  external-sdk:
    env:
      SDK_VERSION: '2.4.0'
      SDK_TAG: 'sdk-v2.4.0'
    steps:
      - run: gh release download sdk-v2.4.0 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
  stable-host-package:
    steps:
      - run: |
          test "$(git rev-parse HEAD)" = {OLD_COMMIT}
          python3 - <<'PY'
          expected = {{
              'sco-sdk-2.4.0.zip': '{OLD_COMBINED}',
              'sco-sdk-core-source-2.4.0.zip': '{OLD_CORE_SRC}',
              'sco-sdk-host-source-sc-2.4.0.zip': '{OLD_HOST_SRC}',
          }}
          assert release['tag'] == 'sdk-v2.4.0'
          assert release['source']['sha'] == '{OLD_COMMIT}'
          assert release['builder']['run_id'] == 4242
          assert data['core_sdk_version'] == '2.4.0'
          PY
          unzip -q sco-sdk-host-source-sc-2.4.0.zip
"""
PREFLIGHT = f"""#!/usr/bin/env bash
# Verify the exact published SDK 2.4.0 package.
expected_source={OLD_COMMIT}
expected_archive={OLD_COMBINED}
unzip -q "$out/assets/sco-sdk-2.4.0.zip"
"""
WORKFLOWS_DOC = f"""# SDK workflows
Use the published SDK 2.4.0: tag `sdk-v2.4.0`, archive `sco-sdk-2.4.0.zip`. SDK 2.4.0, plugin ABI 1.1 and game pack 0.8.0 differ.
The archive's SHA-256 is `{OLD_COMBINED}`. See the [2.4.0 release](https://example.com/releases/tag/sdk-v2.4.0).
Game overlay `sco-sdk-game-sc-0.8.0.zip`.
"""
TEST_FILE = """# fixtures with made-up versions stay as they are
archive = root / 'sco-sdk-2.5.0.zip'
self.assertEqual(explicit_preflight_pin(args()), ('sco-sdk-2.4.0', SDK_SHA256))
self.assertEqual(args.sdk_version, '2.4.0')
"""
CMAKE = "# Loadout headers ship in the pinned sdk-v2.4.0; release staging is pending.\n"
HISTORY = "The 2.4.0 release, sdk-v2.4.0, was built from commit " + OLD_COMMIT + ".\n"


def put(root: Path, rel: str, text: str, newline: str = "\n") -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))


def sums_text(version: str = "2.4.1") -> str:
    return (f"{NEW_COMBINED}  sco-sdk-{version}.zip\n{NEW_CORE_SRC}  sco-sdk-core-source-{version}.zip\n"
            f"{NEW_HOST_SRC} *sco-sdk-host-source-sc-{version}.zip\n{'b' * 64}  sco-sdk-core-{version}.zip\n")


class BumpSdkPin(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "repo"
        put(self.root, "tools/check_inspector_sdk.py", INSPECTOR)
        put(self.root, "tools/check_npc_sdk.py", '"""Proof against published stable SDK 2.4.0."""\n')
        put(self.root, ".github/workflows/build.yml", WORKFLOW, "\r\n")
        put(self.root, "tools/ci-stable-sdk-preflight.sh", PREFLIGHT)
        put(self.root, "docs/sdk-workflows.md", WORKFLOWS_DOC)
        put(self.root, "tools/test_sdk_generators.py", TEST_FILE)
        put(self.root, "plugins/loadout/CMakeLists.txt", CMAKE)
        put(self.root, "docs/handoff/2026-10-30-note.md", HISTORY)
        put(self.root, "CHANGELOG.md", HISTORY)
        self.sums = Path(self.tmp.name) / "SHA256SUMS"
        self.sums.write_text(sums_text(), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = bump.main(["--root", str(self.root), *argv])
        return code, out.getvalue(), err.getvalue()

    def target(self, *extra):
        return ["--version", "2.4.1", "--tag", "sdk-v2.4.1", "--game-pack", "0.8.1", "--commit", NEW_COMMIT,
                "--run-id", "9999", "--sha256sums", str(self.sums), *extra]

    def read(self, rel):
        return (self.root / rel).read_bytes().decode("utf-8")

    def test_reads_the_current_pins_from_the_repository(self):
        ref = bump.current_pins(self.root)
        self.assertEqual(ref["version"], "2.4.0")
        self.assertEqual(ref["hashes"]["sco-sdk-{v}.zip"], OLD_COMBINED)
        self.assertEqual(ref["hashes"]["sco-sdk-core-source-{v}.zip"], OLD_CORE_SRC)
        self.assertEqual((ref["commit"], ref["run_id"], ref["game_pack"]), (OLD_COMMIT, "4242", "0.8.0"))

    def test_a_consistent_tree_has_no_stale_pins(self):
        code, out, _ = self.run_main("--check")
        self.assertEqual(code, 0, out)
        self.assertIn("every pin is at SDK 2.4.0", out)

    def test_check_reports_every_stale_pin_and_changes_nothing(self):
        before = self.read(".github/workflows/build.yml")
        code, out, _ = self.run_main("--check", *self.target())
        self.assertEqual(code, 1)
        self.assertIn("stale .github/workflows/build.yml:", out)
        self.assertIn("tools/check_inspector_sdk.py:2: 2.4.0 -> 2.4.1", out)
        self.assertEqual(self.read(".github/workflows/build.yml"), before)

    def test_bump_rewrites_every_pin_and_keeps_line_endings(self):
        code, out, err = self.run_main(*self.target())
        self.assertEqual(code, 0, out + err)
        inspector = self.read("tools/check_inspector_sdk.py")
        self.assertIn("SDK_VERSION = '2.4.1'", inspector)
        self.assertIn(f"SDK_SHA256 = '{NEW_COMBINED}'", inspector)
        self.assertIn("sdk_root == 'sco-sdk-2.4.1'", inspector)
        self.assertIn("Published stable SDK 2.4.1;", inspector)
        self.assertIn("(default: published stable 2.4.1)", inspector)
        self.assertIn("published stable SDK 2.4.1", self.read("tools/check_npc_sdk.py"))
        workflow = self.read(".github/workflows/build.yml")
        self.assertIn("\r\n", workflow)                       # CRLF stays CRLF
        self.assertNotIn("\n", workflow.replace("\r\n", ""))
        for text in ("SDK_VERSION: '2.4.1'", "SDK_TAG: 'sdk-v2.4.1'", "gh release download sdk-v2.4.1",
                     "'sco-sdk-2.4.1.zip'", f"'sco-sdk-core-source-2.4.1.zip': '{NEW_CORE_SRC}'",
                     f"'sco-sdk-host-source-sc-2.4.1.zip': '{NEW_HOST_SRC}'", f"['source']['sha'] == '{NEW_COMMIT}'",
                     "['builder']['run_id'] == 9999", "['core_sdk_version'] == '2.4.1'", "release['tag'] == 'sdk-v2.4.1'",
                     "published sco-core SDK 2.4.1 release", f"= {NEW_COMMIT}"):
            self.assertIn(text, workflow)
        for old in ("2.4.0", OLD_COMMIT, OLD_COMBINED, OLD_CORE_SRC, OLD_HOST_SRC, "4242"):
            self.assertNotIn(old, workflow)
        preflight = self.read("tools/ci-stable-sdk-preflight.sh")
        self.assertIn(f"expected_source={NEW_COMMIT}", preflight)
        self.assertIn(f"expected_archive={NEW_COMBINED}", preflight)
        self.assertIn("sco-sdk-2.4.1.zip", preflight)
        doc = self.read("docs/sdk-workflows.md")
        for text in ("published SDK 2.4.1", "`sdk-v2.4.1`", "game pack 0.8.1", "sco-sdk-game-sc-0.8.1.zip",
                     f"`{NEW_COMBINED}`", "[2.4.1 release]"):
            self.assertIn(text, doc)
        self.assertNotIn("0.8.0", doc)
        self.assertIn("pinned sdk-v2.4.1", self.read("plugins/loadout/CMakeLists.txt"))
        self.assertIn("follow-up: docs/sdk-candidate.md needs", out)
        # a second run changes nothing, and --check agrees
        code, out2, _ = self.run_main(*self.target())
        self.assertEqual(code, 0)
        self.assertIn("0 pin(s) changed", out2)
        self.assertEqual(self.run_main("--check", *self.target())[0], 0)
        self.assertEqual(self.run_main("--check")[0], 0)

    def test_test_fixtures_with_made_up_versions_and_history_are_left_alone(self):
        self.assertEqual(self.run_main(*self.target())[0], 0)
        tests = self.read("tools/test_sdk_generators.py")
        self.assertIn("'sco-sdk-2.5.0.zip'", tests)
        self.assertIn("('sco-sdk-2.4.1', SDK_SHA256)", tests)
        self.assertIn("args.sdk_version, '2.4.1'", tests)
        self.assertEqual(self.read("docs/handoff/2026-10-30-note.md"), HISTORY)
        self.assertEqual(self.read("CHANGELOG.md"), HISTORY)

    def test_follow_ups_name_prose_that_still_has_the_old_version(self):
        put(self.root, "docs/sdk-workflows.md", WORKFLOWS_DOC + "The 2.4.0 archive does not certify that pairing.\n")
        _, out, _ = self.run_main(*self.target())
        self.assertIn("docs/sdk-workflows.md:5: still names 2.4.0", out)
        self.assertIn("sco-sdk-2.4.1.zip  " + NEW_COMBINED, out)

    def test_check_flags_a_partly_moved_tree(self):
        self.assertEqual(self.run_main(*self.target())[0], 0)
        put(self.root, "tools/check_npc_sdk.py", '"""Proof against published stable SDK 2.4.0."""\n')
        code, out, _ = self.run_main("--check")
        self.assertEqual(code, 1)
        self.assertIn("stale tools/check_npc_sdk.py:1: 2.4.0 -> 2.4.1", out)

    def test_release_json_supplies_tag_commit_run_id_game_pack_and_hashes(self):
        import json
        release = Path(self.tmp.name) / "release.json"
        release.write_text(json.dumps({
            "tag": "sdk-v2.4.1", "source": {"repo": "scuba-sc/sco-core", "sha": NEW_COMMIT}, "builder": {"run_id": 7777},
            "artifacts": [{"name": "sco-sdk-2.4.1.zip", "sha256": NEW_COMBINED},
                          {"name": "sco-sdk-core-source-2.4.1.zip", "sha256": NEW_CORE_SRC},
                          {"name": "sco-sdk-host-source-sc-2.4.1.zip", "sha256": NEW_HOST_SRC},
                          {"name": "sco-sdk-game-sc-0.8.1.zip", "sha256": "c" * 64}]}), encoding="utf-8")
        code, out, err = self.run_main("--release-json", str(release))
        self.assertEqual(code, 0, out + err)
        self.assertIn("['builder']['run_id'] == 7777", self.read(".github/workflows/build.yml"))
        self.assertIn("game pack 0.8.1", self.read("docs/sdk-workflows.md"))

    def test_refuses_incomplete_or_inconsistent_input(self):
        before = self.read(".github/workflows/build.yml")
        # no hash for a new archive
        code, _, err = self.run_main("--version", "2.4.1", "--game-pack", "0.8.1", "--commit", NEW_COMMIT, "--run-id", "1")
        self.assertEqual(code, 2)
        self.assertIn("no SHA-256 for sco-sdk-2.4.1.zip", err)
        # tag and version disagree
        code, _, err = self.run_main(*self.target()[:2], "--tag", "sdk-v2.4.2", *self.target()[4:])
        self.assertEqual(code, 2)
        self.assertIn("does not match", err)
        # the commit is needed because the repository still holds the old one
        code, _, err = self.run_main("--version", "2.4.1", "--game-pack", "0.8.1", "--run-id", "1", "--sha256sums", str(self.sums))
        self.assertEqual(code, 2)
        self.assertIn("--commit is required", err)
        # a bad hash
        code, _, err = self.run_main(*self.target("--sha256", "sco-sdk-2.4.1.zip=nothex"))
        self.assertEqual(code, 2)
        self.assertEqual(self.read(".github/workflows/build.yml"), before)

    def test_sha256sums_parser_takes_both_separators(self):
        parsed = bump.parse_sums(sums_text())
        self.assertEqual(parsed["sco-sdk-host-source-sc-2.4.1.zip"], NEW_HOST_SRC)
        self.assertEqual(parsed["sco-sdk-2.4.1.zip"], NEW_COMBINED)

    def test_the_real_repository_is_consistent(self):
        """Read-only: the repository's own pins agree with each other (this is the --check of the CI tests)."""
        out = io.StringIO()
        with redirect_stdout(out):
            code = bump.main(["--check"])
        self.assertEqual(code, 0, out.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
