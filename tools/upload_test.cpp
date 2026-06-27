// tools/upload_test.cpp
//
// upload_test —— upload 端到端最小独立 binary。
// 实例化 app_workmode::UploadWorker,根据传入的 jpg 路径现场造一个最小
// desc JSON(从 DeviceConfig 读 PID,或 --pid argv 注入),enqueue 后等 flush。
//
// 跑法 (T32):
//   LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH
//     /mnt/huntcam/bin/upload_test
//     /mnt/huntcam/DCIM/IMG_20260119_112056.jpg  [ms_addr] [ms_port] [timeout_s] [--pid <PID>]
//
// 跑法 (simu):
//     ./build_sim/bin/upload_test build/media/20260627_215438_1.jpg
//     # 自动从 <projectRoot>/res/config.sim.ini 读 PID
//
// 不实现:拍照/录影/编码/MP4 mux(那是 snap_test/record_task 的活)。
// 本 binary 只验证 "desc + file 被 upload worker 走到 connect → auth →
// uploadFile → 收 server ack"。
//
// 边界(critical):
//   - 设备上 daemon_app 会部署 /config/htc/config.ini + env.ini,产品代码
//     (MgmtServClient 等)通过 EnvManager::parsePrimaryEnv(env.ini) 拿到
//     CONFIG_FILE 路径。但 upload_test 是独立 binary,不一定继承 daemon
//     的 env 状态。
//   - 如果用户传 --pid,upload_test 会在 /tmp 生成一份"最小 config.ini"
//     (仅含 [DEVICE] PID=...),并 setEnv CONFIG_FILE 指向它,让
//     DeviceConfig 也能拿到 PID(UploadWorker 内部还会读 DeviceConfig
//     做 PID 校验)。这不是测试数据造假,是 DeviceConfig 单例的
//     fixture setup。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <chrono>
#include <memory>
#include <unistd.h>

#include <json/json.h>

#include "ElogInit.h"            // elog_init_default()
#include "Logger.h"              // Logger::log (被 UploadWorker 调)
#include "DeviceConfig.h"        // DeviceConfig::getInstance() — 读 [DEVICE] PID
#include "Settings.h"            // Settings::getInstance() — auth/upload 路径
#include "MCU.h"                 // MCU::getInstance()     — authenticate 调 readFirmwareVersion
#include "EnvManager.h"          // EnvManager  — DeviceConfig 内部读 CONFIG_FILE env
#include "misc/Misc.h"           // getFilename / getFileSize / isJsonFile
#include "Common.h"              // INI_SECTION_DEVICE, INI_KEY_PID (config key 字符串常量)
#include "upload_worker.h"       // app_workmode::UploadWorker

namespace {

// 落盘临时 desc 路径。UploadWorker 会按 isJsonFile 校验 + Json 解析 +
// PID 校验后,走 uploadFile(desc) + uploadFile(file_inf) 链路。
constexpr const char *kTempDescPath = "/tmp/upload_test_desc.json";

void emitAnchor(const char *k, const std::string &v) {
    std::printf("%s %s\n", k, v.c_str());
    std::fflush(stdout);
}

bool buildDesc(const std::string &jpgPath, const std::string &pid) {
    Json::Value desc;
    desc["F_UploadedTag"] = 0;
    desc["device"]["PID"] = pid;

    Json::Value fileItem;
    fileItem["F_FileName"] = Misc::getFilename(jpgPath);
    // F_FilePath 是父目录(UploadWorker 上传时会拼 F_FilePath/F_FileName)
    auto slash = jpgPath.find_last_of('/');
    fileItem["F_FilePath"] = (slash == std::string::npos) ? std::string(".") : jpgPath.substr(0, slash);
    fileItem["F_UploadedTag"] = 0;
    desc["file_inf"].append(fileItem);

    std::ofstream ofs(kTempDescPath);
    if (!ofs.is_open()) {
        std::fprintf(stderr, "upload_test: open %s for write failed\n", kTempDescPath);
        return false;
    }
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    ofs << Json::writeString(w, desc);
    ofs.close();
    return true;
}

int runUpload(const std::string &descPath, const std::string &msAddr, int msPort, int timeoutMs) {
    auto worker = std::make_shared<app_workmode::UploadWorker>();
    worker->start(msAddr, msPort);
    worker->enqueue(descPath);
    bool flushed = worker->flush(timeoutMs);
    worker->stop();
    return flushed ? 0 : 2;
}

}  // namespace

