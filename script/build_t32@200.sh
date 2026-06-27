#!/bin/sh
# T32 hardware cross-build — HOME build host 192.168.31.200 (this PC).
# Run from anywhere; the script cd's to the repo root itself:
#   ./script/build_t32@200.sh
#
# This script exists because two host-specific snags break a bare `cmake`
# invocation on this PC:
#   1. The cross-gcc resolves `as` via PATH; without toolchain/bin first it
#      falls back to the host x86 `as`  →  "unrecognized option '-EL'".
#   2. The toolchain bundles cmake 3.8.2; once toolchain/bin is on PATH a bare
#      `cmake` shadows to it. Call /usr/bin/cmake (3.28.3) by absolute path.
#
# NOTE — diverges from script/build_t32@206.sh in one place:
#   the company host runs cmake 4.x, which dropped <3.5 compat and needs
#   -DCMAKE_POLICY_VERSION_MINIMUM=3.5 against third_party/'s old
#   cmake_minimum_required. This host has cmake 3.28.3 which still accepts
#   old project() calls natively, so that knob is omitted here. If you ever
#   upgrade the system cmake to 4.x, copy that flag back in from the @206
#   script.
#
# Adapting to another build host: copy this file to
# script/build_t32@<host>.sh and tweak TOOLCHAIN_DIR / CMAKE_BIN below
# if they live elsewhere on that machine.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

# --- host-specific knobs (edit when porting to a new build host) ---
TOOLCHAIN_DIR="$ROOT/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve"
CMAKE_BIN=/usr/bin/cmake      # system cmake 3.28.3 — NOT the toolchain's 3.8.2

# toolchain/bin first so the cross-gcc finds the MIPS assembler
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

echo ">> configure  (cmake=$CMAKE_BIN  host=$(hostname -I 2>/dev/null | awk '{print $1}'))"
"$CMAKE_BIN" -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
             -B build -S .

echo ">> build  (-j$(nproc))"
"$CMAKE_BIN" --build build -j"$(nproc)"

echo ">> done → build/bin (MIPS) + build/lib"