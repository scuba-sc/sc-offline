#!/usr/bin/env bash
# Coordinated RC3 source candidate for syntax, module and package checks.
# This does not change the published external-plugin SDK default.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
core=$(cd "$1" && pwd)
out=$2
expected_source=44ffd1290e261e12684b38ac862ec0c4f08921df
expected_archive=bcd5c946ed8658214571d95bb79df594c9ea83748f63869d1963ef1fa77f51d9
[ "$(git -C "$core" rev-parse HEAD)" = "$expected_source" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
mkdir -p "$out/assets" "$out/extracted"
python3 "$core/sdk/package.py" --profile legacy --out "$out/assets"
printf '%s  %s\n' "$expected_archive" "$out/assets/sco-sdk-2.4.0.zip" | sha256sum -c -
unzip -q "$out/assets/sco-sdk-2.4.0.zip" -d "$out/extracted"
(cd "$out/extracted/sco-sdk-2.4.0" && sha256sum -c SHA256SUMS >/dev/null)
printf 'RC3 candidate preflight: source=%s archive_sha256=%s\n' "$expected_source" "$expected_archive"
