#pragma once
// wm_ingest — m2 lean upload：为一个 quickSnap 工作目录构建（或复用）上传用 desc。
//
// 新设计（doc/design/workmode-m2-workunit-handoff.md）：quickSnap 不再写 info.json；
// wm 直接扫工作目录的白名单媒体 → manifest::createDescInfoFile 建 desc 到
// <workDir>/<basename>.json，供 UploadTask 扫描上传。desc 已存在则复用（断点续传，
// 保 F_UploadedTag，不重建、不读传感器）。

#include <string>

namespace app_workmode {

// 为工作目录 workDir 确保 desc 存在：
//   desc 路径 = <workDir 去尾斜杠>/<basename>.json（basename = workDir 最后一段，如 <ts>）。
//   - desc 已存在 → 直接复用（续传），不重建、不读传感器。
//   - desc 不存在 → 扫 workDir 下白名单媒体（jpg/jpeg/mp4，大小写不敏感），有则
//     manifest::createDescInfoFile 建 desc；无媒体则不建（跳过）。
//   workDir 尾部带不带 '/' 均可（内部归一化）。
// descPathOut（可选）= 最终 desc 路径（建成/已存在时写入）。
// Returns: 1 = desc 就绪（新建或复用），0 = 无可传媒体（未建 desc），-1 = 建 desc 失败。
int ensureWorkDirDesc(const std::string& workDir, std::string* descPathOut = nullptr);

}  // namespace app_workmode