int main(int argc, char **argv) {
    // argv 解析:
    //   <file.jpg> [ms_addr] [ms_port] [timeout_s] [--pid <PID>]
    //   ms_addr/ms_port/timeout_s 位置参数
    //   --pid <PID>                显式注入设备 PID(可选)
    //
    // 注意 argv 顺序:位置参数先,再是 --pid 选项。这样不破坏现有调用:
    //   ./upload_test foo.jpg
    //   ./upload_test foo.jpg www.aidetcloud.com 8899 30 --pid T152T...
    std::string filePath;
    std::string msAddr = "www.aidetcloud.com";
    int msPort = 8899;
    int timeoutS = 30;
    std::string injectedPid;
    int posConsumed = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pid" && i + 1 < argc) {
            injectedPid = argv[++i];
        } else if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "upload_test: unknown flag %s\n", a.c_str());
            return 2;
        } else {
            // 位置参数按顺序消费
            switch (posConsumed) {
                case 0: filePath = a; break;
                case 1: msAddr = a; break;
                case 2: msPort = std::atoi(a.c_str()); break;
                case 3: timeoutS = std::atoi(a.c_str()); break;
                default:
                    std::fprintf(stderr, "upload_test: unexpected positional arg %s\n", a.c_str());
                    return 2;
            }
            ++posConsumed;
        }
    }
    if (filePath.empty()) {
        std::fprintf(stderr,
                     "usage: upload_test <file.jpg> [ms_addr] [ms_port] [timeout_s] [--pid <PID>]\n"
                     "  default ms_addr=www.aidetcloud.com  ms_port=8899  timeout_s=30\n"
                     "  --pid <PID>: explicitly inject device PID (overrides config.ini);\n"
                     "               on T32 w/o daemon_app env, this is required.\n");
        return 2;
    }

    // ---- S0: MCU bypass (devtest 边界保护) ----
    // upload 阶段的"边界"是:不触达硬件拍照/录影的副作用。MCU 在 ctor 会 open
    // /dev/hc32l13x,simu 端不存在会打 "Failed to open the iic bus" 噪音
    // (虽然 readFirmwareVersion 本身返常量不调 I2C)。强制绕开,让 upload_test
    // 不依赖任何硬件 init。
    //   注意:MgmtServClient::authenticate 把 FW_Version 塞进 auth JSON 是产品
    //   设计上的"边界错乱"——upload 阶段不该被它拖住。后续重构候选:把
    //   FW_Version 从 auth JSON 移到 WorkModeRunner cascade init 阶段缓存到
    //   DeviceConfig(S方案 A/C)。本测试 binary 不动产品代码,仅做"自包含"
    //   边界:用 HTC_NO_MCU 让 MCU ctor 直接 return。
    ::setenv("HTC_NO_MCU", "1", 0);  // 0=不覆盖已存在的 env

    // ---- S1: env (必须在 DeviceConfig::getInstance() 之前 — call_once 锁住) ----
    // 镜像 ProcessLifecycle::commonStartup S1 的 setEnvIfEmpty 模式:
    // 不覆盖用户传入的 env(T32 上 CONFIG_FILE 可能由 daemon_app 提前设好)。
    //
    // 平台分支:
    //   T32 (NFS 共享 /mnt/huntcam):默认 /mnt/huntcam/config.ini(build/ 根)
    //   simu (PC build_sim):沿 cwd 上溯找 <projectRoot>/res/config.sim.ini
    //
    // 区分信号:/mnt/huntcam/config.ini 文件存在 → T32 端;否则 → simu 端。
    // 这比 BUILD_FOR_SIMULATION 宏更鲁棒(测试 binary 不强制在 build 阶段注入宏)。
    //
    // --pid 注入路径:即使 config.ini 找不到,我们也写一份"最小 config.ini"
    // (仅含 [DEVICE] PID=)到 /tmp/,setEnv CONFIG_FILE 指向它,让
    // DeviceConfig 单例 init 成功(UploadWorker 内部仍会读 DeviceConfig
    // 做 PID 校验,见 upload_worker.cpp:147)。
    {
        auto env = EnvManager::getInstance();
        if (env->getEnv("CONFIG_FILE", "").empty()) {
            std::string cfg;
            std::ifstream t32Probe("/mnt/huntcam/config.ini");
            if (t32Probe.good()) {
                cfg = "/mnt/huntcam/config.ini";
            } else {
                // simu 端:沿 cwd 上溯,找第一个含 res/config.sim.ini 的目录
                char cwd[1024] = {0};
                std::string candidate;
                if (::getcwd(cwd, sizeof(cwd)) != nullptr) {
                    candidate = cwd;
                } else {
                    std::string exePath = argv[0] ? argv[0] : "";
                    candidate = exePath;
                }
                std::string projectRoot;
                while (!candidate.empty()) {
                    std::string probe = candidate + "/res/config.sim.ini";
                    std::ifstream test(probe);
                    if (test.good()) { projectRoot = candidate; break; }
                    auto slash = candidate.find_last_of('/');
                    if (slash == std::string::npos) break;
                    candidate = candidate.substr(0, slash);
                }
                if (!projectRoot.empty()) {
                    cfg = projectRoot + "/res/config.sim.ini";
                } else if (!injectedPid.empty()) {
                    // simu 端无 config.sim.ini 但有 --pid:落一份到 /tmp/
                    cfg = "/tmp/upload_test_config.ini";
                } else {
                    cfg = "res/config.sim.ini";  // 兜底,留给运行时 cwd 解析(可能仍 fail)
                }
            }
            env->setEnv("CONFIG_FILE", cfg);
        }
        if (env->getEnv("MOUNT_PATH", "").empty()) {
            env->setEnv("MOUNT_PATH", "/mnt/huntcam");
        }
    }

    // ---- S1.5: --pid 注入时,落一份最小 config.ini 让 DeviceConfig init 成功 ----
    if (!injectedPid.empty()) {
        // 用与 S1 一致的 setEnv 逻辑:用同一个 EnvManager 单例,读回
        // CONFIG_FILE(可能刚刚被 S1 写入了项目根的 config.sim.ini 路径)
        // —— 但当且仅当 S1 走到 "simu 端没 res/" 分支时,这里才会读到
        // /tmp/upload_test_config.ini。
        std::string cfgPath = EnvManager::getInstance()->getEnv("CONFIG_FILE", "");
        if (cfgPath.empty()) {
            cfgPath = "/tmp/upload_test_config.ini";
            EnvManager::getInstance()->setEnv("CONFIG_FILE", cfgPath);
        }
        std::ofstream f(cfgPath);
        if (!f.is_open()) {
            std::fprintf(stderr, "upload_test: open %s for write failed (--pid mode)\n", cfgPath.c_str());
            return 3;
        }
        f << "[DEVICE]\n";
        f << "PID=" << injectedPid << "\n";
        f.close();
    }

    // ---- S1: logger (单例 init 会 log) ----
    elog_init_default();

    // ---- S2: 单例预热 ----
    auto devCfg = DeviceConfig::getInstance();
    auto settings = Settings::getInstance();
    auto mcu = MCU::getInstance();
    (void)devCfg; (void)settings; (void)mcu;  // 持有 shared_ptr,确保 ctor 跑完

    // PID 优先级:--pid argv > config.ini[DEVICE] PID
    // --pid 路径下 S1.5 已经把 PID 落到了 config.ini,DeviceConfig 也能读;
    // 此处 argv 优先保证 desc 用的 PID 与调用方一致(防止 config.ini 内容
    // 与 --pid 不一致的歧义)。
    std::string pid = !injectedPid.empty()
        ? injectedPid
        : devCfg->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
    if (pid.empty()) {
        std::fprintf(stderr,
            "upload_test: PID empty. Pass --pid <PID> on T32 without daemon_app env, "
            "or ensure config.ini has [DEVICE] PID=<value>.\n");
        return 3;
    }

    // ---- S2: 造 desc + anchor ----
    if (!buildDesc(filePath, pid)) {
        return 3;
    }
    // T32 uclibc 工具链的 <string> 不带 std::to_string,<cstdio> 的 snprintf 也不在 std::
    // (与 json_stress_test.cpp:42 同样的 uclibc 限制)。用裸 snprintf 兜底。
    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%d", msPort);
    emitAnchor("mgmtServerAddr:", msAddr + ":" + portBuf);
    emitAnchor("desc_path:", kTempDescPath);
    emitAnchor("source_file:", filePath);
    emitAnchor("device_pid:", pid);

    // ---- S3: 跑 upload ----
    auto t0 = std::chrono::steady_clock::now();
    int rc = runUpload(kTempDescPath, msAddr, msPort, timeoutS * 1000);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // ---- S4: 收尾 anchor ----
    std::printf("upload_test: flush_done=%s rc=%d elapsed_ms=%lld\n",
                (rc == 0) ? "true" : "false", rc, (long long)ms);
    std::fflush(stdout);

    // 清理临时 desc(不影响下次跑,/tmp 重启清空)
    ::unlink(kTempDescPath);
    return rc;
}
