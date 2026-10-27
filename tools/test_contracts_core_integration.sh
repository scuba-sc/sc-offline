#!/usr/bin/env bash
# Connect actual contracts module to explicit Core services and synthetic backend.
set -eu
if [ "$#" -ne 3 ]; then echo "usage: $0 <core-source-root> <core-cmake-build> <contracts-module>" >&2; exit 2; fi
core=$1
build=$2
module=$3
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/sco-contracts-integration.XXXXXX")
trap 'rm -rf "$out"' EXIT
extra=(-pthread)
if [ "$(uname -s)" != Darwin ]; then extra=(-ldl -pthread); fi
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$here" -I "$core/src" -I "$core/include" \
  "$here/contracts_core_integration_test.cpp" "$build/libsco_offline_missions.a" "$build/libsco_host.a" \
  "$build/libsco_runtime.a" "$build/libsco_core.a" "${extra[@]}" -o "$out/test"
"$out/test" "$module"
