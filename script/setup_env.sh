#!/bin/sh
# setup_env.sh — set up the T32 MIPS cross-build environment.
# Source it (do NOT execute — exports must land in your shell):
#   source script/setup_env.sh        # bash/zsh, from anywhere inside the repo
#   . script/setup_env.sh             # POSIX sh
#
# Two things must be right or a bare `cmake` build fails:
#
#   1. PATH — toolchain/bin FIRST.
#      The cross-gcc resolves the assembler `as` via PATH. Without toolchain/bin
#      first it falls back to the host x86 `/usr/bin/as`, which rejects the MIPS
#      little-endian flag  →  "as: unrecognized option '-EL'". This bites BOTH
#      configure (cmake's compiler probe) and build.
#
#   2. cmake — the SYSTEM cmake (>= 3.16), NOT the toolchain's.
#      `CMakeLists.txt:1` is `cmake_minimum_required(3.16)`. The toolchain bundles
#      cmake 3.8.2, which is BELOW 3.16 → it CANNOT configure this project (only
#      drive `--build` on an already-configured build/). On cmake 4.x hosts,
#      configure also needs -DCMAKE_POLICY_VERSION_MINIMUM=3.5 (third_party/ has
#      old cmake_minimum_required); this script auto-adds it (T32_CMAKE_CONFIGURE_FLAGS).
#
# After sourcing:
#   "$T32_CMAKE_BIN" $T32_CMAKE_CONFIGURE_FLAGS -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .  # configure
#   "$T32_CMAKE_BIN" --build build -j"$(nproc)"                                                       # build (system cmake takes bare -jN)
# Or use the host-specific driver script/build_t32@<host>.sh (configure+build in one shot).
# Full write-up: doc/knowledge/playbooks/local-build-and-run.md §"Hardware build".

# --- locate repo root = the dir containing toolchain/, walking up from $PWD ---
_root="$PWD"
while [ "$_root" != "/" ] && [ ! -d "$_root/toolchain" ]; do
    _root=$(dirname "$_root")
done
if [ ! -d "$_root/toolchain" ]; then
    echo "setup_env: repo root not found (no toolchain/ dir upward from $PWD)." >&2
    echo "setup_env: cd into the t32_app repo first, then source again." >&2
    return 1 2>/dev/null || exit 1
fi

TC_DIR="$_root/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve"
if [ ! -x "$TC_DIR/bin/mips-linux-uclibc-gnu-gcc" ]; then
    echo "setup_env: toolchain not extracted at $TC_DIR" >&2
    echo "setup_env: extract it first — see doc/knowledge/playbooks/local-build-and-run.md §'Prepare the toolchain'." >&2
    return 1 2>/dev/null || exit 1
fi

export T32_ROOT="$_root"
export T32_TOOLCHAIN_DIR="$TC_DIR"
# (1) toolchain/bin FIRST so cross-gcc finds the MIPS as/ld (fixes 'as: -EL').
export PATH="$TC_DIR/bin:$PATH"

# (2) SYSTEM cmake (>= 3.16) = first `cmake` on PATH EXCLUDING toolchain/bin
#     (skip the bundled 3.8.2, which can't configure this project).
_syspath=$(printf '%s\n' "$PATH" | tr ':' '\n' | grep -vxF "$TC_DIR/bin" | tr '\n' ':')
_cmake=$(PATH="$_syspath" command -v cmake 2>/dev/null)
[ -z "$_cmake" ] && _cmake=$(command -v cmake)
export T32_CMAKE_BIN="$_cmake"

# cmake 4.x dropped <3.5 compat; third_party/ has old cmake_minimum_required, so
# configure on 4.x needs -DCMAKE_POLICY_VERSION_MINIMUM=3.5. Auto-set when the
# system cmake major version is >= 4 (empty on 3.x).
_cmake_maj=$(printf '%s' "$("$_cmake" --version 2>/dev/null | head -1)" | sed -n 's/^cmake version \([0-9]\+\).*/\1/p')
if [ -n "$_cmake_maj" ] && [ "$_cmake_maj" -ge 4 ] 2>/dev/null; then
    export T32_CMAKE_CONFIGURE_FLAGS="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
else
    export T32_CMAKE_CONFIGURE_FLAGS=""
fi

cat <<EOF
[T32 build env ready]
  repo      : $T32_ROOT
  toolchain : $T32_TOOLCHAIN_DIR
  as        : $(command -v as)        # must be under toolchain/, NOT /usr/bin/as
  cmake     : $T32_CMAKE_BIN ($("$_cmake" --version 2>/dev/null | head -1))
  configure : $T32_CMAKE_BIN $T32_CMAKE_CONFIGURE_FLAGS -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .
  build     : $T32_CMAKE_BIN --build build -j\$(nproc)
EOF
unset _root _syspath _cmake _cmake_maj
