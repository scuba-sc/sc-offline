#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Narrow release wiring regression for the external command-first NPC plugin."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent


class NpcRelease(unittest.TestCase):
    def test_no_host_registration_or_feature_linkage(self):
        host = (ROOT / 'CMakeLists.txt').read_text()
        builtins = (ROOT / 'src/builtins/builtins.h').read_text()
        self.assertNotIn('kNpcBuiltin', builtins)
        self.assertNotIn('sco-feature-npc', host)
        self.assertNotIn('plugins/npc/', host)
        self.assertNotIn('workflow_adapters.cpp', host)
        self.assertNotIn('SCO_EXTERNAL_WORKFLOWS', host + builtins)
        self.assertIn('src/npc.cpp', host)  # Legacy callers have not migrated.
        self.assertNotIn('kCrewBuiltin', builtins)

    def test_command_first_retires_shared_picker_after_crew(self):
        for path in ('src/builtins/npc_ui.cpp', 'src/builtins/crew_ui.cpp'):
            self.assertFalse((ROOT / path).exists())
        self.assertNotIn('NpcPicker', (ROOT / 'src/builtins/tabs.h').read_text())

    def test_binary_gate_does_not_reject_retained_legacy_identifiers(self):
        workflow = (ROOT / '.github/workflows/build.yml').read_text()
        legacy = (ROOT / 'src/npc.cpp').read_text()
        loop = re.search(r"foreach \(\$cmd in ([^)]*)\) \{\s*Check \(-not \$text.Contains", workflow)
        self.assertIsNotNone(loop)
        gated = re.findall(r"'([^']+)'", loop.group(1))
        literals = re.findall(r'"([^"\n]+)"', legacy)
        for command in gated:
            self.assertFalse(any(command in literal for literal in literals),
                             f'{command} collides with retained legacy code, not a registration')

    def test_enabled_manifest_and_release_stage(self):
        manifest = (ROOT / 'plugins/npc/plugin.ini').read_text()
        for key, value in (('id', 'npc'), ('version', '1.1.0'), ('api', '1.1'), ('entry', 'npc.dll')):
            self.assertRegex(manifest, rf'(?m)^{key}\s*=\s*{re.escape(value)}\s*$')
        self.assertFalse((ROOT / 'plugins/npc/disabled').exists())
        cmake = (ROOT / 'plugins/CMakeLists.txt').read_text()
        self.assertIn('data/plugins/${feature}', cmake)
        self.assertNotIn('SCO_HOST_INCLUDE_DIR', cmake)
        workflow = (ROOT / '.github/workflows/build.yml').read_text()
        self.assertIn('npc-sdk-posix:', workflow)
        self.assertIn('npc-sdk:', workflow)
        self.assertIn('needs: [build, inspector-sdk, npc-sdk, external-sdk]', workflow)
        self.assertIn('built-npc/npc/plugin.ini built-npc/npc/npc.dll', workflow)
        self.assertIn('test ! -e "stage/$pkg/data/plugins/npc/disabled"', workflow)


if __name__ == '__main__':
    unittest.main(verbosity=2)
