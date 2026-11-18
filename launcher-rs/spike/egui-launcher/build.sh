#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Release build that strips the builder's home and source paths from panic strings.
# Usage: ./build.sh glow|wgpu
# RUSTFLAGS is set here because cargo config cannot expand CARGO_HOME. The static-CRT and /Brepro
# flags from .cargo/config.toml are repeated, since RUSTFLAGS replaces config rustflags.
set -e
v="${1:?glow or wgpu}"
ch="${CARGO_HOME:-$HOME/.cargo}"
here="$(pwd -W 2>/dev/null || pwd)"
# rustc on Windows prints backslash paths, so the prefixes must use that form.
if command -v cygpath >/dev/null 2>&1; then ch="$(cygpath -w "$ch")"; here="$(cygpath -w "$here")"; fi
export RUSTFLAGS="-C target-feature=+crt-static -C link-arg=/Brepro --remap-path-prefix=$ch=/cargo --remap-path-prefix=$here=/spike"
cargo build --release --features "$v" --target-dir "target/$v"
