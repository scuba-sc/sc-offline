#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Qualify a private paired source candidate, never the published SDK archive.
set -eu
if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo "usage: $0 CORE_SOURCE EMPTY_BUILD_DIR OFFLINE_COMMIT OFFLINE_TREE [--verify-only|--product]" >&2
  exit 2
fi
core=$1; build=$2; offline_commit=$3; offline_tree=$4; mode=${5:-}
case "$mode" in ''|--verify-only|--product) ;; *) echo 'invalid mode' >&2; exit 2;; esac
for hash in "$offline_commit" "$offline_tree"; do
  if [[ ! "$hash" =~ ^[0-9a-f]{40}$ ]]; then echo 'expected full lowercase Git SHA-1' >&2; exit 2; fi
done
root=$(cd "$(dirname "$0")/../.." && pwd -P)
core=$(cd "$core" && pwd -P)
platform=$(uname -s)
case "$platform" in
  MINGW*|MSYS*|CYGWIN*)
    # Git and CMake also receive shell paths, so do not proceed when the
    # caller has disabled their automatic conversion.
    [ -z "${MSYS_NO_PATHCONV:-}" ] && [ -z "${MSYS2_ARG_CONV_EXCL:-}" ] || {
      echo 'MSYS argument conversion must be enabled' >&2; exit 1;
    }
    ;;
esac
check_root() {
  local dir=$1 top
  top=$(git -C "$dir" rev-parse --show-toplevel) || return 1
  [ "$(cd "$top" && pwd -P)" = "$dir" ] || { echo "not repository root: $dir" >&2; return 1; }
}
check_clean() {
  local dir=$1
  [ -z "$(git -C "$dir" status --porcelain --untracked-files=all)" ] || {
    echo "source has tracked or untracked changes: $dir" >&2; return 1;
  }
}
check_no_ignored() {
  local dir=$1
  [ -z "$(git -C "$dir" clean -ndx)" ] || {
    echo "ignored files present in source: $dir" >&2; return 1;
  }
}
check_root "$root"; check_root "$core"
check_clean "$root"; check_clean "$core"
[ "$(git -C "$root" rev-parse HEAD)" = "$offline_commit" ] || { echo 'offline commit mismatch' >&2; exit 1; }
[ "$(git -C "$root" rev-parse 'HEAD^{tree}')" = "$offline_tree" ] || { echo 'offline tree mismatch' >&2; exit 1; }
git -C "$root" merge-base --is-ancestor 259ee6284d4c37f2eafa32f668ff29f4bc266844 HEAD || {
  echo 'offline candidate ancestry mismatch' >&2; exit 1;
}
[ "$(git -C "$root" rev-parse HEAD:external/sco-core)" = d33932bc14b85af0caccc4636b42028f44f97710 ] || {
  echo 'development offline gitlink mismatch' >&2; exit 1;
}
[ "$(git -C "$core" rev-parse HEAD)" = d33932bc14b85af0caccc4636b42028f44f97710 ] || { echo 'Core commit mismatch' >&2; exit 1; }
[ "$(git -C "$core" rev-parse 'HEAD^{tree}')" = c7563a009994c07598edae124608053dde85a92a ] || { echo 'Core tree mismatch' >&2; exit 1; }
# Ignored files could influence CMake. Offline is built in place, so reject them.
check_no_ignored "$root"
printf 'offline=%s tree=%s\ncore=%s tree=%s\ndevelopment_gitlink=d33932bc14b85af0caccc4636b42028f44f97710\n' \
  "$offline_commit" "$offline_tree" d33932bc14b85af0caccc4636b42028f44f97710 c7563a009994c07598edae124608053dde85a92a
# Convert explicitly instead of relying on MSYS argument conversion, which can
# be disabled by the caller. Then resolve all paths in the same Python process
# before any mkdir, including in verify-only mode.
build_native=$build; root_native=$root; core_native=$core
case "$platform" in
  MINGW*|MSYS*|CYGWIN*)
    command -v cygpath >/dev/null || { echo 'cygpath required for Windows path checks' >&2; exit 1; }
    # Cygwin/MSYS Python may use POSIX rather than Windows path semantics.
    python_os=$(python3 -c 'import os; print(os.name)') || exit 1
    case "$python_os" in
      nt) path_option=-w ;;
      posix) path_option=-u ;;
      *) echo 'unsupported Python path semantics' >&2; exit 1 ;;
    esac
    build_native=$(cygpath "$path_option" -- "$build") || exit 1
    root_native=$(cygpath "$path_option" -- "$root") || exit 1
    core_native=$(cygpath "$path_option" -- "$core") || exit 1
    ;;
