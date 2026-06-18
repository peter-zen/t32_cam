# T8 Tester Evidence — Phase A (docs + inventory only)

Task: T8 (Phase A: documentation + capability inventory ONLY — no source)
Node: tester (feature flow)
Worktree: `/home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode`
Branch: `feature/new-workmode`
Date: 2026-06-17

## Pass/Fail Summary

| # | Gate | Result |
|---|------|--------|
| 1 | No-source-change (zero changes under `src/` / `CMakeLists.txt`) | PASS |
| 2 | Both design docs exist & non-empty | PASS |
| 3 | Inventory covers all 5 `-wm` sub-modes | PASS |
| 4 | Both overlap callouts (`TEST_ONLY(3)≡-m`, `UVC(4) RTSP≡-rs`) | PASS |
| 5 | All 8 capabilities present | PASS |
| 6 | "Biggest gap" callout (`generateDescInfo`/`createDescInfoFile`) | PASS |
| 7 | Mermaid layering diagram in architecture doc | PASS |
| 8 | `working-set.md` pointer | PASS |
| 9 | Markdown validity (tables/fences consistent) | PASS |
| 10 | file:line accuracy spot-checks | PASS |

Overall: **PASS (10/10)**

---

## Gate 1 — No-source-change (CRITICAL)

```
$ git status --short
 M doc/knowledge/working-set.md
 M orchestration-state.yaml
?? artifacts/T8-implementer-report.md
?? artifacts/T8-planner-full.md
?? artifacts/T8-planner-report.md
?? artifacts/T8-progress.yaml
?? doc/design/workmode-capability-inventory.md
?? doc/design/workmode-sdk-architecture.md
```

```
$ git diff --stat
 doc/knowledge/working-set.md | 7 +++++++
 orchestration-state.yaml     | 8 +++++++-
 2 files changed, 14 insertions(+), 1 deletion(-)
```

Source-change filter (must be empty):
```
$ git status --short | grep -E 'src/|CMakeLists'
NONE (good)
$ git diff --name-only | grep -E 'src/|CMakeLists'
NONE (good)
```

Verdict: ZERO changes under `src/` or any `CMakeLists.txt`. Phase A boundary respected.

## Gate 2 — Both design docs exist & non-empty

```
$ test -s doc/design/workmode-sdk-architecture.md && echo arch OK
arch OK
$ test -s doc/design/workmode-capability-inventory.md && echo inv OK
inv OK
```

## Gate 3 — All 5 `-wm` sub-modes present in inventory

```
SNAP_ONLY: 2
SNAP_UPLOAD: 2
UPLOAD_ONLY: 1
TEST_ONLY: 3
UVC: 4
```
All five enumerated. Inventory table §1 lists all 5 `WORKING_MODE_*` enum rows (lines 19-23).

## Gate 4 — Both overlap callouts

```
TEST_ONLY(3) ≡ -m/--mobile  -> inventory.md:27, :22 (table row)
UVC(4) RTSP ≡ -rs           -> inventory.md:30, :23 (table row)
```
Both explicit overlap callouts present with file:line cross-references.

## Gate 5 — All 8 capabilities present

Inventory §2 headings (lines 52-151):
```
### (1) Network / WiFi / DHCP connect      :52
### (2) NTP sync                            :68
### (3) 拍照 + 可选缩略图 (photo + thumb)    :80
### (4) 录影 + 可选缩略图 (video + thumb)    :94
### (5) mDNS discovery                      :109
### (6) RTSP server                         :122
### (7) SQLite DB 文件信息                    :138
### (8) JSON manifest 生成 + server 上传     :151
```
All 8 capabilities enumerated as dedicated subsections.

## Gate 6 — "Biggest gap" callout

```
inventory.md:44  -> generateDescInfo ... 能力 3 和 7 ... 能力 8 才是紧耦合
inventory.md:169 -> 缺口（最大）：generateDescInfo/createDescInfoFile 必须变成一个新能力库
inventory.md:175 -> ## 3. 最大缺口（callout）
inventory.md:177 -> generateDescInfo(:268) 与 createDescInfoFile(:453) 是八项能力中...
```
`generateDescInfo` / `createDescInfoFile` explicitly flagged as the biggest extraction gap.

## Gate 7 — Mermaid layering diagram in architecture doc

```
$ grep -c mermaid doc/design/workmode-sdk-architecture.md
1
```
Fenced `mermaid` block spans architecture.md:61-93, properly opened/closed. Diagram shows Apps → Orch (mode orchestration) → Caps (capability libs) → HAL layering.

## Gate 8 — working-set.md pointer

```
$ grep -n workmode-sdk-architecture doc/knowledge/working-set.md
109:**参考文档**：`doc/design/workmode-sdk-architecture.md`（方向 + 路线图），`doc/design/workmode-capability-inventory.md`（能力清单 + 缺口）
```

## Gate 9 — Markdown validity

Table structural column count (ignoring bitwise-OR pipes inside inline code spans):
```
17: 4 cols   (header)
18: 4 cols   (separator)
19: 4 cols
20: 4 cols
21: 4 cols
22: 4 cols
23: 4 cols
```
All rows consistently 4 columns. Fenced blocks: architecture.md has 2 fence markers (1 well-formed mermaid block); inventory.md has 0 fences (pure prose, valid). No broken tables/fences.

## Gate 10 — file:line accuracy spot-checks

| Cite | Expected | Actual | OK |
|------|----------|--------|----|
| `WORKING_MODE_SNAP_ONLY` case | `main_app.cpp:1167` | `1167: case WORKING_MODE_SNAP_ONLY:` | YES |
| `generateDescInfo` | `main_app.cpp:268` | `268: static int generateDescInfo(...)` | YES |
| `createDescInfoFile` | `main_app.cpp:453` | `453: static int createDescInfoFile(...)` | YES |
| `htc_main_app -wm` spawn | `media_app.cpp:257` | `257: std::string command = "htc_main_app -wm " ...` | YES |
| `libstorage` STATIC | a STATIC storage lib | `src/service/storage/CMakeLists.txt:6: add_library(storage_service STATIC ${SOURCES})` | YES (named `storage_service`, STATIC) |

All cited anchors resolve to the referenced symbols. (Note: the actual cmake target is `storage_service`; the inventory's prose "storage" shorthand refers to this same library.)

---

## Residual risk / notes
- This is a documentation-only deliverable; there is no code to compile or unit-test. The "test suite" is structural/content validation of the docs plus the no-source-change gate.
- file:line numbers are accurate as of the current `src/` tree; the reviewer node will do deeper semantic review of the gap analysis and roadmap.
- The storage library cmake target is named `storage_service` (not literally `storage`); doc prose uses `storage` as shorthand — not an error, but worth noting for Phase B implementer to target the correct `add_library(storage_service ...)` line.
