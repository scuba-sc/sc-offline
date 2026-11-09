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
[ "$(git -C "$root" rev-parse HEAD:external/sco-core)" = d06f9124d6d7d380295ada4bc2af8968b98a051c ] || {
  echo 'development offline gitlink mismatch' >&2; exit 1;
}
[ "$(git -C "$core" rev-parse HEAD)" = d06f9124d6d7d380295ada4bc2af8968b98a051c ] || { echo 'Core commit mismatch' >&2; exit 1; }
[ "$(git -C "$core" rev-parse 'HEAD^{tree}')" = 66d6b29f6c54aa1933aa2b8e0f2bc40d70c004c1 ] || { echo 'Core tree mismatch' >&2; exit 1; }
# Ignored files could influence CMake. Offline is built in place, so reject them.
check_no_ignored "$root"
printf 'offline=%s tree=%s\ncore=%s tree=%s\ndevelopment_gitlink=d06f9124d6d7d380295ada4bc2af8968b98a051c\n' \
  "$offline_commit" "$offline_tree" d06f9124d6d7d380295ada4bc2af8968b98a051c 66d6b29f6c54aa1933aa2b8e0f2bc40d70c004c1
# Resolve symlinked parents before any mkdir, including in verify-only mode.
[ ! -e "$build" ] || { echo 'build directory already exists' >&2; exit 1; }
build_abs=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$build")
for source in "$root" "$core"; do
  case "$build_abs/" in "$source/"*) echo 'build directory must be outside both source trees' >&2; exit 1;; esac
done
[ "$mode" != --verify-only ] || exit 0
for cmd in cmake ctest; do command -v "$cmd" >/dev/null || { echo "$cmd not found" >&2; exit 2; }; done
# Never reuse a CMake cache, alter source trees, or remove existing files.
mkdir -p "$build_abs"
build=$build_abs
build=$(cd "$build" && pwd -P)
# Clone the verified object locally so ignored/generated files in the input Core
# workspace cannot affect the build. This does not fetch from or publish to GitHub.
git clone -q --no-hardlinks --no-checkout "$core" "$build/core"
git -C "$build/core" checkout -q --detach d06f9124d6d7d380295ada4bc2af8968b98a051c
[ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = 66d6b29f6c54aa1933aa2b8e0f2bc40d70c004c1 ]
[ -z "$(git -C "$build/core" status --porcelain --untracked-files=all)" ]
mkdir -p "$build/fixture/.cmake/api/v1/query"
touch "$build/fixture/.cmake/api/v1/query/codemodel-v2"
cmake -S "$root/tools/offline-host" -B "$build/fixture" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DSCO_HOST_SOURCE_DIR=$build/core"
[ "$(git -C "$build/core" rev-parse HEAD)" = d06f9124d6d7d380295ada4bc2af8968b98a051c ]
[ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = 66d6b29f6c54aa1933aa2b8e0f2bc40d70c004c1 ]
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
  [ "$(git -C "$build/core" rev-parse HEAD)" = d06f9124d6d7d380295ada4bc2af8968b98a051c ]
  [ "$(git -C "$build/core" rev-parse 'HEAD^{tree}')" = 66d6b29f6c54aa1933aa2b8e0f2bc40d70c004c1 ]
  check_clean "$build/core"; check_no_ignored "$build/core"
  check_clean "$root"; check_no_ignored "$root"
  python3 "$root/tools/transition-pair/verify_cmake.py" "$build/product" "$build/core" "$root" product
  cmake --build "$build/product" --config Release --parallel 2
fi
printf '%s\n' 'Paired source qualification finished; this is not native/game acceptance.'
