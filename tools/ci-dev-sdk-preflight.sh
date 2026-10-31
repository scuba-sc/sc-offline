#!/usr/bin/env bash
# Unpublished coordinated core development compatibility input for syntax and module checks.
# This does not change the product gitlink or the published SDK default.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
core=$(cd "$1" && pwd)
out=$2
expected_source=49eb153d1d0952ebbc7baa258988e6dc6cc67320
expected_archive=2a3f467ce7a517603056ff50d93a4991316828234ef24b587db3a4741df5d066
[ "$(git -C "$core" rev-parse HEAD)" = "$expected_source" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
mkdir -p "$out/assets" "$out/extracted"
python3 "$core/sdk/package.py" --profile legacy --out "$out/assets"
printf '%s  %s\n' "$expected_archive" "$out/assets/sco-sdk-2.4.0.zip" | sha256sum -c -
unzip -q "$out/assets/sco-sdk-2.4.0.zip" -d "$out/extracted"
(cd "$out/extracted/sco-sdk-2.4.0" && sha256sum -c SHA256SUMS >/dev/null)
printf 'UNPUBLISHED development preflight: source=%s archive_sha256=%s\n' "$expected_source" "$expected_archive"
