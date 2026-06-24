#!/bin/sh
# T32 hardware cross-build — COMPANY build host 192.168.0.206.
# Run from anywhere; the script cd's to the repo root itself:
#   ./script/build_t32@206.sh
#
# This script exists because three host-specific snags break a bare `cmake`
# invocation on this PC:
#   1. The cross-gcc resolves `as` via PATH; without toolchain/bin first it
#      falls back to the host x86 `as`  →  "unrecognized option '-EL'".
#   2. The toolchain bundles cmake 3.8.2; once toolchain/bin is on PATH a bare
#      `cmake` shadows to it. Call /usr/bin/cmake (4.x) by absolute path.
#   3. cmake 4.x dropped <3.5 compat, but third_party/libevent uses an old
#      cmake_minimum_required  →  -DCMAKE_POLICY_VERSION_MINIMUM=3.5.
#
# Adapting to another build host (e.g. home WSL on 192.168.31.x): copy this
# file to script/build_t32@<host>.sh and tweak TOOLCHAIN_DIR / CMAKE_BIN below
# if they live elsewhere on that machine.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

# --- host-specific knobs (edit when porting to a new build host) ---
TOOLCHAIN_DIR="$ROOT/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve"
CMAKE_BIN=/usr/bin/cmake      # system cmake 4.x — NOT the toolchain's 3.8.2
POLICY_MIN=3.5                # cmake 4.x vs old third_party cmake_minimum_required

# toolchain/bin first so the cross-gcc finds the MIPS assembler
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

echo ">> configure  (cmake=$CMAKE_BIN  host=$(hostname -I 2>/dev/null | awk '{print $1}'))"
"$CMAKE_BIN" -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
             -DCMAKE_POLICY_VERSION_MINIMUM=$POLICY_MIN \
             -B build -S .

echo ">> build  (-j$(nproc))"
"$CMAKE_BIN" --build build -j"$(nproc)"

echo ">> done → build/bin (MIPS) + build/lib"
