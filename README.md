# How to build project

`toolchain.cmake` expects the cross-compile toolchain at `toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve/` (relative to the project root). If the directory is missing, extract it from `ref/Tassadar-T32-1.0.6-20250613.7z`.

## T32 hardware build (NFS-shared with the device)

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .
cmake --build build -j$(nproc)
```

Output binaries land in `build/bin/` and `build/lib/`. This `build/` directory is also NFS-shared to the T32 device at `/mnt/huntcam/`; do not `rm -rf` it without first unmounting on the device side. See `.claude/CLAUDE.md` for NFS mount options and the build/deploy sanity-check workflow.

- `htc_main_app`  — main app (work mode / wifi / mobile / etc.)
- `htc_media_app` — boot-time workmode probe + main_app launcher
- `htc_daemon_app`, `snap_test`, `wpa_conn`

## PC simulation build

```bash
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .
cmake --build build_sim -j$(nproc)
```

# How to build sdk/samples/libimp-samples/
- cd sdk/samples/libimp-samples/
- make CROSS_COMPILE=/home/zengping/t32/bsp/toolchain/mips-gcc540-glibc222-cmake3.16.3-r3.3.7.mxu2.cve/bin/mips-linux-uclibc-gnu-