#!/usr/bin/env python3
"""Focused exact ImGui pin gate tests; no game or compiler required."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / 'tools/imgui-pin.py'


class ImGuiPinTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('imgui_pin', SCRIPT)
        assert spec is not None and spec.loader is not None
        self.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gate)

    def fixture(self):
        temp = tempfile.TemporaryDirectory(prefix='imgui-pin-')
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        for rel in ['src/third_party/imgui', 'external/sco-core/sdk/third_party/imgui']:
            shutil.copytree(ROOT / rel, root / rel)
        return root

    def test_gate_exists(self):
        self.assertTrue(SCRIPT.is_file(), 'exact source pin gate must exist')

    def test_real_host_matches_sdk_and_pin(self):
        self.assertEqual(self.gate.check(ROOT), [])

    def test_every_host_input_is_pinned_even_with_unchanged_version_label(self):
        root = self.fixture()
        for name in self.gate.SHA256:
            with self.subTest(name=name):
                path = root / 'src/third_party/imgui' / name
                original = path.read_bytes()
                # Same version macros, different source/configuration bytes.
                path.write_bytes(original + b'\n// pin drift\n')
                self.assertIn(f'{name}: differs from exact host source pin '
                              f'{self.gate.SOURCE_REVISION}', self.gate.check(root))
                path.write_bytes(original)
        self.assertEqual(self.gate.check(root), [])

    def test_mutually_matching_drift_cannot_bless_a_new_pin(self):
        root = self.fixture()
        for name in self.gate.PUBLIC_HEADERS:
            with self.subTest(name=name):
                host = root / 'src/third_party/imgui' / name
                sdk = root / 'external/sco-core/sdk/third_party/imgui' / name
                original = host.read_bytes()
                host.write_bytes(original + b'\n// both copies drift\n')
                sdk.write_bytes(host.read_bytes())
                errors = self.gate.check(root)
                self.assertTrue(any('exact host source pin' in e for e in errors))
                self.assertFalse(any('host/SDK mismatch' in e for e in errors))
                host.write_bytes(original)
                sdk.write_bytes(original)

    def test_sdk_only_drift_is_rejected(self):
        root = self.fixture()
        for name in self.gate.PUBLIC_HEADERS:
            with self.subTest(name=name):
                path = root / 'external/sco-core/sdk/third_party/imgui' / name
                original = path.read_bytes()
                path.write_bytes(original + b'\n// SDK drift\n')
                self.assertIn(f'{name}: host/SDK mismatch', self.gate.check(root))
                path.write_bytes(original)

    def test_missing_host_input_fails_closed(self):
        root = self.fixture()
        for name in self.gate.SHA256:
            with self.subTest(name=name):
                path = root / 'src/third_party/imgui' / name
                original = path.read_bytes()
                path.unlink()
                self.assertIn(f'{name}: missing/unreadable host input', self.gate.check(root))
                path.write_bytes(original)

    def test_missing_sdk_input_fails_closed(self):
        root = self.fixture()
        for name in self.gate.PUBLIC_HEADERS:
            with self.subTest(name=name):
                path = root / 'external/sco-core/sdk/third_party/imgui' / name
                original = path.read_bytes()
                path.unlink()
                self.assertTrue(any(e.startswith(f'{name}: missing/unreadable SDK input')
                                    for e in self.gate.check(root)))
                path.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
