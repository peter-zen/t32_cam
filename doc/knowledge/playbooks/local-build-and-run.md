# Local Build and Run

## Prerequisites

- CMake >= 3.16
- Git
- GCC (for simulation build)
- `p7zip-full` (for extracting T32 toolchain from 7z archive)

## Simulation build (PC)

```bash
cd /path/to/t32
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

Output binary: `build_sim/bin/htc_main_app`

## Hardware build (T32 MIPS)

### 1. Prepare the toolchain (first time only)

The T32 MIPS cross-compiler is bundled in `ref/Tassadar-T32-1.0.6-20250613.7z`:

```bash
cd /path/to/t32

# Create temp extraction dir
mkdir -p /tmp/t32_extract

# Extract the tar.bz2 from the 7z archive
7z x ref/Tassadar-T32-1.0.6-20250613.7z \
    -o/tmp/t32_extract \
    "Tassadar-T32-1.0.6-20250613/software/pc/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve.tar.bz2"

# Extract toolchain into project directory
tar -xjf /tmp/t32_extract/Tassadar-T32-1.0.6-20250613/software/pc/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve.tar.bz2 \
    -C toolchain/

# Verify compiler is available
./toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve/bin/mips-linux-uclibc-gnu-gcc --version
```

Expected output:
```
mips-linux-gnu-gcc (Ingenic r3.3.7.mxu2.cve-gcc540 ...) 5.4.0
```

### 2. Set up the build environment (every new shell)

T32 MIPS cross-compilation has two host snags that break a bare `cmake`/`make`.
Put the toolchain first on PATH **and** drive the build with a compatible cmake.
Easiest — source the helper (run from anywhere inside the repo):

```bash
source script/setup_env.sh
```

It exports `PATH` (toolchain/bin first), `T32_TOOLCHAIN_DIR`, `T32_ROOT`,
`T32_CMAKE_BIN`, and `T32_CMAKE_CONFIGURE_FLAGS`, then prints what it picked.
Verify the two resolves:

- `as` → must be under `toolchain/.../bin/as` (the MIPS assembler), **not** `/usr/bin/as`.
- `cmake` → the **system** cmake (>= 3.16 — `CMakeLists.txt:1` requires it). The
  toolchain bundles cmake 3.8.2 but that is **below 3.16 and cannot configure
  this project** (it can only `--build` an already-configured `build/`). On a
  cmake 4.x host, `setup_env.sh` also sets `T32_CMAKE_CONFIGURE_FLAGS` =
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (third_party/ has old `cmake_minimum_required`).

The two pitfalls this step avoids (full story in `script/build_t32@200.sh` header):

| Symptom | Cause | Fix |
|---|---|---|
| `as: unrecognized option '-EL'` | cross-gcc resolved `as` via PATH to the host x86 `/usr/bin/as` (no MIPS `-EL` endianness flag) — bites configure's compiler probe AND build | toolchain/bin first on PATH (`setup_env.sh`) |
| configure fails `CMake 3.16 or higher is required` / `cmake_minimum_required` compat error | using the toolchain's cmake 3.8.2 (< 3.16), OR a system cmake 4.x without the policy flag | use the **system** cmake (>= 3.16) via `$T32_CMAKE_BIN`, plus `$T32_CMAKE_CONFIGURE_FLAGS` on 4.x (both auto-set by `setup_env.sh`) |

> Alternative — the host-specific driver does configure+build in one shot:
> `./script/build_t32@200.sh` (this PC) / `build_t32@206.sh` (company host). Copy
> to `build_t32@<host>.sh` and tweak `TOOLCHAIN_DIR`/`CMAKE_BIN` when porting.
> On a cmake 4.x host, also pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to configure
> (see `build_t32@206.sh`).

### 3. Build

After `source script/setup_env.sh`:

```bash
"$T32_CMAKE_BIN" $T32_CMAKE_CONFIGURE_FLAGS -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .   # configure (first time)
"$T32_CMAKE_BIN" --build build -j"$(nproc)"                          # build (system cmake: bare -jN)
```

Output: `build/bin/*` (MIPS ELF) + `build/lib/*.so`. Note `wm` links
`libapp_workmode.so` dynamically — **deploy both `build/bin/<app>` and any
rebuilt `build/lib/*.so`** to the device.

## Quick rebuild

```bash
# Simulation (PC — no toolchain needed)
cd build_sim && make -j$(nproc)

# Hardware (after `source script/setup_env.sh`)
"$T32_CMAKE_BIN" --build build -j"$(nproc)"
```

## Run simulation

```bash
./build_sim/bin/htc_main_app -rs    # Start RTSP server
ffplay rtsp://localhost:554/live     # Play stream (on another terminal)
```

## Run tests

```bash
cd build_sim
./bin/test_rtsp_av_simple ../sim_sdcard/video/test.h264 ../sim_sdcard/video/test.pcm
```

## Verify logs

Logs are written to `sim_sdcard/log/app.log` in simulation mode.

## Known Issues

### Toolchain search path mismatch (non-blocking)

`toolchain.cmake` appends the following paths to `CMAKE_FIND_ROOT_PATH`:

- `lib/gcc/mips-linux-uclibc-gnu/5.4.0/`
- `mips-linux-uclibc-gnu/lib/`

However, the actual extracted toolchain only contains `mips-linux-gnu/...` directories. The uclibc compiler is provided via a wrapper script (`uclibc-toolchain-wrapper`) that injects `-muclibc` and resolves libraries correctly, so this path mismatch does not block compilation. It is a minor inconsistency that may be cleaned up later if needed.
