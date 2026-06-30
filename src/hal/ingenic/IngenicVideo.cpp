#include "IngenicVideo.h"
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_osd.h>
#include <imp/imp_encoder.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <map>
#include <vector>
#include "Logger.h"
#include "sensor-config.h"
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
namespace hal {

// --- hal/ IMP trace sink (debug only; env HTC_HAL_TRACE=1) -----------------
// Writes a per-line-fsync'd trace so the last line survives a hard poweroff
// after a kernel wedge. Each IMP call is wrapped with "--> name" / "<-- name
// rc=N"; the last "-->" with no matching "<--" is the call that hung inside
// the kernel ioctl. Env-gated and off by default → zero effect on normal runs.
// Remove once cm==1 (JPEG→H264) wedge is fixed.
static std::atomic<int> g_hal_trace_enabled{-1};   // -1 unchecked, 0 off, 1 on
static std::atomic<int> g_hal_trace_seq{0};
static std::mutex       g_hal_trace_mtx;
static FILE*            g_hal_trace_fp = nullptr;

static bool hal_trace_enabled() {
    int v = g_hal_trace_enabled.load();
    if (v == -1) {
        const char* e = getenv("HTC_HAL_TRACE");
        v = (e && e[0] == '1') ? 1 : 0;
        g_hal_trace_enabled.store(v);
    }
    return v == 1;
}

static void hal_trace(const char* fmt, ...) {
    if (!hal_trace_enabled()) return;
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_hal_trace_mtx);
    if (!g_hal_trace_fp) {
        const char* envp = getenv("HTC_HAL_TRACE_PATH");
        if (envp && envp[0]) {
            g_hal_trace_fp = fopen(envp, "w");
        } else {
#ifdef BUILD_FOR_SIMULATION
            char path[256];
            const char* root = getenv("SIM_SD_ROOT");
            snprintf(path, sizeof(path), "%s/logs/hal_trace.log",
                     (root && root[0]) ? root : ".");
            g_hal_trace_fp = fopen(path, "w");
#else
            g_hal_trace_fp = fopen("/mnt/sdcard/logs/hal_trace.log", "w");
#endif
        }
    }
    if (!g_hal_trace_fp) return;
    int seq = g_hal_trace_seq.fetch_add(1);
    fprintf(g_hal_trace_fp, "%d %s\n", seq, buf);
    fflush(g_hal_trace_fp);
    fsync(fileno(g_hal_trace_fp));
}

// Hot capture-loop throttle: log every IMP call for the first HAL_LOOP_FULL
// frames (~3s), then a heartbeat every HAL_LOOP_BEAT frames, so the fsync cost
// stays bounded during a 30s record while still catching an early wedge fully.
static std::atomic<int> g_hal_loop_frame{0};
static const int HAL_LOOP_FULL = 90;
static const int HAL_LOOP_BEAT = 60;
static bool hal_trace_loop_gate() {
    if (!hal_trace_enabled()) return false;
    int fr = g_hal_loop_frame.load();
    return fr < HAL_LOOP_FULL || (fr % HAL_LOOP_BEAT == 0);
}

static std::atomic<int> g_video_init_ref_count{0};

static std::mutex g_group_mutex;
static std::map<int, int> g_group_ref_count;

static bool acquireGroup(int group_id) {
    std::lock_guard<std::mutex> lock(g_group_mutex);
    auto it = g_group_ref_count.find(group_id);
    if (it != g_group_ref_count.end()) {
        it->second++;
        Logger::log(LogLevel::DEBUG, "[HAL] acquireGroup(%d): shared, ref=%d", group_id, it->second);
        return true;
    }
    hal_trace("--> IMP_Encoder_CreateGroup g=%d", group_id);
    int cg_rc = IMP_Encoder_CreateGroup(group_id);
    hal_trace("<-- IMP_Encoder_CreateGroup g=%d rc=%d", group_id, cg_rc);
    if (cg_rc < 0) {
        Logger::log(LogLevel::WARNING, "[HAL] acquireGroup(%d): CreateGroup failed, assuming already exists", group_id);
    }
    g_group_ref_count[group_id] = 1;
    Logger::log(LogLevel::DEBUG, "[HAL] acquireGroup(%d): acquired, ref=1", group_id);
    return true;
}

static void releaseGroup(int group_id) {
    std::lock_guard<std::mutex> lock(g_group_mutex);
    auto it = g_group_ref_count.find(group_id);
    if (it == g_group_ref_count.end()) {
        Logger::log(LogLevel::WARNING, "[HAL] releaseGroup(%d): not found", group_id);
        return;
    }
    it->second--;
    if (it->second <= 0) {
        hal_trace("--> IMP_Encoder_DestroyGroup g=%d", group_id);
        int dg_rc = IMP_Encoder_DestroyGroup(group_id);
        hal_trace("<-- IMP_Encoder_DestroyGroup g=%d rc=%d", group_id, dg_rc);
        g_group_ref_count.erase(it);
        Logger::log(LogLevel::DEBUG, "[HAL] releaseGroup(%d): destroyed", group_id);
    } else {
        Logger::log(LogLevel::DEBUG, "[HAL] releaseGroup(%d): shared, ref=%d", group_id, it->second);
    }
}

static std::mutex g_bind_mutex;
static std::map<int, int> g_bind_ref_count;

static bool acquireBind(int group_id, IMPCell* fs_cell, IMPCell* enc_cell) {
    std::lock_guard<std::mutex> lock(g_bind_mutex);
    auto it = g_bind_ref_count.find(group_id);
    if (it != g_bind_ref_count.end()) {
        it->second++;
        Logger::log(LogLevel::DEBUG, "[HAL] acquireBind(%d): shared, ref=%d", group_id, it->second);
        return true;
    }
    hal_trace("--> IMP_System_Bind fs(g=%d,o=%d)->enc(g=%d,o=%d)",
              fs_cell->groupID, fs_cell->outputID, enc_cell->groupID, enc_cell->outputID);
    int b_rc = IMP_System_Bind(fs_cell, enc_cell);
    hal_trace("<-- IMP_System_Bind g=%d rc=%d", group_id, b_rc);
    if (b_rc < 0) {
        Logger::log(LogLevel::WARNING, "[HAL] acquireBind(%d): Bind failed, assuming already bound", group_id);
    }
    g_bind_ref_count[group_id] = 1;
    Logger::log(LogLevel::DEBUG, "[HAL] acquireBind(%d): bound, ref=1", group_id);
    return true;
}

static void releaseBind(int group_id, IMPCell* fs_cell, IMPCell* enc_cell) {
    std::lock_guard<std::mutex> lock(g_bind_mutex);
    auto it = g_bind_ref_count.find(group_id);
    if (it == g_bind_ref_count.end()) {
        Logger::log(LogLevel::WARNING, "[HAL] releaseBind(%d): not found", group_id);
        return;
    }
    it->second--;
    if (it->second <= 0) {
        hal_trace("--> IMP_System_UnBind fs(g=%d,o=%d)->enc(g=%d,o=%d)",
                  fs_cell->groupID, fs_cell->outputID, enc_cell->groupID, enc_cell->outputID);
        int ub_rc = IMP_System_UnBind(fs_cell, enc_cell);
        hal_trace("<-- IMP_System_UnBind g=%d rc=%d", group_id, ub_rc);
        g_bind_ref_count.erase(it);
        Logger::log(LogLevel::DEBUG, "[HAL] releaseBind(%d): unbound", group_id);
    } else {
        Logger::log(LogLevel::DEBUG, "[HAL] releaseBind(%d): shared, ref=%d", group_id, it->second);
    }
}

static std::mutex g_chn_mutex;
static std::map<int, int> g_chn_ref_count;

// CreateChn + RegisterChn，ref-counted 幂等：解决同 chn 多 stream 重复 CreateChn 冲突
// （如 prewarm thumb enc14 + record thumb enc14）。第一次真正 CreateChn+RegisterChn，后续 ref++。
static bool acquireChn(int group, int chn, const IMPEncoderCHNAttr* attr) {
    std::lock_guard<std::mutex> lock(g_chn_mutex);
    auto it = g_chn_ref_count.find(chn);
    if (it != g_chn_ref_count.end()) {
        it->second++;
        Logger::log(LogLevel::DEBUG, "[HAL] acquireChn(chn=%d): shared, ref=%d", chn, it->second);
        return true;
    }
    hal_trace("--> IMP_Encoder_CreateChn chn=%d", chn);
    int cc_rc = IMP_Encoder_CreateChn(chn, attr);
    hal_trace("<-- IMP_Encoder_CreateChn chn=%d rc=%d", chn, cc_rc);
    if (cc_rc < 0) {
        Logger::log(LogLevel::ERROR, "[HAL] acquireChn(chn=%d): CreateChn failed", chn);
        return false;
    }
    hal_trace("--> IMP_Encoder_RegisterChn g=%d chn=%d", group, chn);
    int rg_rc = IMP_Encoder_RegisterChn(group, chn);
    hal_trace("<-- IMP_Encoder_RegisterChn g=%d chn=%d rc=%d", group, chn, rg_rc);
    if (rg_rc < 0) {
        Logger::log(LogLevel::ERROR, "[HAL] acquireChn(chn=%d): RegisterChn(g=%d) failed", chn, group);
        IMP_Encoder_DestroyChn(chn);
        return false;
    }
    g_chn_ref_count[chn] = 1;
    Logger::log(LogLevel::DEBUG, "[HAL] acquireChn(chn=%d): created+registered, ref=1", chn);
    return true;
}

// ref--→0 才 UnRegisterChn + DestroyChn（常驻/共享 channel 时其他 stream 仍用，不拆）。
static void releaseChn(int chn) {
    std::lock_guard<std::mutex> lock(g_chn_mutex);
    auto it = g_chn_ref_count.find(chn);
    if (it == g_chn_ref_count.end()) {
        Logger::log(LogLevel::WARNING, "[HAL] releaseChn(chn=%d): not found", chn);
        return;
    }
    it->second--;
    if (it->second <= 0) {
        hal_trace("--> IMP_Encoder_UnRegisterChn chn=%d", chn);
        int ur_rc = IMP_Encoder_UnRegisterChn(chn);
        hal_trace("<-- IMP_Encoder_UnRegisterChn chn=%d rc=%d", chn, ur_rc);
        hal_trace("--> IMP_Encoder_DestroyChn chn=%d", chn);
        int de_rc = IMP_Encoder_DestroyChn(chn);
        hal_trace("<-- IMP_Encoder_DestroyChn chn=%d rc=%d", chn, de_rc);
        g_chn_ref_count.erase(it);
        Logger::log(LogLevel::DEBUG, "[HAL] releaseChn(chn=%d): destroyed", chn);
    } else {
        Logger::log(LogLevel::DEBUG, "[HAL] releaseChn(chn=%d): shared, ref=%d", chn, it->second);
    }
}

