#ifndef CAMERA_RECORDER_H
#define CAMERA_RECORDER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../media/audio/AudioParams.h"
#include "../../media/video/VideoParams.h"
#include "../../media/video/VideoRecorder.h"

namespace service {
namespace camera {

/**
 * @brief Recording error categories.
 *
 * Returned via RecordResult.error so callers can distinguish failure types
 * programmatically (i18n-safe, no string matching).
 */
enum class RecordError {
    None = 0,                  // 录影成功完成或主动 stop（不视为失败）
    InsufficientDiskSpace,     // 磁盘空间不足，autoCover=0 或覆盖后仍不足
    EncoderInitFailed,         // VideoRecorder 初始化失败
    RecordStartFailed,         // VideoRecorder::record() 启动失败
    InternalError,             // 内部异常（mutex/cv/thread 等）
    UserStop                   // 用户主动调用 stop()（stoppedManually=true 时填这个）
};

/**
 * @brief Options passed to CameraRecorder::record().
 */
struct RecordOptions {
    /**
     * @brief Whether to record audio.
     *
     * - true  : 启用音频（AAC, 16kHz, mono, 音量/增益从 Settings 读）
     * - false : 禁用音频（HTTP `--no-audio` 场景）
     *
     * 默认 true。
     */
    bool audio = true;

    /**
     * @brief Whether to allow deleting oldest videos to free disk space.
     *
     * - true  : 磁盘不足时循环删除最旧 video 腾空间
     * - false : 磁盘不足时直接拒绝录影
     *
     * 不显式设置时，CameraRecorder 读 Settings::autoCover 作为默认值。
     */
    bool autoCover = false;

    /**
     * @brief 录影结束回调（异步触发，在 VideoRecorder 后台线程执行）。
     *
     * 必传。caller 不应在回调内做阻塞操作；如需阻塞请用 std::promise/future 桥接。
     */
    std::function<void(const RecordResult&)> onComplete;
};

/**
 * @brief Result of a recording session, delivered via RecordOptions::onComplete.
 *
 * 字段精简：不含 codec/fps/bitrate/rcMode 等"实际生效参数"（无消费者，按 KISS 原则删除）。
 */
struct RecordResult {
    RecordError error = RecordError::None;
    std::string filePath;          // 录影文件路径（与 caller 传入一致）
    std::string thumbnailPath;     // 缩略图路径（<video dir>/thumb/<basename>.jpg）
    int64_t durationMs = 0;        // 实际录了多久（毫秒）
    int64_t fileSizeBytes = 0;     // 录影文件大小
    bool stoppedManually = false;  // 是否被 stop() 主动停止
    std::string errorMessage;      // error != None 时的详细错误信息
};

/**
 * @brief 通用录影器（策略/编排层）
 *
 * 包裹 media::VideoRecorder，添加：
 * - 读 CameraPropertyService 配置（视频参数、时长、音频音量/增益、autoCover）
 * - 磁盘空间检查 + autoCover 循环覆盖
 * - 异步 + 回调 API
 * - 主动 stop() 支持
 * - 状态查询（getCurrentDurationMs）
 * - 缩略图路径自动生成
 *
 * 不做：
 * - 不直接调用 SDK
 * - 不负责写 desc JSON / DB（由 RecordingPostProcess 处理）
 * - 不做 desc schema 变更
 *
 * 详见 doc/knowledge/specs/camera-recorder-unified-design.md § 2-7。
 */
class CameraRecorder {
public:
    CameraRecorder();
    ~CameraRecorder();

    // 禁用拷贝（每个 CameraRecorder 持有一个活跃 VideoRecorder）
    CameraRecorder(const CameraRecorder&) = delete;
    CameraRecorder& operator=(const CameraRecorder&) = delete;

    /**
     * @brief 异步启动录影。
     *
     * @param filePath    录影文件输出路径
     * @param durationSec 录影时长（秒）；<= 0 时降级到 CameraPropertyService::getVideoRecordLength()
     * @param options     配置选项（autoCover + onComplete）
     *
     * @return true  = 已成功启动，录影在后台进行，结束后通过 onComplete 回调通知
     *         false = 启动失败（已在录制 / 磁盘不足 / 编码器初始化失败等），onComplete 不会触发
     */
    bool record(const std::string& filePath,
                int durationSec,
                const RecordOptions& options);

    /**
     * @brief 异步停止录影。
     *
     * @return true  = stop 信号已发出，MP4 收尾在后台完成后触发 onComplete（stoppedManually=true）
     *         false = 当前未在录制（no-op + 警告日志）
     */
    bool stop();

    /**
     * @brief 获取当前录影已耗时（毫秒）—— HTTP GET /video/status 调用。
     *
     * @return 当前 durationMs；未录制时返回 0
     */
    int64_t getCurrentDurationMs() const;

    /**
     * @brief 获取内部 VideoRecorder 抓取的缩略图 JPEG 数据（pass-through）。
     *
     * 用于 HTTP 路径在 onComplete 回调里调 `dao.saveThumbnail(filePath, data)`。
     * 未录制时返回空 vector。
     */
    const std::vector<uint8_t>& getThumbnailData() const;

    /**
     * @brief 是否抓到缩略图（pass-through）。
     */
    bool hasThumbnail() const;

private:
    static std::string computeThumbnailPath(const std::string& filePath);
    bool ensureDiskSpace(int bitrateKbps, int durationSec, bool autoCover,
                         std::string& errorMessage);
    static std::shared_ptr<media::VideoParams> buildVideoParams();
    static std::shared_ptr<media::AudioParams> buildAudioParams();

    std::shared_ptr<media::VideoRecorder> video_recorder_;
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<int64_t> current_duration_ms_{0};
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace camera
} // namespace service

#endif // CAMERA_RECORDER_H
