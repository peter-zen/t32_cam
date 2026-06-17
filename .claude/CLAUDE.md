# t32_cam — Claude Code Session Guide

## Project identity

- **Name**: `t32_cam` (Ingenic T32 camera firmware)
- **Languages**: C++14, C11
- **Platforms**: Ingenic T32 MIPS (hardware) and x86_64 PC (simulation)
- **Build system**: CMake >= 3.16
- **Primary build targets**: `htc_main_app`, `htc_media_app`, `htc_daemon_app`, `snap_test`

## Authoritative sources

- **`doc/knowledge/README.md`** — documentation directory guide and placement rules
- **`doc/knowledge/overview.md`** — project summary, architecture, constraints
- **`doc/knowledge/working-set.md`** — current focus, top read-first docs, active risks

If anything in this prompt conflicts with the above sources, the latter wins.

## Build

### Canonical build directories (DO NOT use other names)

| Target | Build dir | Why this name |
|--------|-----------|---------------|
| T32 hardware | **`build/`** | NFS-shared to T32 device at `/mnt/huntcam/`; output is consumed in place |
| PC simulation | **`build_sim/`** | Local-only; produced by `-DBUILD_FOR_SIMULATION=ON` |

Both are gitignored. If a `build_t32/` (or similar) dir exists, it is **stale** — `rm -rf` it and re-run with `-B build`.

### Toolchain location (T32 only)

The cross-compile toolchain is **already present on the build host** at `toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve/` (relative to project root; referenced from `toolchain.cmake:10`). It is gitignored — not committed, but installed on this dev machine — so a normal T32 cross-build works out of the box. `build/` on this host is already configured for T32 (`BUILD_FOR_SIMULATION=OFF` + `toolchain.cmake`) and holds prior artifacts, so `cmake --build build -j$(nproc)` rebuilds in place.

**Do not conclude "toolchain missing" just because a path probe failed.** Paths like `toolchain/...` and `build/...` are relative to the project root and resolve against the current working directory. If you `cd`'d into `build/` or `build_sim/` (e.g. to run a built binary), those probes look for `build/toolchain/...` and come back empty — check `pwd` (or use an absolute path) before concluding anything is absent.

The toolchain ships two compiler variants, selected by `USE_UCLIBC` in `toolchain.cmake` (default `USE_UCLIBC=1`):
- `mips-linux-uclibc-gnu-gcc` (uClibc) — the **default** T32 target actually used by `build/`
- `mips-linux-gnu-gcc` (glibc) — alternate

On a fresh machine where the directory genuinely is absent, the toolchain is shipped as a nested `.tar.bz2` inside the release archive — extract the 7z, then untar into `toolchain/`:

```bash
# One-time, only if toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve/ is absent
7z x ref/Tassadar-T32-1.0.6-20250613.7z -oref/ \
  Tassadar-T32-1.0.6-20250613/software/pc/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve.tar.bz2
tar xjf ref/Tassadar-T32-1.0.6-20250613/software/pc/toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve.tar.bz2 \
  -C toolchain/
```

### T32 hardware build (NFS-shared, deployed to T32)

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .
cmake --build build -j$(nproc)
```

Outputs land in `build/bin/` and `build/lib/`. Notable:
- `htc_main_app`, `htc_media_app`, `htc_daemon_app` — main binaries
- `htc_main_app` is NOT a self-contained executable; it dynamically links all the `.so` in `build/lib/`
- `build/` will also contain device runtime data over time (`DCIM/`, `both/`, `media/`, etc.) because it is NFS-shared — **never `rm -rf build/`**

### T32 deployment via NFS

The standard development setup is the build host exporting `t32_cam/build/` over NFS and the T32 device mounting it at `/mnt/huntcam`:

```bash
# On the T32 device (one-time)
mkdir -p /mnt/huntcam
mount -t nfs -o nolock,noac,vers=3 \
  <BUILD_HOST>:/home/zengping/project/huntcam/code/t32_cam/build \
  /mnt/huntcam
