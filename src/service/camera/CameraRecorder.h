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
struct RecordResult;  // forward decl: onComplete below needs the type name

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

    /**
     * @brief 诊断用 bitrate 覆盖（kbps）。>0 时跳过 CameraPropertyService 直接使用此值。
     *
     * 默认 0（用 CPS 配置）。work mode 路径下，env HTC_RECORD_BITRATE_KBPS 会
     * 覆盖到此字段，用于验证 16 Mbps 编码器吞吐瓶颈假设
     * （见 doc/knowledge/bugs/T32-recording-fps-17-investigation.md §6.5）。
     */
    int bitrateKbpsOverride = 0;
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

    /**
     * @brief 显式释放底层视频录制资源（SDK 缓冲、encoder 状态等）。
     *
     * 正常情况下 CameraRecorder 析构时会自动释放。但对内存紧张的 T32 设备，
     * SDK 的帧缓冲池（~20-50MB）如果在函数返回前一直持有，会在析构瞬间
     * 触发内核把大量脏页 swap out 到 zram，可能造成 zram OOM（表现为
     * "Error allocating memory for compressed page" + "Write-error on
     * swap-device"）。在录制完成、但还要做其他不依赖 SDK 的 IO
     * （写 desc JSON、走 IIC）时，可调用本方法提前释放，避免 zram 尖峰。
     *
     * 调用后，video_recorder_ 已被 reset，hasThumbnail/getThumbnailData
     * 等会返回空/无数据；getCurrentDurationMs 返回 0。
     */
    void releaseVideoResources();

private:
    static std::string computeThumbnailPath(const std::string& filePath);
    bool ensureDiskSpace(int bitrateKbps, int durationSec, bool autoCover,
                         std::string& errorMessage);
    static std::shared_ptr<media::VideoParams> buildVideoParams(int bitrateKbpsOverride = 0);
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
