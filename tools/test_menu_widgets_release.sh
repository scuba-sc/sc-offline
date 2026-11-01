#!/usr/bin/env bash
# Build the actual menu harness with NDEBUG and prove its checks still execute.
set -eu
if [ "$#" -ne 1 ]; then
  echo "usage: $0 <core-source-root>" >&2
  exit 2
fi
core=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/sco-menu-release.XXXXXX")
trap 'rm -rf "$out"' EXIT
# A false condition after the real Start call must fail even with NDEBUG.
sed 's/CHECK(sco::app::Start(p));/CHECK(sco::app::Start(p) \&\& false);/' "$here/menu_widgets_test.cpp" > "$out/negative.cpp"
grep -q 'CHECK(sco::app::Start(p) && false);' "$out/negative.cpp"
cat > "$out/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(menu_release_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(SCO_GAME_SC OFF CACHE BOOL "" FORCE)
set(SCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CORE}" core EXCLUDE_FROM_ALL)
foreach(name menu negative)
  if(name STREQUAL menu)
    set(source "${TOOLS}/menu_widgets_test.cpp")
  else()
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/negative.cpp")
  endif()
  add_executable(${name} "${source}" "${SRC}/menu_widgets.cpp")
  target_include_directories(${name} PRIVATE "${SRC}" "${TOOLS}")
  target_compile_definitions(${name} PRIVATE NDEBUG)
  target_link_libraries(${name} PRIVATE sco_app)
endforeach()
CMAKE
"${SCO_TEST_CMAKE:-cmake}" -S "$out" -B "$out/build" -DCORE="$core" -DTOOLS="$here" -DSRC="$here/../src" -DCMAKE_BUILD_TYPE=Debug
"${SCO_TEST_CMAKE:-cmake}" --build "$out/build" --target menu negative --parallel 2
status=0
"$out/build/menu" || status=$?
case "$status" in 0|77) ;; *) exit 1;; esac
status=0
"$out/build/negative" >"$out/negative.log" 2>&1 || status=$?
[ "$status" = 1 ]
grep -q 'FAIL .*sco::app::Start(p) && false' "$out/negative.log"
echo 'menu harness NDEBUG checks and forced-failure negative control passed'
