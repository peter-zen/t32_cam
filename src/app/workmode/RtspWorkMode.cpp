#include "RtspWorkMode.h"
#include "RtspServer.h"
#include "Logger.h"

#include <functional>

namespace app_workmode {

bool runRtspServerUntilSignal(uint16_t rtsp_port,
                              std::function<bool()> keepRunning,
                              std::function<void(int)> waitForSignal) {
    media::RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
        Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
    });
    media::RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
    if (!media::RtspServer::getInstance()->start()) {
        Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
        return false;  // caller does goto main_exit
    }
    /* RTSP 服务器持续运行，等待退出信号 */
    while (keepRunning()) {
        waitForSignal(1000);
    }
    media::RtspServer::getInstance()->stop();
    return true;
}

}  // namespace app_workmode
