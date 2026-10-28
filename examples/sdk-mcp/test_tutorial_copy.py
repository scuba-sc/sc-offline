#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regression test for the independent tutorial source copy."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TutorialCopyTest(unittest.TestCase):
    def test_prepare_copies_shared_header_and_feature_includes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Prepare-only checks existence but intentionally does not verify or use these files.
            archive, sums = root / 'archive.zip', root / 'SHA256SUMS'
            archive.touch()
            sums.touch()
            work = root / 'tutorial'
            subprocess.run([sys.executable, str(ROOT / 'tools/check_sdk_tutorial.py'),
                            '--archive', str(archive), '--sums', str(sums),
                            '--work', str(work), '--prepare-only'], check=True,
                           capture_output=True, text=True)
            source = work / 'owned-source'
            self.assertEqual((source / 'LICENSE').read_bytes(), (ROOT / 'LICENSE').read_bytes())
            self.assertEqual((source / 'plugins/picker.h').read_bytes(),
                             (ROOT / 'plugins/picker.h').read_bytes())
            for feature in ('npc', 'inspector'):
                for inc in (ROOT / 'plugins' / feature).glob('*.inc'):
                    self.assertTrue((source / 'plugins' / ('tutorial_' + feature) / inc.name).is_file(),
                                    inc.name)
            self.assertTrue((source / 'plugins/tutorial_npc/picker.inc').is_file())


if __name__ == '__main__':
    unittest.main()
