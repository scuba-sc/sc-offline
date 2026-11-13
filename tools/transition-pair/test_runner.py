#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise exact-pair identity refusals in isolated local clones."""
import os
import pathlib
import shutil
import string
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) != 6:
    raise SystemExit(
        'usage: test_runner.py CORE_SOURCE OFFLINE_COMMIT OFFLINE_TREE CORE_COMMIT CORE_TREE'
    )
core_source = pathlib.Path(sys.argv[1]).resolve()
offline_commit, offline_tree, core_commit, core_tree = sys.argv[2:]
identities = (
    ('offline commit', offline_commit),
    ('offline tree', offline_tree),
    ('Core commit', core_commit),
    ('Core tree', core_tree),
)


def run(*args, **kwargs):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, **kwargs)


def git(repo, *args):
    result = run('git', '-C', str(repo), *args)
    if result.returncode:
        raise RuntimeError(f'git -C {repo} {args!r} failed:\n{result.stdout}')
    return result.stdout.strip()


for name, value in identities:
    if len(value) != 40 or any(char not in '0123456789abcdef' for char in value):
        raise SystemExit(f'{name} must be a full lowercase Git SHA-1 from the reviewed identity record')

# The four expectations are caller supplied. Cross-check them against the
# immutable input objects before creating fixtures; never derive expectations
# from a moving ref or an uncommitted candidate checkout.
actual_offline_commit = git(root, 'rev-parse', 'HEAD')
actual_offline_tree = git(root, 'rev-parse', 'HEAD^{tree}')
actual_gitlink = git(root, 'rev-parse', 'HEAD:external/sco-core')
actual_core_commit = git(core_source, 'rev-parse', 'HEAD')
actual_core_tree = git(core_source, 'rev-parse', 'HEAD^{tree}')
actuals = (actual_offline_commit, actual_offline_tree, actual_core_commit, actual_core_tree)
expected = (offline_commit, offline_tree, core_commit, core_tree)
if actuals != expected or actual_gitlink != core_commit:
    raise SystemExit(
        'supplied reviewed identity record does not match the selected Offline/Core objects '
        f'(offline={actual_offline_commit} tree={actual_offline_tree}; '
        f'gitlink={actual_gitlink}; core={actual_core_commit} tree={actual_core_tree})'
    )

anchor = '259ee6284d4c37f2eafa32f668ff29f4bc266844'
if run('git', '-C', str(root), 'cat-file', '-e', anchor).returncode:
    raise SystemExit('Offline ancestry anchor is unavailable locally; no fetch attempted')
ancestor = run('git', '-C', str(root), 'merge-base', '--is-ancestor', anchor, 'HEAD')
if ancestor.returncode:
    raise SystemExit('Offline ancestry anchor is not confirmed locally; no fetch attempted')

# Only the three owned source files may be dirty while this test is run. This
# prevents the fixture clone from silently dropping unrelated user work.
owned = {
    'tools/transition-pair/run.sh',
    'tools/transition-pair/test_runner.py',
    'tools/transition-pair/README.md',
}
status_result = run('git', '-C', str(root), 'status', '--porcelain', '--untracked-files=all')
if status_result.returncode:
    raise SystemExit(status_result.stdout)
changed = {line[3:] for line in status_result.stdout.splitlines() if len(line) >= 4}
if changed - owned:
    raise SystemExit('unrelated dirty or untracked files prevent isolated test clone: '
                     + ', '.join(sorted(changed - owned)))

runner_source = root / 'tools/transition-pair/run.sh'
test_source = pathlib.Path(__file__).resolve()
if os.name == 'nt':
    bash = os.environ.get('SCO_PAIR_TEST_BASH')
    if not bash:
        candidates = (
            pathlib.Path('C:/Program Files/Git/bin/bash.exe'),
            pathlib.Path('C:/Program Files (x86)/Git/bin/bash.exe'),
        )
        bash = next((str(candidate) for candidate in candidates if candidate.is_file()), None)
    if not bash:
        bash = shutil.which('bash')
else:
    bash = None
if os.name == 'nt' and not bash:
    raise SystemExit('Git Bash is required on Windows; set SCO_PAIR_TEST_BASH')

