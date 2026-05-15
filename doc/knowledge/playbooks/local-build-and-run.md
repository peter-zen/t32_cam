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

### 2. Build

```bash
cd /path/to/t32
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j$(nproc)
```

## Quick rebuild

```bash
# Simulation
cd build_sim && make -j$(nproc)

# Hardware
cd build && make -j$(nproc)
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
