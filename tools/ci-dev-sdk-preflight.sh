#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Reproducibly package the exact development Core selected by the product gitlink.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/.." && pwd -P)
core=$(cd "$1" && pwd -P)
out=$2
expected=$(git -C "$root" rev-parse HEAD:external/sco-core)
[ "$(git -C "$core" rev-parse HEAD)" = "$expected" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain --untracked-files=all)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
out=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$out")
for source in "$root" "$core"; do case "$out/" in "$source/"*) echo 'output must be outside source trees' >&2; exit 1;; esac; done
mkdir -p "$out"
# Local clone eliminates ignored/generated artifacts in the selected checkout.
git clone -q --no-hardlinks --no-checkout "$core" "$out/core"
git -C "$out/core" checkout -q --detach "$expected"
[ "$(git -C "$out/core" rev-parse HEAD)" = "$expected" ]
[ "$(git -C "$out/core" rev-parse 'HEAD^{tree}')" = "$(git -C "$core" rev-parse 'HEAD^{tree}')" ]
[ -f "$out/core/sdk/DEVELOPMENT_VERSION" ] || { echo 'Core lacks an explicit development package version' >&2; exit 1; }
version=$(tr -d '\r\n' < "$out/core/sdk/DEVELOPMENT_VERSION")
case "$version" in *-dev.*) ;; *) echo 'Core sdk/DEVELOPMENT_VERSION must identify a development build' >&2; exit 1;; esac
mkdir -p "$out/first" "$out/second" "$out/extracted"
export PYTHONDONTWRITEBYTECODE=1
python3 "$out/core/sdk/package.py" --development --profile legacy --out "$out/first"
python3 "$out/core/sdk/package.py" --development --profile legacy --out "$out/second"
archive="sco-sdk-$version.zip"
cmp "$out/first/$archive" "$out/second/$archive" || { echo 'development archive is not reproducible' >&2; exit 1; }
python3 "$out/core/tools/ip_audit.py" zip --development --profile legacy "$out/first/$archive"
(cd "$out/first" && sha256sum "$archive" > SHA256SUMS)
digest=$(sha256sum "$out/first/$archive" | cut -d' ' -f1)
# Verify archive inventory and all source/profile metadata using the same strict
# candidate path that individual isolated consumer proofs use.
python3 - "$root" "$out/first/$archive" "$out/first/SHA256SUMS" "$out/extracted" "$version" "$digest" "$expected" <<'PY'
import pathlib, sys
root, archive, sums, dest = map(pathlib.Path, sys.argv[1:5])
version, digest, commit = sys.argv[5:]
sys.path.insert(0, str(root / 'tools'))
from check_inspector_sdk import verify_extract
verify_extract(archive, sums, dest, ('sco-sdk-' + version, digest, commit, 'legacy'))
PY
printf '%s\n' "$expected" > "$out/core-source.sha"
printf '%s\n' "$version" > "$out/sdk-version.txt"
printf '%s\n' "$digest" > "$out/sdk-sha256.txt"
printf 'Development SDK: source=%s version=%s archive_sha256=%s\n' "$expected" "$version" "$digest"