```

**Mount options are non-negotiable:**
- `noac` — disables NFS attribute caching, so rebuilt binaries / .so files become visible immediately. Without it, device sees stale `htc_main_app` even after rebuild.
- `nolock` — embedded T32 NFS client often lacks `rpc.lockd`
- `vers=3` — most embedded device NFS servers only support v3

The NFS server's `/etc/exports` must whitelist `t32_cam/build/`. If you also need to mount other dirs (e.g., `t32_cam/build_t32/`), update exports + `exportfs -a`.

### PC simulation build (local-only)

```bash
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .
cmake --build build_sim -j$(nproc)
```

Outputs land in `build_sim/bin/` and `build_sim/lib/`. Safe to `rm -rf build_sim` to start over.

### Verifying the build is what you think it is

Three failure modes look identical ("I rebuilt, why is the device still buggy?"). Run this from the project root before debugging:

```bash
# 1. PC-side md5 of the binary
md5sum build/bin/htc_main_app
# expect: 0cb89e4e6ddcf065ec7b53889b98f61d (or whatever the current build is)

# 2. Device-side md5
# On T32:
cd /mnt/huntcam && md5sum bin/htc_main_app
# MUST match step 1 — if not, see "Common build/deploy mistakes" below.

# 3. Check which .so the device process actually loaded
# On T32:
htc_main_app -wm 0 -rtc 1 &
PID=$!
cat /proc/$PID/maps | grep media_recorder
kill $PID
# MUST show /mnt/huntcam/lib/libmedia_recorder.so (not /usr/lib/...)
```

### Common build/deploy mistakes

| Symptom | Root cause | Fix |
|---------|-----------|-----|
| Device runs old code, log format is from a prior refactor | NFS mount points at a different build dir (e.g., `build/` vs `build_t32/`) than cmake is writing to | Verify `mount | grep huntcam` source path matches cmake `-B` value |
| Device log shows "thumbnail skipped" or other old path that the new code removed | Old `libmedia_recorder.so` is being loaded from `/usr/lib/` instead of the NFS-mounted `build/lib/` | Run with `LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_main_app`, or rebuild-and-push the .so |
| Edits don't take effect even after `cmake --build` | NFS attribute cache — mount option missing `noac` | Remount with `-o noac` (see T32 deployment above) |
| `mount ... build_t32 → Permission denied` | NFS server's `/etc/exports` doesn't list that path | Either symlink `build → build_t32` on the server, edit exports + `exportfs -a`, or compile into `build/` |

## Guardrails

- **`src/hal/**` is PIC-owned.** Do not modify without prior written proposal and confirmation.
- **Git policy**: Never commit, push, or rollback without explicit user permission.
- **Dual-platform**: Code must compile for both `BUILD_FOR_SIMULATION=ON` and target hardware.

## Claude Code interaction norms

### Task planning

For multi-step tasks (3+ steps, non-trivial refactoring, or multi-file changes):
- Use `TodoWrite`. Mark one item `in_progress` at a time; mark `completed` immediately after finishing.
- Use `EnterPlanMode` before starting architectural decisions or significant features.
- Use `ExitPlanMode` to present the plan for user approval before implementing.

### Asking questions

Use `AskUserQuestion` when requirements are ambiguous, multiple approaches are valid, or assumptions need validation. Do not ask "Is this plan okay?" — use `ExitPlanMode` instead.

### Documentation discipline

- When work produces new project facts, update the relevant `doc/knowledge/` note.
- After bootstrap or migration passes, leave a brief note in `reviews/<date>-<topic>.md`.
- Keep `working-set.md` and `todo.md` current when focus shifts.
- Do not mechanically copy legacy docs into `doc/knowledge/` without verifying currency.

### What not to do

- Do not create empty directories without guide files.
- Do not write long essays into `working-set.md`.
- Do not leave `todo.md` or `working-set.md` stale after major tasks.