with tempfile.TemporaryDirectory(prefix='sc-offline-pair-test-') as tmp:
    base = pathlib.Path(tmp).resolve()
    off, src = base / 'offline', base / 'core'
    for origin, target in ((root, off), (core_source, src)):
        # Local upload-pack keeps the test independent of network access and
        # preserves shallow boundaries more reliably than object-copy clones.
        result = run('git', 'clone', '--no-local', str(origin), str(target))
        if result.returncode:
            raise RuntimeError(result.stdout)

    if run('git', '-C', str(off), 'cat-file', '-e', anchor).returncode:
        raise SystemExit('isolated Offline clone lacks the ancestry anchor; no fetch attempted')
    copied_ancestor = run('git', '-C', str(off), 'merge-base', '--is-ancestor', anchor, 'HEAD')
    if copied_ancestor.returncode:
        raise SystemExit('isolated Offline clone lacks the ancestry anchor; no fetch attempted')
    if git(src, 'rev-parse', 'HEAD') != core_commit or git(src, 'rev-parse', 'HEAD^{tree}') != core_tree:
        raise AssertionError('isolated Core clone changed the reviewed identity')

    # The runner under test is a worktree edit. Copy only the owned runner and
    # test into this disposable clone, then make a clearly labelled fixture
    # commit so the production cleanliness gate stays enabled during tests.
    (off / 'tools/transition-pair').mkdir(parents=True, exist_ok=True)
    shutil.copy2(runner_source, off / 'tools/transition-pair/run.sh')
    shutil.copy2(test_source, off / 'tools/transition-pair/test_runner.py')
    shutil.copy2(root / 'tools/transition-pair/README.md',
                 off / 'tools/transition-pair/README.md')
    git(off, 'config', 'user.name', 'transition-pair test fixture')
    git(off, 'config', 'user.email', 'transition-pair-fixture@example.invalid')
    git(off, 'add', 'tools/transition-pair/run.sh', 'tools/transition-pair/test_runner.py',
        'tools/transition-pair/README.md')
    result = run('git', '-C', str(off), 'commit', '-m', '[TEST FIXTURE ONLY] exercise pair runner')
    if result.returncode:
        raise RuntimeError(result.stdout)
    fixture_commit = git(off, 'rev-parse', 'HEAD')
    fixture_tree = git(off, 'rev-parse', 'HEAD^{tree}')
    runner = off / 'tools/transition-pair/run.sh'

    def invoke(source, build, expected_offline=fixture_commit,
               expected_offline_tree=fixture_tree, expected_core=core_commit,
               expected_core_tree=core_tree, mode='--verify-only', env=None,
               args_override=None, runner_path=None):
        args = [str(source), str(build), expected_offline, expected_offline_tree,
                expected_core, expected_core_tree]
        if args_override is not None:
            args = args_override
        command = ([bash] if bash else []) + [str(runner_path or runner), *args]
        if mode:
            command.append(mode)
        return run(*command, env=env)

    def verify(source=src, expected_offline=fixture_commit,
               expected_offline_tree=fixture_tree, expected_core=core_commit,
               expected_core_tree=core_tree, ok=True, reason='', build=None):
        build = build or base / 'unused-build'
        result = invoke(source, build, expected_offline, expected_offline_tree,
                        expected_core, expected_core_tree)
        if (result.returncode == 0) != ok or (reason and reason not in result.stdout):
            raise AssertionError(result.stdout)
        if build.exists():
            raise AssertionError('verify-only created a build path')
        return result

    positive = verify()
    receipt = (
        f'offline={fixture_commit} tree={fixture_tree}',
        f'core={core_commit} tree={core_tree}',
        f'development_gitlink={core_commit}',
    )
    if any(line not in positive.stdout for line in receipt):
        raise AssertionError(f'missing exact identity receipt:\n{positive.stdout}')
    print(f'reviewed_offline={offline_commit} tree={offline_tree}')
    print(f'reviewed_core={core_commit} tree={core_tree}')
    print(f'test_fixture_offline={fixture_commit} tree={fixture_tree}')
    print('paired source verify-only fixture: PASS (temporary Offline commit)')

    missing_core = invoke(src, base / 'missing-build', fixture_commit, fixture_tree,
                          mode='', args_override=[str(src), str(base / 'missing-build'),
                                                  fixture_commit, fixture_tree])
    if missing_core.returncode != 2 or 'usage:' not in missing_core.stdout:
        raise AssertionError(missing_core.stdout)
    print('missing required Core identities: refused')

    for index, (name, bad_value) in enumerate((
            ('offline commit', 'a' * 39),
            ('offline tree', 'A' * 40),
            ('Core commit', 'c591513'),
            ('Core tree', 'F' * 40))):
        values = [str(src), str(base / 'invalid-build'), fixture_commit, fixture_tree,
                  core_commit, core_tree]
        values[2 + index] = bad_value
        result = invoke(src, base / 'invalid-build', mode='', args_override=values)
        expected_message = f'expected full lowercase Git SHA-1 for {name}'
        if result.returncode != 2 or expected_message not in result.stdout:
            raise AssertionError(result.stdout)
        if (base / 'invalid-build').exists():
            raise AssertionError('invalid identity created a build path')
    print('malformed, short and uppercase identity arguments: refused')

    verify(expected_offline='0' * 40, ok=False, reason='offline commit mismatch')
    verify(expected_offline_tree='0' * 40, ok=False, reason='offline tree mismatch')
    verify(expected_core='0' * 40, ok=False, reason='development offline gitlink mismatch')
    verify(expected_core_tree='0' * 40, ok=False, reason='Core tree mismatch')
    verify(source=off, ok=False, reason='Core commit mismatch')
    print('wrong Offline/Core commit, tree and gitlink identities: refused')

    # A minimal unrelated repository makes the ancestry refusal deterministic
    # without graft replacement, network access, or mutation of the input.
    unrelated = base / 'unrelated-offline'
    (unrelated / 'tools/transition-pair').mkdir(parents=True)
    shutil.copy2(runner_source, unrelated / 'tools/transition-pair/run.sh')
    run('git', 'init', '-q', str(unrelated), check=True)
    git(unrelated, 'config', 'user.name', 'transition-pair test fixture')
    git(unrelated, 'config', 'user.email', 'transition-pair-fixture@example.invalid')
    git(unrelated, 'add', 'tools/transition-pair/run.sh')
    git(unrelated, 'update-index', '--add', '--cacheinfo',
        f'160000,{core_commit},external/sco-core')
    result = run('git', '-C', str(unrelated), 'commit', '-m', '[TEST FIXTURE ONLY] unrelated ancestry')
    if result.returncode:
        raise RuntimeError(result.stdout)
    (unrelated / 'external').mkdir(exist_ok=True)
    result = run('git', 'clone', '--no-local', str(src),
                 str(unrelated / 'external/sco-core'))
    if result.returncode:
        raise RuntimeError(result.stdout)
    unrelated_commit = git(unrelated, 'rev-parse', 'HEAD')
    unrelated_tree = git(unrelated, 'rev-parse', 'HEAD^{tree}')
    unrelated_runner = unrelated / 'tools/transition-pair/run.sh'
    ancestry_result = invoke(src, base / 'unrelated-build', unrelated_commit,
                             unrelated_tree, core_commit, core_tree,
                             mode='--verify-only', runner_path=unrelated_runner)
    if (ancestry_result.returncode == 0 or
            'offline candidate ancestry mismatch' not in ancestry_result.stdout):
        raise AssertionError(ancestry_result.stdout)
    if (base / 'unrelated-build').exists():
        raise AssertionError('ancestry refusal created a build path')
    print('wrong Offline ancestry: refused')

    for source in (off, src):
        for candidate in (source / 'new-build', source / 'new-build' / 'nested'):
            paths = [str(candidate)]
            if os.name == 'nt':
                paths.append(str(candidate).swapcase())
            for path in paths:
                result = invoke(src, pathlib.Path(path))
                if result.returncode == 0 or 'outside both source trees' not in result.stdout:
                    raise AssertionError(result.stdout)
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
        if invoke(src, source).returncode == 0:
            raise AssertionError('source root accepted as build directory')
        sibling = source.with_name(source.name + '-sibling') / 'new-build'
        result = invoke(src, sibling)
        if result.returncode or sibling.exists():
            raise AssertionError(result.stdout)
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

    if os.name == 'nt':
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
        result = invoke(src, alias / 'new-build')
        if result.returncode == 0 or 'outside both source trees' not in result.stdout:
            raise AssertionError(result.stdout)

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
    (off / 'unexpected-untracked').write_text('test')
    verify(ok=False, reason='source has tracked or untracked changes')
    (off / 'unexpected-untracked').unlink()

    (off / 'build').mkdir()
    (off / 'build' / 'ignored-test').write_text('test')
    verify(ok=False, reason='ignored files present in source')
    shutil.rmtree(off / 'build')
    print('dirty, untracked and ignored source files: refused')

    if git(off, 'status', '--porcelain', '--untracked-files=all'):
        raise AssertionError('test mutations were not restored in the isolated clone')
    if git(src, 'status', '--porcelain', '--untracked-files=all'):
        raise AssertionError('test mutations were not restored in the isolated Core clone')
    print('paired source identity/dirty-source rejection: PASS')
