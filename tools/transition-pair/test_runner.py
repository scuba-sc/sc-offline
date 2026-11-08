#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise identity refusals in disposable local clones; no compiler needed."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) != 2:
    raise SystemExit('usage: test_runner.py CORE_SOURCE')
core = pathlib.Path(sys.argv[1]).resolve()

def run(*args, **kwargs):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, **kwargs)

def git(repo, *args):
    result = run('git', '-C', str(repo), *args)
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout.strip()

with tempfile.TemporaryDirectory(prefix='sc-offline-pair-test-') as tmp:
    base = pathlib.Path(tmp)
    off, src = base / 'offline', base / 'core'
    for origin, target in ((root, off), (core, src)):
        result = run('git', 'clone', '--no-hardlinks', str(origin), str(target))
        if result.returncode:
            raise RuntimeError(result.stdout)
    commit, tree = git(off, 'rev-parse', 'HEAD'), git(off, 'rev-parse', 'HEAD^{tree}')
    runner = off / 'tools/transition-pair/run.sh'
    def verify(source=src, expected_commit=commit, expected_tree=tree, ok=True, reason=''):
        result = run(str(runner), str(source), str(base / 'unused'), expected_commit,
                     expected_tree, '--verify-only')
        if (result.returncode == 0) != ok or (reason and reason not in result.stdout):
            raise AssertionError(result.stdout)
    verify()
    inside = run(str(runner), str(src), str(off / 'new-build'), commit, tree, '--verify-only')
    if inside.returncode == 0 or 'outside both source trees' not in inside.stdout:
        raise AssertionError(inside.stdout)
    alias = base / 'source-link'
    alias.symlink_to(off, target_is_directory=True)
    inside = run(str(runner), str(src), str(alias / 'new-build'), commit, tree, '--verify-only')
    if inside.returncode == 0 or 'outside both source trees' not in inside.stdout:
        raise AssertionError(inside.stdout)
    verify(expected_commit='0' * 40, ok=False, reason='offline commit mismatch')
    verify(expected_tree='0' * 40, ok=False, reason='offline tree mismatch')
    verify(source=off, ok=False, reason='Core commit mismatch')
    tracked = src / 'CMakeLists.txt'
    original = tracked.read_bytes()
    tracked.write_bytes(original + b'\n# dirty test\n')
    verify(ok=False, reason='source has tracked or untracked changes')
    tracked.write_bytes(original)
    (src / 'unexpected-untracked').write_text('test')
    verify(ok=False, reason='source has tracked or untracked changes')
    (src / 'unexpected-untracked').unlink()
    tracked = off / 'src/offline_host.cpp'
    original = tracked.read_bytes()
    tracked.write_bytes(original + b'\n// dirty test\n')
    verify(ok=False, reason='source has tracked or untracked changes')
    tracked.write_bytes(original)
    (off / 'build').mkdir()
    (off / 'build' / 'ignored-test').write_text('test')
    verify(ok=False, reason='ignored files present in source')
    print('paired source identity/dirty-source rejection: PASS')
