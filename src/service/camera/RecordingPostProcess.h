#ifndef RECORDING_POST_PROCESS_H
#define RECORDING_POST_PROCESS_H

#include <string>

namespace service {
namespace camera {

/**
 * @brief 录影后置处理工具类（静态方法工具集）
 *
 * 设计原则：只做"落盘"这一件事，不参与录影编排，不生成 desc JSON 内容。
 *
 * - `writeWorkModeDescJson` 负责把 caller 提供的 JSON 字符串写入 desc 文件
 * - `generateDescInfo` 由 caller（main_app）负责（依赖 MCU/Disk/CRC/Timezone 等模块，
 *   不宜迁到 camera_service 库）
 *
 * 详见 doc/knowledge/specs/camera-recorder-unified-design.md § 8。
 */
class RecordingPostProcess {
public:
    /**
     * @brief 把 desc JSON 内容写入文件（work mode 用）
     *
     * @param jsonContent 已生成好的 desc JSON 字符串
     * @param filePath    desc 文件的完整输出路径
     *
     * 行为：
     * 1. 父目录不存在时自动创建
     * 2. 写入失败时打 ERROR 日志
     * 3. 写成功时打 INFO 日志
     *
     * 不做：
     * - 不生成 JSON 内容
     * - 不读 config
     * - 不参与录影流程
     */
    static void writeWorkModeDescJson(const std::string& jsonContent,
                                       const std::string& filePath);

private:
    // 工具类不允许实例化
    RecordingPostProcess() = delete;
    ~RecordingPostProcess() = delete;
    RecordingPostProcess(const RecordingPostProcess&) = delete;
    RecordingPostProcess& operator=(const RecordingPostProcess&) = delete;
};

} // namespace camera
} // namespace service

#endif // RECORDING_POST_PROCESS_H
