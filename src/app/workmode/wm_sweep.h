#pragma once
// wm_sweep — m2 兜底扫描：收集 /tmp/media/ 下滞留的 quickSnap 工作目录（其它 <ts>/，
// 排除本次 -d 传入的目录），供 wm 补传崩溃/重连遗留。独立、可开关。
// 见 doc/design/workmode-m2-workunit-handoff.md §6。

#include <string>
#include <vector>

namespace app_workmode {

// 兜底扫描是否启用。读 HTC_WM_SWEEP_STRANDED：默认开（仅显式 "0" 关）。
bool sweepEnabled();

// SD 落卡续传是否启用。读 HTC_WM_SD_FALLBACK：默认开（仅显式 "0" 关）。
// 同时 gate persist（超时落卡 /tmp/media→SD）与 resume（下次启动扫 SD 滞留续传）。
// 见 doc/knowledge/specs/wm-upload-sd-fallback.md §4 Q6 / §5.2。
bool sdFallbackEnabled();

// 收集 baseDir 下滞留的工作目录：
//   - 仅直接子目录，名字匹配 ^\d{8}_\d{6}$（quickSnap 时间戳目录 YYYYMMDD_HHMMSS）。
//   - 排除 excludeDir（本次 -d 目录，按 basename 比较；空则不排除）。
//   - 升序（老先传）；每个返回串为「baseDir/<name>/」全路径带尾斜杠
//     （满足 UploadTask 的 dir + 文件名 无分隔符拼接）。
//   纯收集器，不碰上传。命中时打一行 [wm] sweep 日志。
std::vector<std::string> collectStrandedWorkDirs(const std::string& baseDir,
                                                 const std::string& excludeDir);

}  // namespace app_workmode
