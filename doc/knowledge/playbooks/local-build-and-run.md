# Local Build and Run

## Prerequisites

- GCC (simulation) or MIPS cross-compiler (hardware)
- CMake >= 3.16
- Git

## Simulation build (PC)

```bash
cd /path/to/t32
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

Output binary: `build_sim/bin/htc_main_app`

## Hardware build (T32 MIPS)

```bash
cd /path/to/t32
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j$(nproc)
```

## Quick rebuild

```bash
cd build_sim && make -j$(nproc)
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
