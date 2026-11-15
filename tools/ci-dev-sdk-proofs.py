#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run isolated consumer proofs against the selected development SDK artifact."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
if len(sys.argv) != 3:
    raise SystemExit('usage: ci-dev-sdk-proofs.py SDK_ASSETS NEW_WORK_DIR')
assets = Path(sys.argv[1]).resolve()
work = Path(sys.argv[2]).resolve()
if work.exists() or work == ROOT or ROOT in work.parents:
    raise SystemExit('proof directory must be new and outside the source tree')
version = (assets / 'sdk-version.txt').read_text().strip()
digest = (assets / 'sdk-sha256.txt').read_text().strip()
commit = (assets / 'core-source.sha').read_text().strip()
pinned = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse',
                                  'HEAD:external/sco-core'], text=True).strip()
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-dev\.[0-9]+', version):
    raise SystemExit('invalid development SDK version')
if not re.fullmatch(r'[0-9a-f]{64}', digest) or commit != pinned:
    raise SystemExit('development SDK identity does not match product pin')
archive = assets / 'first' / ('sco-sdk-' + version + '.zip')
sums = assets / 'first' / 'SHA256SUMS'
if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
    raise SystemExit('development SDK archive digest mismatch')
work.mkdir(parents=True)
features = ('npc', 'inspector', 'loadout', 'creative', 'discord', 'crew',
            'spawn', 'build', 'mining', 'teleport', 'quantum', 'contracts')
for feature in features:
    script = ROOT / 'tools' / ('contracts_sdk_check.py' if feature == 'contracts' else f'check_{feature}_sdk.py')
    if feature == 'creative':
        # Creative is its own repository, carried as the external/sc-creative submodule.
        script = ROOT / 'external' / 'sc-creative' / 'tools' / 'check_creative_sdk.py'
        if not script.is_file():
            raise SystemExit('creative proof missing: git submodule update --init external/sc-creative')
    command = [sys.executable, str(script), '--archive', str(archive),
               '--sums', str(sums), '--work', str(work / feature), '--candidate',
               '--sdk-version', version, '--sdk-sha256', digest, '--archive-sha256', digest,
               '--sdk-source', commit, '--sdk-profile', 'legacy']
    if feature == 'contracts':
        command += ['--slice', 'read']
    print(f'Running {feature} against development Core {commit}', flush=True)
    subprocess.run(command, check=True)

# The Loadout SDK proof above extracts the archive only after checking its digest,
# checksum inventory, source commit, profile, and current product gitlink. Reuse
# that extraction for the actual list-pack install staging test so it exercises
# the same locked development SDK on both CI operating systems.
sdk_include = work / 'loadout' / 'sdk' / ('sco-sdk-' + version) / 'include'
if not (sdk_include / 'scosdk').is_dir():
    raise SystemExit(f'verified Loadout SDK include directory is missing: {sdk_include}')
staging_command = [sys.executable, str(ROOT / 'tools' / 'test_loadout_list_staging.py'),
                   '--sdk-include', str(sdk_include)]
if os.name == 'nt':
    staging_command += ['--generator', 'Visual Studio 18 2026', '--architecture', 'x64']
else:
    staging_command += ['--generator', 'Unix Makefiles']
print(f'Running Loadout list staging against development Core {commit}', flush=True)
subprocess.run(staging_command, check=True)
print('Development SDK consumer proofs passed; no native/game acceptance implied.')
