#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds the Rust menu and runs both synthetic hosts with the panel fixture. Needs the toolchain
# environment of the build notes (cargo, cmake, Visual Studio 2022) and a Core checkout with
# include/sco/widgets_view.h.
#   CORE=<core checkout> OUT=<scratch dir> ./run.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
: "${CORE:?set CORE to a Core checkout with include/sco/widgets_view.h}"
: "${OUT:=${TMPDIR:-/tmp}/rust-menu-tests}"
: "${SHOTS:=$OUT/shots}"
export CARGO_TARGET_DIR="$OUT/cargo"
(cd "$repo/launcher-rs" && cargo test -p scol-menu && cargo build -p scol-menu --release && cargo build -p scol-menu --examples \
  && "$CARGO_TARGET_DIR/debug/examples/menu_host" --dump "$here/panels.txt" --out "$SHOTS/rust")
cmake -S "$repo/tools/rust-menu-host" -B "$OUT/host" -DSCO_CORE_SOURCE_DIR="$CORE"
cmake --build "$OUT/host" --config Release --target rust_menu_host
"$OUT/host/Release/rust_menu_host" "$CARGO_TARGET_DIR/release/sc_offline_menu.dll" "$SHOTS/core" "$here/panels.txt"