static std::mutex g_fs_mutex;
static std::map<int, int> g_fs_ref_count;

static bool acquireFrameSource(int group_id, bool* out_first = nullptr) {
    std::lock_guard<std::mutex> lock(g_fs_mutex);
    auto it = g_fs_ref_count.find(group_id);
    if (it != g_fs_ref_count.end()) {
        it->second++;
        if (out_first) *out_first = false;
        Logger::log(LogLevel::DEBUG, "[HAL] acquireFrameSource(%d): shared, ref=%d", group_id, it->second);
        return true;
    }
    hal_trace("--> IMP_FrameSource_EnableChn g=%d", group_id);
    int ec_rc = IMP_FrameSource_EnableChn(group_id);
    hal_trace("<-- IMP_FrameSource_EnableChn g=%d rc=%d", group_id, ec_rc);
    if (ec_rc < 0) {
        Logger::log(LogLevel::ERROR, "[HAL] acquireFrameSource(%d): EnableChn failed", group_id);
        if (out_first) *out_first = false;
        return false;
    }
    g_fs_ref_count[group_id] = 1;
    if (out_first) *out_first = true;
    Logger::log(LogLevel::DEBUG, "[HAL] acquireFrameSource(%d): enabled, ref=1", group_id);
    return true;
}

static bool releaseFrameSource(int group_id, bool* out_last = nullptr) {
    std::lock_guard<std::mutex> lock(g_fs_mutex);
    auto it = g_fs_ref_count.find(group_id);
    if (it == g_fs_ref_count.end()) {
        Logger::log(LogLevel::WARNING, "[HAL] releaseFrameSource(%d): not found", group_id);
        if (out_last) *out_last = false;
        return true;
    }
    it->second--;
    if (it->second <= 0) {
        hal_trace("--> IMP_FrameSource_DisableChn g=%d", group_id);
        int dc_rc = IMP_FrameSource_DisableChn(group_id);
        hal_trace("<-- IMP_FrameSource_DisableChn g=%d rc=%d", group_id, dc_rc);
        g_fs_ref_count.erase(it);
        if (out_last) *out_last = true;
        Logger::log(LogLevel::DEBUG, "[HAL] releaseFrameSource(%d): disabled", group_id);
        return true;
    }
    if (out_last) *out_last = false;
    Logger::log(LogLevel::DEBUG, "[HAL] releaseFrameSource(%d): shared, ref=%d", group_id, it->second);
    return true;
}

static std::vector<SensorConfig> loadSensors() {
    std::vector<SensorConfig> s;
    int num = SENSOR_NUM;
    if (num > IMPISP_TOTAL_ONE) num = 2;
    if (num > IMPISP_TOTAL_TWO) num = 3;
    if (num > IMPISP_TOTAL_THR) num = 4;
    SensorConfig c0;
    memset(&c0, 0, sizeof(c0));
    strncpy(c0.name, FIRST_SNESOR_NAME, sizeof(c0.name)-1);
    c0.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(c0.i2c.type, FIRST_SNESOR_NAME, sizeof(c0.i2c.type)-1);
    c0.i2c.addr = FIRST_I2C_ADDR;
    c0.i2c.adapter_id = FIRST_I2C_ADAPTER_ID;
    c0.gpio.rst = FIRST_RST_GPIO;
    c0.gpio.pwdn = FIRST_PWDN_GPIO;
    c0.gpio.power = FIRST_POWER_GPIO;
    c0.gpio.sw = FIRST_SWITCH_GPIO;
    c0.gpio.sw_state = 0;
    c0.sensor_id = FIRST_SENSOR_ID;
    c0.video_interface = FIRST_VIDEO_INTERFACE;
    c0.mclk = FIRST_MCLK;
    c0.default_boot = FIRST_DEFAULT_BOOT;
    c0.fps.num = FIRST_SENSOR_FRAME_RATE_NUM;
    c0.fps.den = FIRST_SENSOR_FRAME_RATE_DEN;
    c0.width = FIRST_SENSOR_WIDTH;
    c0.height = FIRST_SENSOR_HEIGHT;
    s.push_back(c0);
    if (num > 1) {
        SensorConfig c1;
        memset(&c1, 0, sizeof(c1));
        strncpy(c1.name, SECOND_SNESOR_NAME, sizeof(c1.name)-1);
        c1.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
        strncpy(c1.i2c.type, SECOND_SNESOR_NAME, sizeof(c1.i2c.type)-1);
        c1.i2c.addr = SECOND_I2C_ADDR;
        c1.i2c.adapter_id = SECOND_I2C_ADAPTER_ID;
        c1.gpio.rst = SECOND_RST_GPIO;
        c1.gpio.pwdn = SECOND_PWDN_GPIO;
        c1.gpio.power = SECOND_POWER_GPIO;
        c1.gpio.sw = SECOND_SWITCH_GPIO;
        c1.gpio.sw_state = 0;
        c1.sensor_id = SECOND_SENSOR_ID;
        c1.video_interface = SECOND_VIDEO_INTERFACE;
        c1.mclk = SECOND_MCLK;
        c1.default_boot = SECOND_DEFAULT_BOOT;
        c1.fps.num = SECOND_SENSOR_FRAME_RATE_NUM;
        c1.fps.den = SECOND_SENSOR_FRAME_RATE_DEN;
        c1.width = SECOND_SENSOR_WIDTH;
        c1.height = SECOND_SENSOR_HEIGHT;
        s.push_back(c1);
    }
    if (num > 2) {
        SensorConfig c2;
        memset(&c2, 0, sizeof(c2));
        strncpy(c2.name, THIRD_SNESOR_NAME, sizeof(c2.name)-1);
        c2.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
        strncpy(c2.i2c.type, THIRD_SNESOR_NAME, sizeof(c2.i2c.type)-1);
        c2.i2c.addr = THIRD_I2C_ADDR;
        c2.i2c.adapter_id = THIRD_I2C_ADAPTER_ID;
        c2.gpio.rst = THIRD_RST_GPIO;
        c2.gpio.pwdn = THIRD_PWDN_GPIO;
        c2.gpio.power = THIRD_POWER_GPIO;
        c2.gpio.sw = THIRD_SWITCH_GPIO;
        c2.gpio.sw_state = 0;
        c2.sensor_id = THIRD_SENSOR_ID;
        c2.video_interface = THIRD_VIDEO_INTERFACE;
        c2.mclk = THIRD_MCLK;
        c2.default_boot = THIRD_DEFAULT_BOOT;
        c2.fps.num = THIRD_SENSOR_FRAME_RATE_NUM;
        c2.fps.den = THIRD_SENSOR_FRAME_RATE_DEN;
        c2.width = THIRD_SENSOR_WIDTH;
        c2.height = THIRD_SENSOR_HEIGHT;
        s.push_back(c2);
    }
    if (num > 3) {
        SensorConfig c3;
        memset(&c3, 0, sizeof(c3));
        strncpy(c3.name, FOURTH_SNESOR_NAME, sizeof(c3.name)-1);
        c3.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
        strncpy(c3.i2c.type, FOURTH_SNESOR_NAME, sizeof(c3.i2c.type)-1);
        c3.i2c.addr = FOURTH_I2C_ADDR;
        c3.i2c.adapter_id = FOURTH_I2C_ADAPTER_ID;
        c3.gpio.rst = FOURTH_RST_GPIO;
        c3.gpio.pwdn = FOURTH_PWDN_GPIO;
        c3.gpio.power = FOURTH_POWER_GPIO;
        c3.gpio.sw = FOURTH_SWITCH_GPIO;
        c3.gpio.sw_state = 0;
        c3.sensor_id = FOURTH_SENSOR_ID;
        c3.video_interface = FOURTH_VIDEO_INTERFACE;
        c3.mclk = FOURTH_MCLK;
        c3.default_boot = FOURTH_DEFAULT_BOOT;
        c3.fps.num = FOURTH_SENSOR_FRAME_RATE_NUM;
        c3.fps.den = FOURTH_SENSOR_FRAME_RATE_DEN;
        c3.width = FOURTH_SENSOR_WIDTH;
        c3.height = FOURTH_SENSOR_HEIGHT;
        s.push_back(c3);
    }
    return s;
}
static unsigned int getChEnableByIndex(int idx) {
    switch(idx) {
        case 0: return CHN0_EN;
        case 1: return CHN1_EN;
        case 2: return CHN2_EN;
        case 3: return CHN3_EN;
        case 4: return CHN4_EN;
        case 5: return CHN5_EN;
        case 6: return CHN6_EN;
        case 7: return CHN7_EN;
        case 8: return CHN8_EN;
        case 9: return CHN9_EN;
        case 10: return CHN10_EN;
        case 11: return CHN11_EN;
    }
    return 0;
}
static void fillFsAttrForOutput(IMPFSChnAttr* a, int sensorIndex, int outputIndex) {
    memset(a, 0, sizeof(IMPFSChnAttr));
    a->pixFmt = PIX_FMT_NV12;
    a->nrVBs = 2;
    a->type = FS_PHY_CHANNEL;
    if (sensorIndex == 0) {
        if (outputIndex == 0) {
            a->outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = FIRST_CROP_EN;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FIRST_SENSOR_WIDTH;
            a->crop.height = FIRST_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = FIRST_SENSOR_WIDTH;
            a->scaler.outheight = FIRST_SENSOR_HEIGHT;
            a->picWidth = FIRST_SENSOR_WIDTH;
            a->picHeight = FIRST_SENSOR_HEIGHT;
        } else if (outputIndex == 1) {
            a->outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FIRST_SENSOR_WIDTH;
            a->crop.height = FIRST_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = FIRST_SENSOR_WIDTH_SECOND;
            a->scaler.outheight = FIRST_SENSOR_HEIGHT_SECOND;
            a->picWidth = FIRST_SENSOR_WIDTH;
            a->picHeight = FIRST_SENSOR_HEIGHT;
        } else {
            a->outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FIRST_SENSOR_WIDTH_THIRD;
            a->crop.height = FIRST_SENSOR_HEIGHT_THIRD;
            a->scaler.enable = 1;
            a->scaler.outwidth = FIRST_SENSOR_WIDTH_THIRD;
            a->scaler.outheight = FIRST_SENSOR_HEIGHT_THIRD;
            a->picWidth = FIRST_SENSOR_WIDTH_THIRD;
            a->picHeight = FIRST_SENSOR_HEIGHT_THIRD;
        }
    } else if (sensorIndex == 1) {
        if (outputIndex == 0) {
            a->outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = SECOND_CROP_EN;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = SECOND_SENSOR_WIDTH;
            a->crop.height = SECOND_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = SECOND_SENSOR_WIDTH;
            a->scaler.outheight = SECOND_SENSOR_HEIGHT;
            a->picWidth = SECOND_SENSOR_WIDTH;
            a->picHeight = SECOND_SENSOR_HEIGHT;
        } else if (outputIndex == 1) {
            a->outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = SECOND_SENSOR_WIDTH;
            a->crop.height = SECOND_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = SECOND_SENSOR_WIDTH_SECOND;
            a->scaler.outheight = SECOND_SENSOR_HEIGHT_SECOND;
            a->picWidth = SECOND_SENSOR_WIDTH;
            a->picHeight = SECOND_SENSOR_HEIGHT;
        } else {
            a->outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = SECOND_SENSOR_WIDTH_THIRD;
            a->crop.height = SECOND_SENSOR_HEIGHT_THIRD;
            a->scaler.enable = 1;
            a->scaler.outwidth = SECOND_SENSOR_WIDTH_THIRD;
            a->scaler.outheight = SECOND_SENSOR_HEIGHT_THIRD;
            a->picWidth = SECOND_SENSOR_WIDTH_THIRD;
            a->picHeight = SECOND_SENSOR_HEIGHT_THIRD;
        }
    } else if (sensorIndex == 2) {
        if (outputIndex == 0) {
            a->outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = THIRD_CROP_EN;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = THIRD_SENSOR_WIDTH;
            a->crop.height = THIRD_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = THIRD_SENSOR_WIDTH;
            a->scaler.outheight = THIRD_SENSOR_HEIGHT;
            a->picWidth = THIRD_SENSOR_WIDTH;
            a->picHeight = THIRD_SENSOR_HEIGHT;
        } else if (outputIndex == 1) {
            a->outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = THIRD_SENSOR_WIDTH;
            a->crop.height = THIRD_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = THIRD_SENSOR_WIDTH_SECOND;
            a->scaler.outheight = THIRD_SENSOR_HEIGHT_SECOND;
            a->picWidth = THIRD_SENSOR_WIDTH;
            a->picHeight = THIRD_SENSOR_HEIGHT;
        } else {
            a->outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = THIRD_SENSOR_WIDTH_THIRD;
            a->crop.height = THIRD_SENSOR_HEIGHT_THIRD;
            a->scaler.enable = 1;
            a->scaler.outwidth = THIRD_SENSOR_WIDTH_THIRD;
            a->scaler.outheight = THIRD_SENSOR_HEIGHT_THIRD;
            a->picWidth = THIRD_SENSOR_WIDTH_THIRD;
            a->picHeight = THIRD_SENSOR_HEIGHT_THIRD;
        }
    } else if (sensorIndex == 3) {
        if (outputIndex == 0) {
            a->outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = FOURTH_CROP_EN;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FOURTH_SENSOR_WIDTH;
            a->crop.height = FOURTH_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = FOURTH_SENSOR_WIDTH;
            a->scaler.outheight = FOURTH_SENSOR_HEIGHT;
            a->picWidth = FOURTH_SENSOR_WIDTH;
            a->picHeight = FOURTH_SENSOR_HEIGHT;
        } else if (outputIndex == 1) {
            a->outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FOURTH_SENSOR_WIDTH;
            a->crop.height = FOURTH_SENSOR_HEIGHT;
            a->scaler.enable = 1;
            a->scaler.outwidth = FOURTH_SENSOR_WIDTH_SECOND;
            a->scaler.outheight = FOURTH_SENSOR_HEIGHT_SECOND;
            a->picWidth = FOURTH_SENSOR_WIDTH;
            a->picHeight = FOURTH_SENSOR_HEIGHT;
        } else {
            a->outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM;
            a->outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN;
            a->crop.enable = 0;
            a->crop.top = 0;
            a->crop.left = 0;
            a->crop.width = FOURTH_SENSOR_WIDTH_THIRD;
            a->crop.height = FOURTH_SENSOR_HEIGHT_THIRD;
            a->scaler.enable = 1;
            a->scaler.outwidth = FOURTH_SENSOR_WIDTH_THIRD;
            a->scaler.outheight = FOURTH_SENSOR_HEIGHT_THIRD;
            a->picWidth = FOURTH_SENSOR_WIDTH_THIRD;
            a->picHeight = FOURTH_SENSOR_HEIGHT_THIRD;
        }
    }
}
static std::vector<ChannelConfig> buildChannels(const std::vector<SensorConfig>& sensors) {
    std::vector<ChannelConfig> v;
    int num = sensors.size();
    for (int s = 0; s < num; ++s) {
        for (int o = 0; o < 3; ++o) {
            int idx = s * 3 + o;
            ChannelConfig c;
            memset(&c, 0, sizeof(c));
            c.index = idx;
            c.sensor_index = s;
            c.output_index = o;
            c.enable = getChEnableByIndex(idx);
            fillFsAttrForOutput(&c.fs_attr, s, o);
            v.push_back(c);
        }
    }
    return v;
}

