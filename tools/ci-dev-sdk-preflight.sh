#!/usr/bin/env bash
# Unpublished coordinated core development compatibility input for syntax and module checks.
# This does not change the product gitlink or the published SDK default.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
core=$(cd "$1" && pwd)
out=$2
expected_source=1b6782bc5c8b7d189cdff4c83e81957f09af35d7
expected_archive=bb52b8bf1cab5aea09939e68a387705149411ce6c8af48e3f3f2576fc8a83c31
[ "$(git -C "$core" rev-parse HEAD)" = "$expected_source" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
mkdir -p "$out/assets" "$out/extracted"
python3 "$core/sdk/package.py" --profile legacy --out "$out/assets"
printf '%s  %s\n' "$expected_archive" "$out/assets/sco-sdk-2.4.0.zip" | sha256sum -c -
unzip -q "$out/assets/sco-sdk-2.4.0.zip" -d "$out/extracted"
(cd "$out/extracted/sco-sdk-2.4.0" && sha256sum -c SHA256SUMS >/dev/null)
printf 'UNPUBLISHED development preflight: source=%s archive_sha256=%s\n' "$expected_source" "$expected_archive"
