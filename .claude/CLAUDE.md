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

## Quick build reference

PC simulation:
```bash
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
```

T32 hardware (requires toolchain at `toolchain/mips-gcc540-glibc222-r3.3.7.mxu2.cve/`; if absent, extract from `ref/Tassadar-T32-1.0.6-20250613.7z`):
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)
```

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
