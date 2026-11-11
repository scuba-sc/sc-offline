#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The copied tutorial must not depend on the machine's text encoding."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TutorialEncodingTest(unittest.TestCase):
    def test_prepare_copies_unicode_sources_with_ascii_locale(self):
        # On POSIX, the C locale exposes reliance on an implicit encoding.
        # Windows may retain a UTF-8 ACP, so that host alone is not a negative control.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, sums = root / 'sdk.zip', root / 'SHA256SUMS'
            archive.touch()
            sums.touch()
            env = dict(os.environ, LC_ALL='C', PYTHONUTF8='0', PYTHONCOERCECLOCALE='0')
            if os.name == 'posix':
                encoding = subprocess.check_output(
                    [sys.executable, '-X', 'utf8=0', '-c',
                     'import locale; print(locale.getpreferredencoding(False))'],
                    env=env, text=True, encoding='ascii').strip()
                self.assertIn(encoding.lower().replace('-', '').replace('_', ''),
                              ('ascii', 'ansix3.41968', 'usascii'))
            result = subprocess.run(
                [sys.executable, '-X', 'utf8=0', str(ROOT / 'tools/check_sdk_tutorial.py'),
                 '--archive', str(archive), '--sums', str(sums), '--work', str(root / 'copy'),
                 '--prepare-only'], env=env, text=True, encoding='utf-8',
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 0, result.stderr)
            copied = root / 'copy/owned-source/tools/check_tutorial_npc_sdk.py'
            self.assertTrue(copied.is_file())
            copied.read_text(encoding='utf-8')
            # Check the actual non-ASCII fixture survived the transformations,
            # including on machines whose default codepage accepts those bytes.
            spec = importlib.util.spec_from_file_location('tutorial', ROOT / 'tools/check_sdk_tutorial.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            original = (ROOT / 'plugins/inspector/inspector_locale.h').read_text(encoding='utf-8')
            self.assertTrue(any(ord(char) > 127 for char in original))
            translated = (root / 'copy/owned-source/plugins/tutorial_inspector/inspector_locale.h')
            self.assertEqual(translated.read_text(encoding='utf-8'), module.owned(original))


if __name__ == '__main__':
    unittest.main()
