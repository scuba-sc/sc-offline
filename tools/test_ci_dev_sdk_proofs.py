#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Negative controls for development SDK proof identity admission."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
pin = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD:external/sco-core'], text=True).strip()
with tempfile.TemporaryDirectory(prefix='offline-dev-proof-test-') as temp:
    base = Path(temp)
    assets = base / 'assets'
    (assets / 'first').mkdir(parents=True)
    (assets / 'sdk-version.txt').write_text('2.4.1-dev.1\n')
    (assets / 'sdk-sha256.txt').write_text('0' * 64 + '\n')
    (assets / 'core-source.sha').write_text(pin + '\n')
    (assets / 'first' / 'sco-sdk-2.4.1-dev.1.zip').write_bytes(b'not a real SDK')
    def refuse(message):
        result = subprocess.run([sys.executable, str(root / 'tools/ci-dev-sdk-proofs.py'),
                                 str(assets), str(base / 'proofs')], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode == 0 or message not in result.stdout or (base / 'proofs').exists():
            raise AssertionError(result.stdout)
    refuse('development SDK archive digest mismatch')
    (assets / 'core-source.sha').write_text('0' * 40 + '\n')
    refuse('development SDK identity does not match product pin')
    (assets / 'sdk-version.txt').write_text('2.4.0\n')
    refuse('invalid development SDK version')
print('development SDK proof identity refusals: PASS')
