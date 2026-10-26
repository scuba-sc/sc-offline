#!/usr/bin/env bash
# Build a local no-game integration fixture against an explicit Core build and spawn module.
set -eu
if [ "$#" -ne 3 ]; then
  echo "usage: $0 <core-source-root> <core-cmake-build> <spawn-module>" >&2
  exit 2
fi
core=$1
build=$2
module=$3
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/sco-asop-integration.XXXXXX")
trap 'rm -rf "$out"' EXIT
libs=("$build/libsco_asop_selection.a" "$build/libsco_widgets.a" "$build/libsco_host.a" "$build/libsco_runtime.a" "$build/libsco_net.a" "$build/libsco_core.a")
extra=()
if [ "$(uname -s)" != Darwin ]; then extra=(-ldl -pthread); fi
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$core/include" "$here/asop_selection_integration_test.cpp" "${libs[@]}" "${extra[@]}" -o "$out/test"
"$out/test" "$module"
