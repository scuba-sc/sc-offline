#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise identity refusals in disposable local clones; no compiler needed."""
import pathlib
import os
import shutil
import string
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
        # Upload-pack preserves shallow boundaries even when origin is a Git
        # worktree; local object-copy clones can lose them on Windows.
        result = run('git', 'clone', '--no-local', str(origin), str(target))
        if result.returncode:
            raise RuntimeError(result.stdout)
    commit, tree = git(off, 'rev-parse', 'HEAD'), git(off, 'rev-parse', 'HEAD^{tree}')
    runner = off / 'tools/transition-pair/run.sh'
    bash = (os.environ.get('SCO_PAIR_TEST_BASH') or shutil.which('bash')) if os.name == 'nt' else None
    if os.name == 'nt' and not bash:
        raise RuntimeError('Git Bash is required on Windows; set SCO_PAIR_TEST_BASH')
    def invoke(source, build, expected_commit=commit, expected_tree=tree, env=None):
        command = ([bash] if bash else []) + [str(runner), str(source), str(build),
                                               expected_commit, expected_tree, '--verify-only']
        return run(*command, env=env)
    def verify(source=src, expected_commit=commit, expected_tree=tree, ok=True, reason=''):
        result = invoke(source, base / 'unused', expected_commit, expected_tree)
        if (result.returncode == 0) != ok or (reason and reason not in result.stdout):
            raise AssertionError(result.stdout)
    verify()
    for source in (off, src):
        for candidate in (source / 'new-build', source / 'new-build' / 'nested'):
            paths = [str(candidate)]
            if os.name == 'nt':
                paths.append(str(candidate).swapcase())
            for path in paths:
                inside = invoke(src, path)
                if inside.returncode == 0 or 'outside both source trees' not in inside.stdout:
                    raise AssertionError(inside.stdout)
                if candidate.exists():
                    raise AssertionError('verify-only created an inside-source path')
            if os.name == 'nt':
                for key, value in (('MSYS_NO_PATHCONV', '1'), ('MSYS2_ARG_CONV_EXCL', '*')):
                    env = dict(os.environ, **{key: value})
                    result = invoke(src, candidate, env=env)
                    if (result.returncode == 0 or
                        'MSYS argument conversion must be enabled' not in result.stdout or
                        candidate.exists()):
                        raise AssertionError(result.stdout)
        # An existing source root itself must also be refused.
        if invoke(src, source).returncode == 0:
            raise AssertionError('source root accepted as build directory')
        sibling = source.with_name(source.name + '-sibling') / 'new-build'
        result = invoke(src, sibling)
        if result.returncode or sibling.exists():
            raise AssertionError(result.stdout)
        # Test the actual volume semantics rather than assuming that POSIX
        # paths are case-sensitive or that all case variants identify a root.
        case_root = source.with_name(source.name.upper())
        case_build = case_root / 'new-build'
        try:
            same_root = os.path.samefile(source, case_root)
        except FileNotFoundError:
            same_root = False
        result = invoke(src, case_build)
        if same_root:
            if result.returncode == 0 or 'outside both source trees' not in result.stdout:
                raise AssertionError(result.stdout)
        elif result.returncode:
            raise AssertionError(result.stdout)
        if case_build.exists():
            raise AssertionError('verify-only created a case-variant path')
        print('case-variant source verify-only:', 'refused' if same_root else 'distinct')
    if os.name == 'nt':
        # Check a different mounted drive without creating a path there.
        other_drive_tested = False
        for letter in string.ascii_uppercase:
            drive = pathlib.Path(f'{letter}:/')
            if drive.exists() and drive.drive.casefold() != base.drive.casefold():
                candidate = drive / ('sc-offline-pair-verify-' + base.name)
                if not candidate.exists():
                    result = invoke(src, candidate)
                    if result.returncode or candidate.exists():
                        raise AssertionError(result.stdout)
                    other_drive_tested = True
                    break
        print('different-drive verify-only: PASS' if other_drive_tested else
              'SKIP different-drive verify-only: no suitable mounted drive')
        print('SKIP symlink fixture on Windows')
    else:
        alias = base / 'source-link'
        alias.symlink_to(off, target_is_directory=True)
        inside = invoke(src, alias / 'new-build')
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
