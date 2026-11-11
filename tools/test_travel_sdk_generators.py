#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generator selection remains explicit for the shared travel SDK proof."""
import unittest
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import check_teleport_sdk as travel
from check_quantum_sdk import QUANTUM


class TravelGeneratorSelection(unittest.TestCase):
    def test_ninja_is_rejected_before_any_proof_work(self):
        with patch.object(travel, 'prove') as prove:
            with self.assertRaises(SystemExit) as raised:
                travel.main(['--archive', 'sdk.zip', '--sums', 'SHA256SUMS', '--work', 'proof',
                             '--generator', 'Ninja'], spec=QUANTUM)
            self.assertEqual(raised.exception.code, 2)
            prove.assert_not_called()

    def test_default_and_explicit_override_preserve_proof_spec(self):
        for spec in (travel.TELEPORT, QUANTUM):
            for requested in (None, 'Visual Studio 17 2022'):
                with self.subTest(feature=spec['feature'], generator=requested):
                    argv = ['--archive', 'sdk.zip', '--sums', 'SHA256SUMS', '--work', 'proof']
                    if requested:
                        argv += ['--generator', requested]
                    with patch.object(travel, 'prove') as prove:
                        travel.main(argv, spec=spec)
                    args, actual_spec = prove.call_args.args
                    self.assertEqual(args.generator, requested or 'Visual Studio 18 2026')
                    self.assertIs(actual_spec, spec)

    def test_actual_first_configure_uses_selected_generator(self):
        class Captured(Exception):
            pass

        for spec in (travel.TELEPORT, QUANTUM):
            for requested in (None, 'Visual Studio 17 2022'):
                with self.subTest(feature=spec['feature'], generator=requested), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    argv = ['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                            '--work', str(root / 'proof')]
                    if requested:
                        argv += ['--generator', requested]
                    command = []
                    def capture(args, cwd):
                        command.extend(str(value) for value in args)
                        raise Captured()
                    with patch.object(travel, 'os', SimpleNamespace(name='nt')), \
                         patch.object(travel, 'verify_extract', return_value=(root / 'sdk', set())), \
                         patch.object(travel, 'run', side_effect=capture), self.assertRaises(Captured):
                        travel.main(argv, spec=spec)
                    self.assertEqual(command[command.index('-G') + 1], requested or 'Visual Studio 18 2026')
                    self.assertEqual(command[command.index('-A') + 1], 'x64')

if __name__ == '__main__':
    unittest.main()
