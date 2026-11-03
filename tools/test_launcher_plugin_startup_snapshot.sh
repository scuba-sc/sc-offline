#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$ROOT/launcher" \
  "$ROOT/tools/launcher_plugin_startup_snapshot_test.cpp" -o "$TMP/plugin-startup-test"
"$TMP/plugin-startup-test" \
  "$ROOT/tools/fixtures/plugin-startup-v1-complete.json" \
  "$ROOT/tools/fixtures/plugin-startup-v1-starting.json"