int OSDController::setPoolSize(int modeSel) {
    int ret = 0;
    if(modeSel == 1) {
        ret = IMP_OSD_SetPoolSize(512*1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_OSD_SetPoolSize failed, ret=%d", ret);
    } else if(modeSel == 2) {
        ret = IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_ISP_Tuning_SetOsdPoolSize failed, ret=%d", ret);
    } else if(modeSel == 3) {
        ret = IMP_OSD_SetPoolSize(512*1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_OSD_SetPoolSize failed, ret=%d", ret);
        ret = IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_ISP_Tuning_SetOsdPoolSize failed, ret=%d", ret);
    } else {
        ret = IMP_OSD_SetPoolSize(512*1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_OSD_SetPoolSize failed, ret=%d", ret);
        ret = IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
        if (ret < 0) Logger::log(LogLevel::ERROR, "OSDController: IMP_ISP_Tuning_SetOsdPoolSize failed, ret=%d", ret);
    }
    Logger::log(LogLevel::INFO, "OSDController: setPoolSize(%d) done", modeSel);
    return 0;
}
int SensorController::openISP() {
    hal_trace("--> [HAL] init: IMP_ISP_Open");
    int rc = IMP_ISP_Open();
    hal_trace("<-- [HAL] init: IMP_ISP_Open rc=%d", rc);
    if (rc < 0) return -1;
    return 0;
}

int SensorController::closeISP() {
    hal_trace("--> [HAL] exit: closeISP IMP_ISP_DisableTuning");
    int dt_rc = IMP_ISP_DisableTuning();
    hal_trace("<-- [HAL] exit: closeISP IMP_ISP_DisableTuning rc=%d", dt_rc);
    if (dt_rc < 0) return -1;
    hal_trace("--> [HAL] exit: closeISP IMP_ISP_Close");
    int ic_rc = IMP_ISP_Close();
    hal_trace("<-- [HAL] exit: closeISP IMP_ISP_Close rc=%d", ic_rc);
    if (ic_rc != 0) return -1;
    return 0;
}

int SensorController::setCameraInputMode(const std::vector<SensorConfig>& sensors) {
    int num = sensors.size();
    mode.sensor_num = num == 1 ? IMPISP_TOTAL_ONE : num == 2 ? IMPISP_TOTAL_TWO : num == 3 ? IMPISP_TOTAL_THR : IMPISP_TOTAL_FOU;
    if (mode.sensor_num > IMPISP_TOTAL_ONE) {
        if (IMP_ISP_SetCameraInputMode(&mode) < 0) return -1;
    }
    return 0;
}
static int addOneSensor(int vi, const SensorConfig& c, IMPSensorInfo* info) {
    memset(info, 0, sizeof(IMPSensorInfo));
    strncpy(info->name, c.name, sizeof(info->name)-1);
    info->cbus_type = (IMPSensorControlBusType)c.cbus_type;
    strncpy(info->i2c.type, c.i2c.type, sizeof(info->i2c.type)-1);
    info->i2c.addr = c.i2c.addr;
    info->i2c.i2c_adapter_id = c.i2c.adapter_id;
    info->rst_gpio = c.gpio.rst;
    info->pwdn_gpio = c.gpio.pwdn;
    info->power_gpio = c.gpio.power;
    info->switch_gpio = c.gpio.sw;
    info->switch_gpio_state = c.gpio.sw_state;
    info->sensor_id = c.sensor_id;
    info->video_interface = (IMPSensorVinType)c.video_interface;
    info->mclk = (IMPSensorMclk)c.mclk;
    info->default_boot = c.default_boot;
    hal_trace("--> [HAL] init: IMP_ISP_AddSensor vi=%d", vi);
    int rc = IMP_ISP_AddSensor((IMPVI_NUM)vi, info);
    hal_trace("<-- [HAL] init: IMP_ISP_AddSensor vi=%d rc=%d", vi, rc);
    if (rc < 0) return -1;
    return 0;
}
int SensorController::addAll(const std::vector<SensorConfig>& sensors) {
    int n = sensors.size();
    if (n >= 1) { if (addOneSensor(IMPVI_MAIN, sensors[0], &sensor_info[0]) < 0) return -1; }
    if (n >= 2) { if (addOneSensor(IMPVI_SEC, sensors[1], &sensor_info[1]) < 0) return -1; }
    if (n >= 3) { if (addOneSensor(IMPVI_THR, sensors[2], &sensor_info[2]) < 0) return -1; }
    if (n >= 4) { if (addOneSensor(IMPVI_FOUR, sensors[3], &sensor_info[3]) < 0) return -1; }
    return 0;
}
int SensorController::enableAll(const std::vector<SensorConfig>& sensors) {
    int num = sensors.size();
    {
        hal_trace("--> [HAL] init: IMP_ISP_EnableSensor vi=MAIN <<<kernel oops site: tisp_awb_init>>>");
        int rc = IMP_ISP_EnableSensor(IMPVI_MAIN, &sensor_info[0]);
        hal_trace("<-- [HAL] init: IMP_ISP_EnableSensor vi=MAIN rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_ONE) {
        hal_trace("--> [HAL] init: IMP_ISP_EnableSensor vi=SEC");
        int rc = IMP_ISP_EnableSensor(IMPVI_SEC, &sensor_info[1]);
        hal_trace("<-- [HAL] init: IMP_ISP_EnableSensor vi=SEC rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_TWO) {
        hal_trace("--> [HAL] init: IMP_ISP_EnableSensor vi=THR");
        int rc = IMP_ISP_EnableSensor(IMPVI_THR, &sensor_info[2]);
        hal_trace("<-- [HAL] init: IMP_ISP_EnableSensor vi=THR rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_THR) {
        hal_trace("--> [HAL] init: IMP_ISP_EnableSensor vi=FOUR");
        int rc = IMP_ISP_EnableSensor(IMPVI_FOUR, &sensor_info[3]);
        hal_trace("<-- [HAL] init: IMP_ISP_EnableSensor vi=FOUR rc=%d", rc);
        if (rc < 0) return -1;
    }
    return 0;
}
int SensorController::setAllFps(const std::vector<SensorConfig>& sensors) {
    /* Disabled: IMP_ISP_Tuning_SetSensorFPS triggers gc4653_set_fps bug
     * which miscalculates VTS=3000, dropping actual fps to ~17.
     * SDK default init already configures correct VTS=1680 for 30fps.
     */
    (void)sensors;
    return 0;
}
int SensorController::disableAll(const std::vector<SensorConfig>& sensors) {
    int num = sensors.size();
    {
        hal_trace("--> [HAL] exit: IMP_ISP_DisableSensor vi=MAIN");
        int rc = IMP_ISP_DisableSensor(IMPVI_MAIN);
        hal_trace("<-- [HAL] exit: IMP_ISP_DisableSensor vi=MAIN rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_ONE) {
        hal_trace("--> [HAL] exit: IMP_ISP_DisableSensor vi=SEC");
        int rc = IMP_ISP_DisableSensor(IMPVI_SEC);
        hal_trace("<-- [HAL] exit: IMP_ISP_DisableSensor vi=SEC rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_TWO) {
        hal_trace("--> [HAL] exit: IMP_ISP_DisableSensor vi=THR");
        int rc = IMP_ISP_DisableSensor(IMPVI_THR);
        hal_trace("<-- [HAL] exit: IMP_ISP_DisableSensor vi=THR rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (num > IMPISP_TOTAL_THR) {
        hal_trace("--> [HAL] exit: IMP_ISP_DisableSensor vi=FOUR");
        int rc = IMP_ISP_DisableSensor(IMPVI_FOUR);
        hal_trace("<-- [HAL] exit: IMP_ISP_DisableSensor vi=FOUR rc=%d", rc);
        if (rc < 0) return -1;
    }
    return 0;
}
int SensorController::delAll(const std::vector<SensorConfig>& sensors) {
    int n = sensors.size();
    {
        hal_trace("--> [HAL] exit: IMP_ISP_DelSensor vi=MAIN");
        int rc = IMP_ISP_DelSensor(IMPVI_MAIN, &sensor_info[0]);
        hal_trace("<-- [HAL] exit: IMP_ISP_DelSensor vi=MAIN rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (n > 1) {
        hal_trace("--> [HAL] exit: IMP_ISP_DelSensor vi=SEC");
        int rc = IMP_ISP_DelSensor(IMPVI_SEC, &sensor_info[1]);
        hal_trace("<-- [HAL] exit: IMP_ISP_DelSensor vi=SEC rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (n > 2) {
        hal_trace("--> [HAL] exit: IMP_ISP_DelSensor vi=THR");
        int rc = IMP_ISP_DelSensor(IMPVI_THR, &sensor_info[2]);
        hal_trace("<-- [HAL] exit: IMP_ISP_DelSensor vi=THR rc=%d", rc);
        if (rc < 0) return -1;
    }
    if (n > 3) {
        hal_trace("--> [HAL] exit: IMP_ISP_DelSensor vi=FOUR");
        int rc = IMP_ISP_DelSensor(IMPVI_FOUR, &sensor_info[3]);
        hal_trace("<-- [HAL] exit: IMP_ISP_DelSensor vi=FOUR rc=%d", rc);
        if (rc < 0) return -1;
    }
    return 0;
}

void FrameChannelController::init(const std::vector<ChannelConfig>& cfgs) {
    channels_ = cfgs;
}

int FrameChannelController::create(int index) {
    for (auto& c : channels_) {
        if (c.enable && (index < 0 || c.index == (unsigned int)index)) {
            if (IMP_FrameSource_CreateChn(c.index, &c.fs_attr) < 0) return -1;
        }
    }
    return 0;
}

int FrameChannelController::setAttr(int index) {
    for (auto& c : channels_) {
        if (c.enable && (index < 0 || c.index == (unsigned int)index)) {
            if (IMP_FrameSource_SetChnAttr(c.index, &c.fs_attr) < 0) return -1;
        }
    }
    return 0;
}

int FrameChannelController::enable(int index) {
    for (auto& c : channels_) {
        if (c.enable && (index < 0 || c.index == (unsigned int)index)) {
            if (IMP_FrameSource_EnableChn(c.index) < 0) return -1;
        }
    }
    return 0;
}

int FrameChannelController::disable(int index) {
    for (auto& c : channels_) {
        if (c.enable && (index < 0 || c.index == (unsigned int)index)) {
            hal_trace("--> [HAL] exit: IMP_FrameSource_DisableChn chn=%d", c.index);
            int rc = IMP_FrameSource_DisableChn(c.index);
            hal_trace("<-- [HAL] exit: IMP_FrameSource_DisableChn chn=%d rc=%d", c.index, rc);
            if (rc < 0) return -1;
        }
    }
    return 0;
}

int FrameChannelController::destroy(int index) {
    for (auto& c : channels_) {
        if (c.enable && (index < 0 || c.index == (unsigned int)index)) {
            hal_trace("--> [HAL] exit: IMP_FrameSource_DestroyChn chn=%d", c.index);
            int rc = IMP_FrameSource_DestroyChn(c.index);
            hal_trace("<-- [HAL] exit: IMP_FrameSource_DestroyChn chn=%d rc=%d", c.index, rc);
            if (rc < 0) return -1;
        }
    }
    return 0;
}

static IMPEncoderRcMode toImpRcMode(VideoRcMode m) {
    switch (m) {
        case VideoRcMode::FIXQP: return ENC_RC_MODE_FIXQP;
        case VideoRcMode::CBR: return ENC_RC_MODE_CBR;
        case VideoRcMode::VBR: return ENC_RC_MODE_VBR;
        case VideoRcMode::CVBR: return ENC_RC_MODE_CVBR;
        case VideoRcMode::AVBR: return ENC_RC_MODE_AVBR;
        case VideoRcMode::SMART: default: return ENC_RC_MODE_SMART;
    }
}

static IMPPayloadType toImpPayload(VideoPayloadType p) {
    switch (p) {
        case VideoPayloadType::H265: return PT_H265;
        case VideoPayloadType::JPEG: return PT_JPEG;
        case VideoPayloadType::H264: default: return PT_H264;
    }
}
static bool configureEncoderAttr(const VideoStreamConfig& cfg, IMPEncoderCHNAttr* chn_attr) {
    IMPFSI2DAttr i2d_attr;
    int picWidth = cfg.width;
    int picHeight = cfg.height;
    memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));
    memset(chn_attr, 0, sizeof(IMPEncoderCHNAttr));

    if(IMP_FrameSource_GetI2dAttr(cfg.channel.sensor_index * 3 + cfg.channel.stream_index, &i2d_attr) < 0){
        return false;
    }

    if((1 == i2d_attr.i2d_enable) &&
            ((i2d_attr.rotate_enable) && 
            (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
        /* this depend on your sensor or channels */
        picWidth  = cfg.height;
        picHeight = cfg.width;
    } else {
        picWidth  = cfg.width;
        picHeight = cfg.height;
    }

    chn_attr->encAttr.enType = toImpPayload(cfg.payload);
    
    if (cfg.payload == VideoPayloadType::JPEG) {
        if (picWidth <= 0) picWidth = 1920;
        if (picHeight <= 0) picHeight = 1080;
        chn_attr->encAttr.picWidth = picWidth;
        chn_attr->encAttr.picHeight = picHeight;
        chn_attr->encAttr.profile = 0;
        chn_attr->encAttr.bufSize = 0;
        chn_attr->rcAttr.attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
        chn_attr->rcAttr.attrRcMode.attrJPEGFixQp.qp = (cfg.quality > 0) ? cfg.quality : 40;
    } else {
        chn_attr->encAttr.profile = cfg.profile > 0 ? cfg.profile : 1;
        
        if ((picHeight > 1920) || (picHeight == 1920)) {
            chn_attr->encAttr.bufSize = picWidth * picHeight * 3 / 2;
        } else if ((picHeight > 1520) || (picHeight == 1520)) {
            chn_attr->encAttr.bufSize = picWidth * picHeight * 3 / 8;
        } else if ((picHeight > 1080) || (picHeight == 1080)) {
            chn_attr->encAttr.bufSize = picWidth * picHeight / 2;
        } else {
            chn_attr->encAttr.bufSize = picWidth * picHeight * 3 / 4;
        }
    
        chn_attr->encAttr.picWidth = picWidth;
        chn_attr->encAttr.picHeight = picHeight;
        chn_attr->rcAttr.attrRcMode.rcMode = toImpRcMode(cfg.rc_mode);
        int fpsNum = cfg.fps_num > 0 ? cfg.fps_num : 25;
        int fpsDen = cfg.fps_den > 0 ? cfg.fps_den : 1;
        chn_attr->rcAttr.outFrmRate.frmRateNum = fpsNum;
        chn_attr->rcAttr.outFrmRate.frmRateDen = fpsDen;
        chn_attr->rcAttr.maxGop = (cfg.gop > 0) ? cfg.gop : (2 * fpsNum / fpsDen);
        chn_attr->rcAttr.attrHSkip.hSkipAttr.skipType = IMP_Encoder_STYPE_N1X;
        chn_attr->rcAttr.attrHSkip.hSkipAttr.m = (cfg.skip_m > 0) ? cfg.skip_m : 3;
        chn_attr->rcAttr.attrHSkip.hSkipAttr.n = (cfg.skip_n > 0) ? cfg.skip_n : 4;
        chn_attr->rcAttr.attrHSkip.hSkipAttr.maxSameSceneCnt = 0;
        chn_attr->rcAttr.attrHSkip.hSkipAttr.bEnableScenecut = 0;
        chn_attr->rcAttr.attrHSkip.hSkipAttr.bBlackEnhance = 0;
        chn_attr->rcAttr.attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N1X;

        chn_attr->bEnableIvdc = cfg.enable_ivdc;
        int defaultBitrateKbps = 0;
        if (cfg.payload != VideoPayloadType::JPEG) {
            if (picHeight >= 1080) defaultBitrateKbps = 4096;
            else if (picHeight >= 720) defaultBitrateKbps = 2048;
            else defaultBitrateKbps = 1024;
        }
    
        bool isH264 = cfg.payload == VideoPayloadType::H264;
        switch (cfg.rc_mode) {
            case VideoRcMode::FIXQP: {
                int q = cfg.quality > 0 ? cfg.quality : 35;
                if (isH264) {
                    chn_attr->rcAttr.attrRcMode.attrH264FixQp.IQp = q;
                    chn_attr->rcAttr.attrRcMode.attrH264FixQp.PQp = q;
                    chn_attr->rcAttr.attrRcMode.attrH264FixQp.blkQpEn = 0;
                } else {
                    chn_attr->rcAttr.attrRcMode.attrH265FixQp.IQp = q;
                    chn_attr->rcAttr.attrRcMode.attrH265FixQp.PQp = q;
                    chn_attr->rcAttr.attrRcMode.attrH265FixQp.blkQpEn = 0;
                }
                break;
            }
            case VideoRcMode::CBR: {
                int br = (cfg.bitrate > 0) ? cfg.bitrate : defaultBitrateKbps;
                if (isH264) {
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.outBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.IPfrmQPDelta = 8;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.PPfrmQPDelta = 8;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.flucLvl = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.qualityLvl = 3;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Cbr.outBitRate * 6 / 5;
                    chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Cbr.maxIPictureSize * 3 / 5;
                } else {
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.outBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.IPfrmQPDelta = 8;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.PPfrmQPDelta = 8;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.flucLvl = 2;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.qualityLvl = 3;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH265Cbr.outBitRate * 6 / 5;
                    chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH265Cbr.maxIPictureSize * 3 / 5;
                }
                break;
            }
            case VideoRcMode::VBR: {
                int br = (cfg.bitrate > 0) ? cfg.bitrate : defaultBitrateKbps;
                if (isH264) {
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Vbr.maxIPictureSize * 3 / 5;
                } else {
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH265Vbr.maxIPictureSize * 3 / 5;
                }
                break;
            }
            case VideoRcMode::SMART: {
                int br = (cfg.bitrate > 0) ? cfg.bitrate : defaultBitrateKbps;
                if (isH264) {
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.minStillRate = 25;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxStillQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.superSmartEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.supSmtStillLvl = 5;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.supSmtStillRateLvl = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxSupSmtStillRate = 20;
                    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Smart.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH264Smart.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Smart.maxIPictureSize * 3 / 5;
                } else {
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.staticTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.minStillRate = 25;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxStillQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH264Smart.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH265Smart.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH265Smart.maxIPictureSize * 3 / 5;
                }
                break;
            }
            case VideoRcMode::CVBR: {
                int br = (cfg.bitrate > 0) ? cfg.bitrate : defaultBitrateKbps;
                if (isH264) {
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.longMaxBitRate = chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxBitRate * 4 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH264CVbr.longMinBitRate = chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxBitRate * 3 / 5;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.shortStatTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.longStatTime = 60;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.extraBitRate = 5;
                    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH264CVbr.maxIPictureSize * 3 / 5;
                } else {
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.minQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.blkQpEn = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.initialQp = 35;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxIQp = 45;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.minIQp = 15;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxBitRate = br;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.longMaxBitRate = chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxBitRate * 4 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH265CVbr.longMinBitRate = chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxBitRate * 3 / 5; 
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.iBiasLvl = 0;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.changePos = 80;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.shortStatTime = 2;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.longStatTime = 60;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.qualityLvl = 6;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxIprop = 100;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.minIprop = 1;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.extraBitRate = 5;
                    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxIPictureSize = chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxBitRate * 6 / 5;
				    chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxPPictureSize = chn_attr->rcAttr.attrRcMode.attrH265CVbr.maxIPictureSize * 3 / 5;
                }
                break;
            }
            default:
                break;
        }
    }

    return true;
}

// ---- Slice 1a 步骤 3a：encoder channel + FrameSource 常驻 ----
// init 一次性 Create+Register+Bind 常驻 channel + Enable FrameSource；运行时 start/stop
// 只 Start/StopRecvPic，不再 Enable/Disable FrameSource。根因：record stop 把 group0
// DisableChn → photo start EnableChn 重启 ISP→FS 管线 → 3s 延迟（且随轮次恶化）。
// group0 统一 sensor-native 2560×1440（record H264 + photo JPEG 共享；photo 改 sensor-native
// 符合 snap 请求尺寸，顺带修复 snap 不 reconfigure 导致请求尺寸不生效）。
struct ResidentChannelDef {
    int group;
    int channel;
    VideoStreamConfig cfg;
};

static std::vector<ResidentChannelDef> buildResidentChannels() {
    auto mk = [](int group, int channel, VideoPayloadType payload, int streamIdx,
                 int w, int h, int fpsNum, int quality, int bitrate, int gop,
                 VideoRcMode rc, bool ivdc) {
        ResidentChannelDef d;
        memset(&d.cfg, 0, sizeof(VideoStreamConfig));
        d.group = group;
        d.channel = channel;
        d.cfg.payload = payload;
        d.cfg.channel.sensor_index = 0;
        d.cfg.channel.stream_index = streamIdx;
        d.cfg.width = w;
        d.cfg.height = h;
        d.cfg.fps_num = fpsNum;
        d.cfg.fps_den = 1;
        d.cfg.quality = quality;
        d.cfg.bitrate = bitrate;
        d.cfg.gop = gop;
        d.cfg.rc_mode = rc;
        d.cfg.enable_ivdc = ivdc;
        return d;
    };
    std::vector<ResidentChannelDef> v;
    // group0(CH0 2560×1440): record H264 enc0 + photo JPEG enc12
    v.push_back(mk(0, 0,  VideoPayloadType::H264, 0, 2560, 1440, 30, 0, 4096, 60, VideoRcMode::CBR,   true));
    v.push_back(mk(0, 12, VideoPayloadType::JPEG, 0, 2560, 1440, 15, 40, 0,    0,  VideoRcMode::FIXQP, true));
    // group1(CH1 1280×720): preview(RTSP) H264 enc1
    v.push_back(mk(1, 1,  VideoPayloadType::H264, 1, 1280, 720,  30, 0, 2048, 60, VideoRcMode::CBR,   true));
    // group2(CH2 320×180): thumbnail JPEG enc14
    v.push_back(mk(2, 14, VideoPayloadType::JPEG, 2, 320,  180,  15, 80, 0,    0,  VideoRcMode::FIXQP, true));
    return v;
}

IngenicVideoStream::IngenicVideoStream()
    : configured_(false),
      started_(false),
      ref_count_(0),
      group_id_(0),
      channel_id_(0),
      last_stream_valid_(false) {
    fs_cell_.groupID = 0;
    fs_cell_.outputID = 0;
    enc_cell_.groupID = 0;
    enc_cell_.outputID = 0;
}
IngenicVideoStream::~IngenicVideoStream() {
    if (configured_) {
        // Flush in-flight encoder frames BEFORE teardown so IMP_System_Exit() is
        // never called while the encoder still holds an unreleased stream. Order
        // follows imp_system.h: StopRecv -> drain/release cached stream -> UnBind
        // (needs FS disabled first; releaseFrameSource does DisableChn) ->
        // DestroyChn -> DestroyGroup. UnRegister/Destroy are unconditional: IMP
        // tolerates calling them on an already-freed channel (returns <0, harmless).
        // The earlier IMP_Encoder_Query(st.registered) gate is unreliable after
        // teardown and was removed (see artifacts/T3-analyst-evidence.md 缺陷 B).
        if (last_stream_valid_) {
            hal_trace("--> ~dtor IMP_Encoder_ReleaseStream chn=%d", channel_id_);
            int rs_rc = IMP_Encoder_ReleaseStream(channel_id_, &last_stream_);
            hal_trace("<-- ~dtor IMP_Encoder_ReleaseStream chn=%d rc=%d", channel_id_, rs_rc);
            last_stream_valid_ = false;
        }
        hal_trace("--> ~dtor IMP_Encoder_StopRecvPic chn=%d", channel_id_);
        int sr_rc = IMP_Encoder_StopRecvPic(channel_id_);
        hal_trace("<-- ~dtor IMP_Encoder_StopRecvPic chn=%d rc=%d", channel_id_, sr_rc);
        releaseFrameSource(group_id_);
        releaseBind(group_id_, &fs_cell_, &enc_cell_);
        releaseChn(channel_id_);
        releaseGroup(group_id_);
        configured_ = false;
    }
}

bool IngenicVideoStream::configure(const VideoStreamConfig& cfg) {
    cfg_ = cfg;
    group_id_ = cfg.channel.sensor_index * 3 + cfg.channel.stream_index;
    // JPEG channels: 12 + sensor_index*3 + stream_index (avoids CH0/CH2 conflict)
    // CH0 -> 12, CH1 -> 13, CH2 -> 14, sensor1 CH0 -> 15, ...
    channel_id_ = (cfg.payload == VideoPayloadType::JPEG)
        ? (12 + cfg.channel.sensor_index * 3 + cfg.channel.stream_index)
        : group_id_;
    Logger::log(LogLevel::DEBUG, "[HAL] configure: payload=%d sensor=%d stream=%d group_id=%d channel_id=%d size=%dx%d fps=%d/%d quality=%d rc=%d skip=%d/%d ivdc=%d",
            (int)cfg.payload, cfg.channel.sensor_index, cfg.channel.stream_index, group_id_, channel_id_,
            cfg.width, cfg.height, cfg.fps_num, cfg.fps_den, cfg.quality, (int)cfg.rc_mode, cfg.skip_m, cfg.skip_n, cfg.enable_ivdc ? 1 : 0);
    
    // FS attr 由 init preBindAllChannels 稳态收敛（EnableChn 前定型）；configure 不再
    // SetChnAttr —— EnableChn 后改 FS attr 违反 imp_system.h 约束，且 group0 record/photo
    // 共享需统一 sensor-native 2560×1440。

    if (!acquireGroup(group_id_)) {
        Logger::log(LogLevel::ERROR, "[HAL] configure: acquireGroup(%d) failed", group_id_);
        return false;
    }

    IMPEncoderCHNAttr chn_attr;
    if (!configureEncoderAttr(cfg, &chn_attr)) {
        Logger::log(LogLevel::ERROR, "[HAL] configureEncoderAttr failed");
        releaseGroup(group_id_);
        return false;
    }

    if (!acquireChn(group_id_, channel_id_, &chn_attr)) {
        Logger::log(LogLevel::ERROR, "[HAL] configure: acquireChn(g=%d,chn=%d) failed", group_id_, channel_id_);
        releaseGroup(group_id_);
        return false;
    }
    fs_cell_.deviceID = DEV_ID_FS;
    fs_cell_.groupID = group_id_;
    fs_cell_.outputID = 0;
    enc_cell_.deviceID = DEV_ID_ENC;
    enc_cell_.groupID = group_id_;
    enc_cell_.outputID = 0;
    if (!acquireBind(group_id_, &fs_cell_, &enc_cell_)) {
        Logger::log(LogLevel::ERROR, "[HAL] configure: acquireBind(fs=%d,enc=%d) failed", fs_cell_.groupID, enc_cell_.groupID);
        releaseChn(channel_id_);
        releaseGroup(group_id_);
        return false;
    }
    if (group_id_ == 0 && IspOsdManager::getInstance()) {
        IspOsdManager::getInstance()->prepare();
    }
    configured_ = true;
    started_ = false;
    Logger::log(LogLevel::DEBUG, "[HAL] configure: success (group_id=%d, channel_id=%d)", group_id_, channel_id_);
    return true;
}
bool IngenicVideoStream::start() {
    if (!configured_) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    if (ref_count_ == 0) {
        Logger::log(LogLevel::DEBUG, "[HAL] start: group_id=%d channel_id=%d ref_count=%d", group_id_, channel_id_, ref_count_);
        bool first_enable = false;
        if (!acquireFrameSource(group_id_, &first_enable)) {
            Logger::log(LogLevel::ERROR, "[HAL] start: acquireFrameSource(%d) failed", group_id_);
            return false;
        }
        hal_trace("--> start IMP_Encoder_StartRecvPic chn=%d", channel_id_);
        int st_rc = IMP_Encoder_StartRecvPic(channel_id_);
        hal_trace("<-- start IMP_Encoder_StartRecvPic chn=%d rc=%d", channel_id_, st_rc);
        if (st_rc < 0) {
            Logger::log(LogLevel::ERROR, "[HAL] start: IMP_Encoder_StartRecvPic(%d) failed", channel_id_);
            releaseFrameSource(group_id_);
            return false;
        }
        if (group_id_ == 0 && first_enable && IspOsdManager::getInstance()) {
            IspOsdManager::getInstance()->start();
        }
        started_ = true;
        Logger::log(LogLevel::DEBUG, "[HAL] start: success (group_id=%d, channel_id=%d)", group_id_, channel_id_);
    }
    ref_count_++;
    return true;
}
bool IngenicVideoStream::stop() {
    if (!configured_) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    if (ref_count_ <= 0) {
        Logger::log(LogLevel::WARNING, "[HAL] stop: ref_count=%d already stopped", ref_count_);
        return true;
    }
    ref_count_--;
    if (ref_count_ == 0 && started_) {
        Logger::log(LogLevel::DEBUG, "[HAL] stop: stopping recv pic channel_id=%d", channel_id_);
        hal_trace("--> stop IMP_Encoder_StopRecvPic chn=%d", channel_id_);
        int sp_rc = IMP_Encoder_StopRecvPic(channel_id_);
        hal_trace("<-- stop IMP_Encoder_StopRecvPic chn=%d rc=%d", channel_id_, sp_rc);
        bool last_disable = false;
        releaseFrameSource(group_id_, &last_disable);
        if (group_id_ == 0 && last_disable && IspOsdManager::getInstance()) {
            IspOsdManager::getInstance()->stop();
        }
        started_ = false;
        Logger::log(LogLevel::DEBUG, "[HAL] stop: success (resources kept configured)");
    }
    return true;
}

bool IngenicVideoStream::polling(int timeout_ms) {
    if (!started_) {
        Logger::log(LogLevel::WARNING, "[HAL] polling: not started channel_id=%d", channel_id_);
        return false;
    }
    bool lg = hal_trace_loop_gate();
    if (lg) hal_trace("--> polling IMP_Encoder_PollingStream chn=%d to=%d", channel_id_, timeout_ms);
    int ps_rc = IMP_Encoder_PollingStream(channel_id_, timeout_ms);
    if (lg) hal_trace("<-- polling IMP_Encoder_PollingStream chn=%d rc=%d", channel_id_, ps_rc);
    return ps_rc >= 0;
}
bool IngenicVideoStream::getFrame(VideoEncodedFrame& out) {
    if (!started_) { 
        Logger::log(LogLevel::WARNING, "[HAL] getFrame: not started channel_id=%d", channel_id_);
        return false; 
    }
    IMPEncoderStream stream;
    bool lg = hal_trace_loop_gate();
    if (lg) hal_trace("--> getFrame IMP_Encoder_GetStream chn=%d", channel_id_);
    int gs_rc = IMP_Encoder_GetStream(channel_id_, &stream, 1);
    if (lg) hal_trace("<-- getFrame IMP_Encoder_GetStream chn=%d rc=%d pack=%d", channel_id_, gs_rc, (int)stream.packCount);
    if (gs_rc < 0) {
        Logger::log(LogLevel::ERROR, "[HAL] getFrame: IMP_Encoder_GetStream(%d) failed", channel_id_);
        return false;
    }
    if (stream.packCount <= 0) {
        Logger::log(LogLevel::WARNING, "[HAL] getFrame: packCount<=0 channel_id=%d", channel_id_);
        hal_trace("--> getFrame(empty) IMP_Encoder_ReleaseStream chn=%d", channel_id_);
        int rse_rc = IMP_Encoder_ReleaseStream(channel_id_, &stream);
        hal_trace("<-- getFrame(empty) IMP_Encoder_ReleaseStream chn=%d rc=%d", channel_id_, rse_rc);
        return false;
    }
    last_pieces_.resize(stream.packCount);
    for (int i = 0; i < (int)stream.packCount; ++i) {
        last_pieces_[i].data = (void*)stream.pack[i].virAddr;
        last_pieces_[i].size = (size_t)stream.pack[i].length;
    }
    out.pieces = last_pieces_.data();
    out.piece_count = (int)stream.packCount;
    out.pts = stream.pack[(int)stream.packCount - 1].timestamp;
    out.key = (cfg_.payload == VideoPayloadType::JPEG) ? true : (stream.refType == IMP_Encoder_FSTYPE_IDR);
    last_stream_ = stream;
    last_stream_valid_ = true;
    g_hal_loop_frame.fetch_add(1);   // one captured video frame → throttle counter
    return true;
}
void IngenicVideoStream::releaseFrame(VideoEncodedFrame& out) {
    if (last_stream_valid_) {
        bool lg = hal_trace_loop_gate();
        if (lg) hal_trace("--> releaseFrame IMP_Encoder_ReleaseStream chn=%d", channel_id_);
        int rf_rc = IMP_Encoder_ReleaseStream(channel_id_, &last_stream_);
        if (lg) hal_trace("<-- releaseFrame IMP_Encoder_ReleaseStream chn=%d rc=%d", channel_id_, rf_rc);
        last_stream_valid_ = false;
    }
    out.pieces = nullptr;
    out.piece_count = 0;
}
bool IngenicVideoStream::getInfo(VideoStreamInfo& info) {
    std::lock_guard<std::mutex> lock(mtx_);
    info.index = group_id_;
    IMPEncoderCHNStat st;
    memset(&st, 0, sizeof(st));
    hal_trace("--> getInfo IMP_Encoder_Query chn=%d", channel_id_);
    int q_rc = IMP_Encoder_Query(channel_id_, &st);
    hal_trace("<-- getInfo IMP_Encoder_Query chn=%d rc=%d registered=%d", channel_id_, q_rc, st.registered);
    if (q_rc >= 0) {
        info.enabled = st.registered ? 1 : 0;
    } else {
        info.enabled = started_ ? 1 : 0;
    }
    info.sensor_index = cfg_.channel.sensor_index;
    info.output_index = cfg_.channel.stream_index;
    IMPFSChnAttr fs_attr;
    memset(&fs_attr, 0, sizeof(fs_attr));
    hal_trace("--> getInfo IMP_FrameSource_GetChnAttr g=%d", group_id_);
    int gai_rc = IMP_FrameSource_GetChnAttr(group_id_, &fs_attr);
    hal_trace("<-- getInfo IMP_FrameSource_GetChnAttr g=%d rc=%d %dx%d", group_id_, gai_rc, fs_attr.picWidth, fs_attr.picHeight);
    if (gai_rc >= 0) {
        info.width = fs_attr.picWidth;
        info.height = fs_attr.picHeight;
        info.fps_num = fs_attr.outFrmRateNum;
        info.fps_den = fs_attr.outFrmRateDen;
    } else {
        info.width = cfg_.width;
        info.height = cfg_.height;
        info.fps_num = cfg_.fps_num;
        info.fps_den = cfg_.fps_den;
    }
    info.payload = cfg_.payload;
    IMPISPAeOnlyReadAttr ae_attr;
    memset(&ae_attr, 0, sizeof(ae_attr));
    IMPVI_NUM vi = (IMPVI_NUM)(info.sensor_index);
    if (IMP_ISP_Tuning_GetAeOnlyReadAttr(vi, &ae_attr) == 0) {
        info.ae_converged = ae_attr.stable ? true : false;
    } else {
        info.ae_converged = false;
    }
    return true;
}
bool IngenicVideoStream::requestIDR() {
    std::lock_guard<std::mutex> lock(mtx_);
    return IMP_Encoder_RequestIDR(channel_id_) == 0;
}
IngenicVideo::IngenicVideo() : direct_switch_(0), gosd_enable_(2), exitCalled_(false) {

}
IngenicVideo::~IngenicVideo() {
    if (!exitCalled_) {
        exit();
    }
}

// Slice 1a 步骤 3a：在 fsMgr.setAttr + ispOsdMgr_ init 之后、任何 consumer configure/start
// 之前调用（g_video_init_ref_count 短路保证只在首个完整 init 跑）。对每个 group 严格按
// imp_system.h 时序：FS attr 稳态收敛(SetChnAttr) → CreateGroup/CreateChn/RegisterChn/Bind →
// EnableChn → group0 OSD。EnableChn 后运行期不再 SetChnAttr（违反约束）；基础 ref=1 保证
// 运行时 stop 不 DisableChn（消除 FrameSource Enable/Disable 往返 = 录影→拍照超时根因）。
bool IngenicVideo::preBindAllChannels() {
    auto defs = buildResidentChannels();
    // 按 group 聚合（同 group 多 channel 共享 FS attr + Bind）
    std::map<int, std::vector<ResidentChannelDef>> byGroup;
    for (auto& d : defs) byGroup[d.group].push_back(std::move(d));

    for (auto& kv : byGroup) {
        int g = kv.first;
        auto& chs = kv.second;
        const VideoStreamConfig& cfg0 = chs[0].cfg;

        // 1. FS attr 稳态收敛（替代 consumer configure 的 SetChnAttr，EnableChn 前定型）
        IMPFSChnAttr fs_attr;
        hal_trace("--> preBind IMP_FrameSource_GetChnAttr g=%d", g);
        int ga_rc = IMP_FrameSource_GetChnAttr(g, &fs_attr);
        hal_trace("<-- preBind IMP_FrameSource_GetChnAttr g=%d rc=%d", g, ga_rc);
        if (ga_rc != 0) {
            Logger::log(LogLevel::ERROR, "[HAL] preBind: GetChnAttr(%d) failed", g);
            return false;
        }
        int sensorW = fs_attr.picWidth;
        int sensorH = fs_attr.picHeight;
        fs_attr.scaler.enable = 1;
        fs_attr.scaler.outwidth = cfg0.width;
        fs_attr.scaler.outheight = cfg0.height;
        fs_attr.picWidth = cfg0.width;
        fs_attr.picHeight = cfg0.height;
        fs_attr.crop.enable = 1;
        fs_attr.crop.top = 0;
        fs_attr.crop.left = 0;
        fs_attr.crop.width = sensorW;
        fs_attr.crop.height = sensorH;
        fs_attr.outFrmRateNum = cfg0.fps_num;
        fs_attr.outFrmRateDen = cfg0.fps_den;
        hal_trace("--> preBind IMP_FrameSource_SetChnAttr g=%d %dx%d crop=%dx%d",
                  g, cfg0.width, cfg0.height, sensorW, sensorH);
        int sa_rc = IMP_FrameSource_SetChnAttr(g, &fs_attr);
        hal_trace("<-- preBind IMP_FrameSource_SetChnAttr g=%d rc=%d", g, sa_rc);
        if (sa_rc != 0) {
            Logger::log(LogLevel::ERROR, "[HAL] preBind: SetChnAttr(%d) failed", g);
            return false;
        }

        // 2. acquireGroup（CreateGroup 幂等）
        if (!acquireGroup(g)) {
            Logger::log(LogLevel::ERROR, "[HAL] preBind: acquireGroup(%d) failed", g);
            return false;
        }

        // 3. 对每个常驻 channel: configureEncoderAttr + acquireChn（CreateChn+RegisterChn 幂等）
        for (auto& d : chs) {
            IMPEncoderCHNAttr chn_attr;
            if (!configureEncoderAttr(d.cfg, &chn_attr)) {
                Logger::log(LogLevel::ERROR, "[HAL] preBind: configureEncoderAttr(g=%d,chn=%d) failed", g, d.channel);
                releaseGroup(g);
                return false;
            }
            if (!acquireChn(g, d.channel, &chn_attr)) {
                Logger::log(LogLevel::ERROR, "[HAL] preBind: acquireChn(g=%d,chn=%d) failed", g, d.channel);
                releaseGroup(g);
                return false;
            }
        }

        // 4. acquireBind（group 级，同 group 多 channel 只 bind 一次）
        IMPCell fs_cell, enc_cell;
        memset(&fs_cell, 0, sizeof(fs_cell));
        memset(&enc_cell, 0, sizeof(enc_cell));
        fs_cell.deviceID = DEV_ID_FS;
        fs_cell.groupID = g;
        fs_cell.outputID = 0;
        enc_cell.deviceID = DEV_ID_ENC;
        enc_cell.groupID = g;
        enc_cell.outputID = 0;
        if (!acquireBind(g, &fs_cell, &enc_cell)) {
            Logger::log(LogLevel::ERROR, "[HAL] preBind: acquireBind(%d) failed", g);
            releaseGroup(g);
            return false;
        }

        // 5. acquireFrameSource（EnableChn，基础 ref=1 → 运行时 stop 不 Disable）
        bool first_enable = false;
        if (!acquireFrameSource(g, &first_enable)) {
            Logger::log(LogLevel::ERROR, "[HAL] preBind: acquireFrameSource(%d) failed", g);
            releaseBind(g, &fs_cell, &enc_cell);
            releaseGroup(g);
            return false;
        }

        // 6. group0 OSD prepare + start（首次 enable；水印常驻，exit 时 stop）
        if (g == 0 && first_enable && ispOsdMgr_) {
            ispOsdMgr_->prepare();
            ispOsdMgr_->start();
        }
        Logger::log(LogLevel::INFO, "[HAL] preBind: group=%d bound (channels=%zu, fs enabled)", g, chs.size());
    }
    return true;
}

bool IngenicVideo::init() {
    if (g_video_init_ref_count.fetch_add(1) > 0) {
        Logger::log(LogLevel::INFO, "IngenicVideo already initialized, ref=%d", g_video_init_ref_count.load());
        return true;
    }
    hal_trace("==== [HAL] init() ENTERED (first ref, full IMP init) ====");
    OSDController osd;
    if (osd.setPoolSize(gosd_enable_) < 0) return false;
    hal_trace("--> [HAL] init: IMP_Encoder_SetJpegBsSize");
    int jbs_rc = IMP_Encoder_SetJpegBsSize(500 * 1024);
    hal_trace("<-- [HAL] init: IMP_Encoder_SetJpegBsSize rc=%d", jbs_rc);
    if (jbs_rc < 0) {
        return false;
    }
    hal_trace("--> [HAL] init: IMP_Encoder_SetMultiSectionMode");
    int msm_rc = IMP_Encoder_SetMultiSectionMode(1, 250, 2);
    hal_trace("<-- [HAL] init: IMP_Encoder_SetMultiSectionMode rc=%d", msm_rc);
    if (msm_rc < 0) {
        return false;
    }
    if (getenv("HTC_NO_MULTIPROCESS")) {
        hal_trace("--> [HAL] init: IMP_Encoder_MultiProcessInit SKIPPED (HTC_NO_MULTIPROCESS) <<<bisect>>>");
    } else {
        hal_trace("--> [HAL] init: IMP_Encoder_MultiProcessInit");
        int mpi_rc = IMP_Encoder_MultiProcessInit();
        hal_trace("<-- [HAL] init: IMP_Encoder_MultiProcessInit rc=%d", mpi_rc);
        if (mpi_rc < 0) {
            return false;
        }
    }
    auto sensors = loadSensors();
    hal_trace("--> [HAL] init: sensorMgr.openISP");
    if (sensorMgr.openISP() < 0) { hal_trace("<-- [HAL] init: openISP FAILED"); return false; }
    hal_trace("--> [HAL] init: sensorMgr.addAll (IMP_ISP_AddSensor per sensor)");
    if (sensorMgr.addAll(sensors) < 0) { hal_trace("<-- [HAL] init: addAll FAILED"); return false; }
    hal_trace("--> [HAL] init: sensorMgr.setCameraInputMode");
    if (sensorMgr.setCameraInputMode(sensors) < 0) { hal_trace("<-- [HAL] init: setCameraInputMode FAILED"); return false; }
    hal_trace("--> [HAL] init: sensorMgr.enableAll (IMP_ISP_EnableSensor) <<< tisp_awb_init oops site >>>");
    if (sensorMgr.enableAll(sensors) < 0) { hal_trace("<-- [HAL] init: enableAll FAILED"); return false; }
    hal_trace("--> [HAL] init: IMP_System_Init");
    int si_rc = IMP_System_Init();
    hal_trace("<-- [HAL] init: IMP_System_Init rc=%d", si_rc);
    if (si_rc < 0) return false;
    hal_trace("--> [HAL] init: IMP_ISP_EnableTuning");
    int et_rc = IMP_ISP_EnableTuning();
    hal_trace("<-- [HAL] init: IMP_ISP_EnableTuning rc=%d", et_rc);
    if (et_rc < 0) return false;
    unsigned char v = 128;
    IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &v);
    IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &v);
    IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &v);
    IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &v);
    IMPISPRunningMode rmode = IMPISP_RUNNING_MODE_DAY;
    if (IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &rmode) < 0) return false;
    /* Skip SetSensorFPS: SDK default init already sets correct VTS=1680 for 30fps.
     * Calling it triggers gc4653_set_fps bug which overwrites VTS=3000, dropping fps to ~17.
     * See doc/knowledge/bugs/T32-recording-fps-17-investigation.md
     */
    // if (sensorMgr.setAllFps(sensors) < 0) return false;
    fsMgr.init(buildChannels(sensors));
    if (fsMgr.create() < 0) return false;
    if (fsMgr.setAttr() < 0) return false;
    ispOsdMgr_.reset(new IspOsdManager());
    if (!ispOsdMgr_->init(SENSOR_NUM)) {
        Logger::log(LogLevel::WARNING, "IngenicVideo: IspOsdManager init failed");
    }
    /* Workaround: libimp.so internally overwrites VTS to 3000 during EnableSensor/EnableTuning,
     * dropping actual fps to ~17. Force VTS back to 1680 for correct 30fps.
     * See doc/knowledge/bugs/T32-recording-fps-17-investigation.md
     */
    {
        IMPISPSensorRegister r = {0x0340, 0x06};
        IMP_ISP_SetSensorRegister(IMPVI_MAIN, &r);
        r.addr = 0x0341; r.value = 0x90;
        IMP_ISP_SetSensorRegister(IMPVI_MAIN, &r);

        r.addr = 0x0340; r.value = 0;
        IMP_ISP_GetSensorRegister(IMPVI_MAIN, &r);
        uint32_t vts_high = r.value;
        r.addr = 0x0341; r.value = 0;
        IMP_ISP_GetSensorRegister(IMPVI_MAIN, &r);
        uint32_t vts_low = r.value;
        uint32_t vts = (vts_high << 8) | vts_low;
        Logger::log(LogLevel::INFO, "IngenicVideo VTS corrected to 0x%04x (%d)", vts, vts);
    }

    // Slice 1a 步骤 3a：encoder channel + FrameSource 一次性常驻（在任何 consumer
    // configure/start 前）。失败则 init 失败。
    if (!preBindAllChannels()) {
        Logger::log(LogLevel::ERROR, "IngenicVideo: preBindAllChannels failed");
        return false;
    }
    return true;
}
bool IngenicVideo::exit() {
    hal_trace("---- [HAL] exit() INVOKED (exitCalled_=%d ref=%d) ----",
              exitCalled_ ? 1 : 0, g_video_init_ref_count.load());
    if (exitCalled_) return true;
    exitCalled_ = true;
    if (g_video_init_ref_count.fetch_sub(1) > 1) {
        Logger::log(LogLevel::INFO, "IngenicVideo still in use, ref=%d", g_video_init_ref_count.load());
        return true;
    }
    // SDK teardown ordering (sdk/include/imp/imp_system.h:130-131):
    //   L130: UnBind must happen AFTER FrameSource is Disabled.
    //   L131: DestroyGroup must happen AFTER UnBind.
    // exit() is the last reliable teardown point (the IngenicVideo destructor
    // is skipped when main() ends via Misc::poweroff()/while(1)). The per-stream
    // destructor (~IngenicVideoStream) normally already releases its own
    // group/bind/channel AND erases from the ref-count maps; exit() here only
    // sweeps residuals left in the file-static maps, so empty maps => no-op and
    // there is no double-destroy vs the stream destructor. Order enforced:
    //   1. Disable FrameSource (all channels)  -> allows UnBind
    //   2. Flush/stop encoder in-flight frames  -> clean encoder state
    //   3. UnBind (g_bind_ref_count)            -> allows DestroyGroup
    //   4. UnRegisterChn/DestroyChn/DestroyGroup (g_group_ref_count)
    //   5. ISP/OSD teardown
    //   6. fsMgr.destroy() (DestroyChn FrameSource) -> IMP_System_Exit -> sensor/ISP
    hal_trace("==== [HAL] exit() ENTERED (ref dropped to 0, teardown begins) ====");
    Logger::log(LogLevel::INFO, "[HAL] exit: teardown begin (FS disable -> flush -> UnBind -> DestroyGroup -> ISP/System)");
    // 1. Disable FrameSource so UnBind is legal (imp_system.h:130).
    hal_trace("--> [HAL] exit: fsMgr.disable() (IMP_FrameSource_DisableChn per chn)");
    fsMgr.disable();
    hal_trace("<-- [HAL] exit: fsMgr.disable()");
    // 2. Stop encoder recv / flush in-flight frames for any residual channel so
    //    IMP_System_Exit() is never called while the encoder holds a stream.
    {
        std::vector<int> chns;
        {
            std::lock_guard<std::mutex> lock(g_chn_mutex);
            for (const auto& kv : g_chn_ref_count) {
                chns.push_back(kv.first);
            }
        }
        for (int chn : chns) {
            Logger::log(LogLevel::INFO, "[HAL] exit: flush/StopRecvPic(chn=%d)", chn);
            hal_trace("--> [HAL] exit: IMP_Encoder_StopRecvPic chn=%d", chn);
            int sr_rc = IMP_Encoder_StopRecvPic(chn);
            hal_trace("<-- [HAL] exit: IMP_Encoder_StopRecvPic chn=%d rc=%d", chn, sr_rc);
        }
    }
    // 3. UnBind (after FS disabled). IMP_System_UnBind on an unbound pair is
    //    tolerated (returns <0, harmless). No Query gate — see stream destructor.
    {
        std::vector<int> bind_groups;
        {
            std::lock_guard<std::mutex> lock(g_bind_mutex);
            for (const auto& kv : g_bind_ref_count) {
                bind_groups.push_back(kv.first);
            }
        }
        for (int grp : bind_groups) {
            IMPCell fs_cell;
            IMPCell enc_cell;
            memset(&fs_cell, 0, sizeof(fs_cell));
            memset(&enc_cell, 0, sizeof(enc_cell));
            fs_cell.deviceID = DEV_ID_FS;
            fs_cell.groupID = grp;
            fs_cell.outputID = 0;
            enc_cell.deviceID = DEV_ID_ENC;
            enc_cell.groupID = grp;
            enc_cell.outputID = 0;
            Logger::log(LogLevel::INFO, "[HAL] exit: fallback UnBind(group=%d)", grp);
            hal_trace("--> [HAL] exit: IMP_System_UnBind g=%d", grp);
            int ub_rc = IMP_System_UnBind(&fs_cell, &enc_cell);
            hal_trace("<-- [HAL] exit: IMP_System_UnBind g=%d rc=%d", grp, ub_rc);
        }
        {
            std::lock_guard<std::mutex> lock(g_bind_mutex);
            g_bind_ref_count.clear();
        }
    }
    // 4. DestroyGroup/Chn (after UnBind, imp_system.h:131). Unconditional
    //    UnRegisterChn — IMP_Encoder_Query(st.registered) is unreliable after
    //    teardown; calling UnRegister/Destroy on an already-freed channel is
    //    harmless (returns <0).
    {
        // 4a. UnRegister + Destroy 所有 encoder channel（含 JPEG enc12/14，不只 chn=grp）
        std::vector<int> chns;
        {
            std::lock_guard<std::mutex> lock(g_chn_mutex);
            for (const auto& kv : g_chn_ref_count) {
                chns.push_back(kv.first);
            }
        }
        for (int chn : chns) {
            Logger::log(LogLevel::INFO, "[HAL] exit: fallback destroy chn=%d", chn);
            hal_trace("--> [HAL] exit: IMP_Encoder_UnRegisterChn chn=%d", chn);
            int ur_rc = IMP_Encoder_UnRegisterChn(chn);
            hal_trace("<-- [HAL] exit: IMP_Encoder_UnRegisterChn chn=%d rc=%d", chn, ur_rc);
            hal_trace("--> [HAL] exit: IMP_Encoder_DestroyChn chn=%d", chn);
            int dc_rc = IMP_Encoder_DestroyChn(chn);
            hal_trace("<-- [HAL] exit: IMP_Encoder_DestroyChn chn=%d rc=%d", chn, dc_rc);
        }
        {
            std::lock_guard<std::mutex> lock(g_chn_mutex);
            g_chn_ref_count.clear();
        }
        // 4b. DestroyGroup（group 空，imp_encoder.h:921）
        std::vector<int> groups;
        {
            std::lock_guard<std::mutex> lock(g_group_mutex);
            for (const auto& kv : g_group_ref_count) {
                groups.push_back(kv.first);
            }
        }
        for (int grp : groups) {
            hal_trace("--> [HAL] exit: IMP_Encoder_DestroyGroup g=%d", grp);
            int dg_rc = IMP_Encoder_DestroyGroup(grp);
            hal_trace("<-- [HAL] exit: IMP_Encoder_DestroyGroup g=%d rc=%d", grp, dg_rc);
        }
        {
            std::lock_guard<std::mutex> lock(g_group_mutex);
            g_group_ref_count.clear();
        }
    }
    // 5. ISP/OSD teardown.
    if (ispOsdMgr_) {
        hal_trace("--> [HAL] exit: ispOsdMgr->exit()");
        ispOsdMgr_->exit();
        hal_trace("<-- [HAL] exit: ispOsdMgr->exit()");
        ispOsdMgr_.reset();
    }
    // 6. Destroy FrameSource channels, then IMP_System_Exit, then sensor/ISP.
    hal_trace("--> [HAL] exit: fsMgr.destroy() (IMP_FrameSource_DestroyChn per chn)");
    if (fsMgr.destroy() < 0) {
        hal_trace("<-- [HAL] exit: fsMgr.destroy() rc=-1 (FAILED)");
        return false;
    }
    hal_trace("<-- [HAL] exit: fsMgr.destroy() rc=0");
    hal_trace("--> [HAL] exit: IMP_System_Exit");
    int se_rc = IMP_System_Exit();
    hal_trace("<-- [HAL] exit: IMP_System_Exit rc=%d", se_rc);
    auto sensors = loadSensors();
    hal_trace("--> [HAL] exit: sensorMgr.disableAll (IMP_ISP_DisableSensor)");
    if (sensorMgr.disableAll(sensors) < 0) {
        hal_trace("<-- [HAL] exit: sensorMgr.disableAll rc=-1 (FAILED)");
        return false;
    }
    hal_trace("<-- [HAL] exit: sensorMgr.disableAll rc=0");
    hal_trace("--> [HAL] exit: sensorMgr.delAll (IMP_ISP_DelSensor)");
    if (sensorMgr.delAll(sensors) < 0) {
        hal_trace("<-- [HAL] exit: sensorMgr.delAll rc=-1 (FAILED)");
        return false;
    }
    hal_trace("<-- [HAL] exit: sensorMgr.delAll rc=0");
    hal_trace("--> [HAL] exit: IMP_ISP_DisableTuning");
	if(IMP_ISP_DisableTuning() < 0) {
        hal_trace("<-- [HAL] exit: IMP_ISP_DisableTuning rc<0 (FAILED)");
        return false;
    }
    hal_trace("<-- [HAL] exit: IMP_ISP_DisableTuning rc=0");
    hal_trace("--> [HAL] exit: sensorMgr.closeISP (IMP_ISP_DisableTuning + IMP_ISP_Close)");
    if (sensorMgr.closeISP() < 0) {
        hal_trace("<-- [HAL] exit: sensorMgr.closeISP rc=-1 (FAILED)");
        return false;
    }
    hal_trace("<-- [HAL] exit: sensorMgr.closeISP rc=0");
    if (getenv("HTC_NO_MULTIPROCESS")) {
        hal_trace("--> [HAL] exit: IMP_Encoder_MultiProcessExit SKIPPED (HTC_NO_MULTIPROCESS) <<<bisect>>>");
    } else {
        hal_trace("--> [HAL] exit: IMP_Encoder_MultiProcessExit");
        IMP_Encoder_MultiProcessExit();
        hal_trace("<-- [HAL] exit: IMP_Encoder_MultiProcessExit");
    }
    Logger::log(LogLevel::INFO, "[HAL] exit: teardown complete");
    hal_trace("==== [HAL] exit() COMPLETE ====");
    return true;
}
std::shared_ptr<IVideoStream> IngenicVideo::createVideoStream() {
    return std::make_shared<IngenicVideoStream>();
}
IngenicVideoControl::IngenicVideoControl() {}

IngenicVideoControl::~IngenicVideoControl() {}

bool IngenicVideoControl::getISPMode(ISPDaynightMode& state) {
    IMPISPRunningMode mode;
    IMPVI_NUM vinum = IMPVI_MAIN;
    if (IMP_ISP_Tuning_GetISPRunningMode(vinum, &mode) < 0) {
        return false;
    }
    state = (mode == IMPISP_RUNNING_MODE_DAY) ? ISPDaynightMode::DAY : ISPDaynightMode::NIGHT;
    return true;
}

bool IngenicVideoControl::setISPMode(ISPDaynightMode state) {
    IMPISPRunningMode mode;
    IMPVI_NUM vinum = IMPVI_MAIN;
    if (IMP_ISP_Tuning_GetISPRunningMode(vinum, &mode) < 0) {
        return false;
    }
    IMPISPRunningMode target = (state == ISPDaynightMode::DAY) ? IMPISP_RUNNING_MODE_DAY : IMPISP_RUNNING_MODE_NIGHT;
    if (mode == target) {
        return true;
    }
    return IMP_ISP_Tuning_SetISPRunningMode(vinum, &target) == 0;
}
}
