#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run synthetic quantum modules concurrently with one shared temp root."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 5:
        raise SystemExit('expected workflow, content, gamerules executables and module')
    executables, module = sys.argv[1:4], sys.argv[4]
    with tempfile.TemporaryDirectory(prefix='quantum-concurrency-') as parent:
        temp = Path(parent)
        # The old fixed directories must remain untouched, not be deleted as cleanup.
        sentinels = []
        for name in ('workflow', 'content', 'gamerules'):
            directory = temp / ('sco-quantum-' + name + '-test')
            directory.mkdir()
            marker = directory / 'keep'
            marker.write_text(name, encoding='utf-8')
            sentinels.append((marker, name))
        env = dict(os.environ, TMPDIR=parent, TMP=parent, TEMP=parent)
        running = [subprocess.Popen([exe, module], stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, env=env)
                   for exe in executables for _ in range(2)]
        try:
            for process in running:
                output, _ = process.communicate(timeout=120)
                if process.returncode:
                    raise AssertionError('concurrent quantum fixture failed:\n' +
                                         output.decode('utf-8', errors='replace'))
        finally:
            for process in running:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        for marker, expected in sentinels:
            if not marker.is_file() or marker.read_text(encoding='utf-8') != expected:
                raise AssertionError('fixture modified a pre-existing fixed-name directory')
        remaining = {path.name for path in temp.iterdir()}
        expected = {'sco-quantum-' + name + '-test' for name in ('workflow', 'content', 'gamerules')}
        if remaining != expected:
            raise AssertionError('fixture failed to clean its owned directories: ' + repr(remaining - expected))
    print('quantum concurrent shared-temp fixtures: PASS (six processes; no game)')


if __name__ == '__main__':
    main()