esac
build_abs=$(python3 -c '
import os, stat, sys

build, *sources = sys.argv[1:]
try:
    os.lstat(build)
except FileNotFoundError:
    pass
except OSError as exc:
    sys.exit("cannot inspect build path: " + str(exc))
else:
    sys.exit("build directory already exists")
build = os.path.realpath(build)
normalized_build = os.path.normcase(build)
source_ids = []
for source in sources:
    source = os.path.normcase(os.path.realpath(source))
    try:
        source_stat = os.stat(source)
    except OSError as exc:
        sys.exit("cannot inspect source path: " + str(exc))
    if not stat.S_ISDIR(source_stat.st_mode):
        sys.exit("source path must be a directory")
    source_ids.append((source_stat.st_dev, source_stat.st_ino))
    try:
        common = os.path.commonpath((normalized_build, source))
    except ValueError:
        # On Windows, absolute paths on different drives are disjoint.
        build_drive = os.path.splitdrive(normalized_build)[0]
        source_drive = os.path.splitdrive(source)[0]
        if os.name == "nt" and build_drive and source_drive and build_drive != source_drive:
            continue
        raise
    if common == source:
        sys.exit("build directory must be outside both source trees")
# A spelling comparison alone is insufficient on case-insensitive POSIX file
# systems. Walk the existing ancestors of the proposed path and compare their
# file identities to both roots. Missing components are expected; inability to
# inspect any other component fails closed. This is not a race-free sandbox.
ancestor = build
found_ancestor = False
while True:
    try:
        info = os.stat(ancestor)
    except FileNotFoundError:
        pass
    except OSError as exc:
        sys.exit("cannot inspect build ancestor: " + str(exc))
    else:
        found_ancestor = True
        if (info.st_dev, info.st_ino) in source_ids:
            sys.exit("build directory must be outside both source trees")
    parent = os.path.dirname(ancestor)
    if parent == ancestor:
        break
    ancestor = parent
if not found_ancestor:
    sys.exit("cannot inspect an existing build ancestor")
print(build)
' "$build_native" "$root_native" "$core_native") || exit 1
[ "$mode" != --verify-only ] || exit 0
for cmd in cmake ctest; do command -v "$cmd" >/dev/null || { echo "$cmd not found" >&2; exit 2; }; done
# Never reuse a CMake cache, alter source trees, or remove existing files.
mkdir -p "$build_abs"
build=$build_abs
build=$(cd "$build" && pwd -P)
# Clone the verified object locally so ignored/generated files in the input Core
# workspace cannot affect the build. This does not fetch from or publish to GitHub.
git clone -q --no-hardlinks --no-checkout "$core" "$build/core"
git -C "$build/core" checkout -q --detach d33932bc14b85af0caccc4636b42028f44f97710
[ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = c7563a009994c07598edae124608053dde85a92a ]
[ -z "$(git -C "$build/core" status --porcelain --untracked-files=all)" ]
mkdir -p "$build/fixture/.cmake/api/v1/query"
touch "$build/fixture/.cmake/api/v1/query/codemodel-v2"
cmake -S "$root/tools/offline-host" -B "$build/fixture" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DSCO_HOST_SOURCE_DIR=$build/core"
[ "$(git -C "$build/core" rev-parse HEAD)" = d33932bc14b85af0caccc4636b42028f44f97710 ]
[ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = c7563a009994c07598edae124608053dde85a92a ]
check_clean "$build/core"; check_no_ignored "$build/core"
check_clean "$root"; check_no_ignored "$root"
python3 "$root/tools/transition-pair/verify_cmake.py" "$build/fixture" "$build/core" "$root" fixture
cmake --build "$build/fixture" --config Release --parallel 2
listing=$(ctest --test-dir "$build/fixture" -C Release -N)
for name in owned_capsule_host owned_capsule_legacy-request host_report_cooperative-request host_report_owner-mismatch host_report_wrong-thread host_report_reentry host_report_retained_reap \
            native_activation_refusal product_registry_refusal \
            product_request_normal product_request_reentrant product_request_competing \
            product_request_failure; do
  printf '%s\n' "$listing" | grep -E "Test +#[0-9]+: ${name}$" >/dev/null || { echo "missing test: $name" >&2; exit 1; }
done
ctest --test-dir "$build/fixture" -C Release --no-tests=error --output-on-failure
if [ "$mode" = --product ]; then
  # Product is Windows/MSVC x64 only; caller supplies the appropriate toolchain.
  mkdir -p "$build/product/.cmake/api/v1/query"
  touch "$build/product/.cmake/api/v1/query/codemodel-v2"
  cmake -S "$root" -B "$build/product" -DCMAKE_BUILD_TYPE=Release "-DSCO_HOST_SOURCE_DIR=$build/core"
  [ "$(git -C "$build/core" rev-parse HEAD)" = d33932bc14b85af0caccc4636b42028f44f97710 ]
  [ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = c7563a009994c07598edae124608053dde85a92a ]
  check_clean "$build/core"; check_no_ignored "$build/core"
  check_clean "$root"; check_no_ignored "$root"
  python3 "$root/tools/transition-pair/verify_cmake.py" "$build/product" "$build/core" "$root" product
  cmake --build "$build/product" --config Release --parallel 2
fi
printf '%s\n' 'Paired source qualification finished; this is not native/game acceptance.'
