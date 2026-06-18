#pragma once

#include <cstdint>
#include <functional>

// Lifted RTSP server start/run/stop sequence for the CMD_RTSP_SERVER work mode.
//
// main_app's signal/exit-flow machinery (already_in_exit_flow,
// waitForSignalOrTimeout, g_signal_pipe, performCleanup) is TU-private and
// intentionally NOT moved. The run-loop's two dependencies on that machinery
// are injected by the caller so the helper stays decoupled.
namespace app_workmode {

// Register the session-closed callback, set the port, and start the RTSP
// singleton, then block on the caller-supplied wait until keepRunning()
// returns false, then stop().
//
//   rtsp_port       — caller-resolved (main_app::getConfiguredPort).
//   keepRunning     — returns true while the run-loop should continue
//                     (caller wraps its private !already_in_exit_flow).
//   waitForSignal   — blocks up to the given ms for a signal
//                     (caller wraps its private waitForSignalOrTimeout).
//
// Returns false if start() fails (caller does `goto main_exit`), true after a
// clean run-loop exit and stop().
//
// NOTE: the helper does NOT set rtsp_singleton_used — that gate (read at
// main_exit to drive RtspServer::getInstance()->shutdown()) stays caller-side.
bool runRtspServerUntilSignal(uint16_t rtsp_port,
                              std::function<bool()> keepRunning,
                              std::function<void(int)> waitForSignal);

}  // namespace app_workmode
