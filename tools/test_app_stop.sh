#!/usr/bin/env bash
# Exercise both SDK Stop signatures and failure paths without a game.
set -eu
if [ "$#" -ne 1 ]; then
  echo "usage: $0 <core-source-root>" >&2
  exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/sco-test-app-stop.XXXXXX")
trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$1/include" "$here/test_app_stop.cpp" -o "$out/test"
for mode in legacy failed throw ok; do
  status=0
  "$out/test" "$mode" >"$out/$mode" 2>&1 || status=$?
  case "$mode:$status" in legacy:77|failed:1|throw:1|ok:0) ;; *) cat "$out/$mode"; exit 1;; esac
  if [ "$mode" = ok ]; then
    grep -qx 'module closed' "$out/$mode"
    grep -qx 'backing destroyed' "$out/$mode"
  elif grep -Eq '^(module closed|backing destroyed)$' "$out/$mode"; then
    cat "$out/$mode"
    exit 1
  fi
done
# A premature release followed by _Exit must still leave visible markers.
status=0
"$out/test" destroy-then-exit >"$out/destroy-then-exit" 2>&1 || status=$?
[ "$status" = 1 ]
grep -qx 'module closed' "$out/destroy-then-exit"
grep -qx 'backing destroyed' "$out/destroy-then-exit"
# Negative control: a broken guard that treats Failed as safe must release both
# objects; otherwise the marker-based retention check cannot detect a regression.
cp "$here/test_app_stop.cpp" "$out/test_app_stop.cpp"
sed 's/stop() == sco::Result::Ok/stop() != sco::Result::Ok/' "$here/test_app_stop.h" > "$out/test_app_stop.h"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$1/include" "$out/test_app_stop.cpp" -o "$out/unsafe"
"$out/unsafe" failed >"$out/unsafe-output" 2>&1
grep -qx 'module closed' "$out/unsafe-output"
grep -qx 'backing destroyed' "$out/unsafe-output"
echo 'test app Stop: legacy, failure, exception, success, and unsafe negative control passed'
