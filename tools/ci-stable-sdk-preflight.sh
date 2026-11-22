#!/usr/bin/env bash
# Verify the exact published SDK 2.4.1 package from its committed Core source.
# The external-plugin and embedded-host pins are independently checked.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 <core-source-root> <new-output-dir>" >&2; exit 2; fi
core=$(cd "$1" && pwd)
out=$2
expected_source=ef4061b123a1f0b4f449e6c0257078d7348520e3
expected_archive=7d17e8671a45129a74b129b4620899ba999d7be783785baa69a8e985a43cb0fd
[ "$(git -C "$core" rev-parse HEAD)" = "$expected_source" ] || { echo 'development source identity mismatch' >&2; exit 1; }
[ -z "$(git -C "$core" status --porcelain)" ] || { echo 'development source is dirty' >&2; exit 1; }
[ ! -e "$out" ] || { echo 'development output already exists' >&2; exit 1; }
mkdir -p "$out/assets" "$out/extracted"
python3 "$core/sdk/package.py" --profile legacy --out "$out/assets"
printf '%s  %s\n' "$expected_archive" "$out/assets/sco-sdk-2.4.1.zip" | sha256sum -c -
unzip -q "$out/assets/sco-sdk-2.4.1.zip" -d "$out/extracted"
(cd "$out/extracted/sco-sdk-2.4.1" && sha256sum -c SHA256SUMS >/dev/null)
printf 'Published SDK 2.4.1 preflight: source=%s archive_sha256=%s\n' "$expected_source" "$expected_archive"
