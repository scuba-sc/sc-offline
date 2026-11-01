#!/usr/bin/env bash
# Verify the exact published SDK 2.4.0 package from its committed Core source.
# The external-plugin and embedded-host pins are independently checked.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
core=$(cd "$1" && pwd)
out=$2
expected_source=6106fa3639cbcc28f78a1ded8de2707de019f4d3
expected_archive=0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc
[ "$(git -C "$core" rev-parse HEAD)" = "$expected_source" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
mkdir -p "$out/assets" "$out/extracted"
python3 "$core/sdk/package.py" --profile legacy --out "$out/assets"
printf '%s  %s\n' "$expected_archive" "$out/assets/sco-sdk-2.4.0.zip" | sha256sum -c -
unzip -q "$out/assets/sco-sdk-2.4.0.zip" -d "$out/extracted"
(cd "$out/extracted/sco-sdk-2.4.0" && sha256sum -c SHA256SUMS >/dev/null)
printf 'Published SDK 2.4.0 preflight: source=%s archive_sha256=%s\n' "$expected_source" "$expected_archive"
