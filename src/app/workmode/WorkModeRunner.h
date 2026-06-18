#pragma once

#include <memory>
#include "WorkMode.h"   // enum workingMode (runWorkMode's parameter)

namespace network { class MgmtServClient; }
namespace network { class StorageServClient; }

namespace app_lifecycle { class ProcessLifecycle; }

// Command-bitmask vocabulary shared by main_app's dispatch and the moved
// cascade. Single-sourced here so the `-wm` switch (runWorkMode) and the
// single-shot dispatch (main_app) agree with runCommands.
#define CMD_HELP 0
#define CMD_CONN_NET (1 << 0)
#define CMD_DHCP (1 << 1)
#define CMD_SNAP (1 << 2)
#define CMD_AUDIO_RECORD (1 << 3)
#define CMD_VIDEO_RECORD (1 << 4)
#define CMD_AUTH (1 << 5)
#define CMD_HEARTBEAT (1 << 6)
#define CMD_UPLOAD (1 << 7)
#define CMD_MOBILE (1 << 8)
#define CMD_RTSP_SERVER (1 << 9)
#define CMD_NTP (1 << 10)
#define CMD_GET_RTC (1 << 11)
#define CMD_SET_RTC (1 << 12)

namespace app_workmode {

// Bundle of the cascade's TU-local dependencies (the symbols that are NOT
// already reachable through ctx.lc.* or global singletons). Extracted verbatim
// from main_app.cpp; see T15-planner-full.md §3.1 for the per-field rationale.
struct WorkModeContext {
    app_lifecycle::ProcessLifecycle& lc;
    bool        isRtcWorkWell     = true;   // -rtc arg
    bool        mobileRtspEnabled = true;   // -m --no-rtsp / default true
    bool        rtspAudioEnabled  = true;   // set in dispatch, not currently read by the cascade
    int         argc              = 0;      // only for CMD_SET_RTC's argv[2]
    char**      argv              = nullptr;
    // BY REFERENCE: the cleanupHook (still in main_app) nulls these same
    // instances, so the ctx must share the caller's shared_ptr, not a copy.
    std::shared_ptr<network::MgmtServClient>&   mgmtServClient;
    std::shared_ptr<network::StorageServClient>& storageServClient;
};

// Result of runCommands/runWorkMode. The original main() had three outcomes:
//   (1) cascade completes normally → the main_exit tail runs;
//   (2) any in-cascade `goto main_exit` → the main_exit tail runs;
//   (3) invalid -wm mode → `return -1` from main, tail SKIPPED.
// Continue covers (1) and (2); the tail runs unconditionally. TerminalExit
// covers (3) only — the caller does `return -1` (skip the tail), matching today.
enum class CascadeResult { Continue, TerminalExit };

// The WHOLE if(command & CMD_*) cascade, verbatim from main_app.cpp. Every
// ex-`goto main_exit` site returns Continue (the caller then runs the tail);
// normal completion returns Continue. runCommands never produces TerminalExit.
CascadeResult runCommands(int command, WorkModeContext& ctx);

// The switch(working_mode)→command-bitmap map (verbatim) + runCommands. The
// invalid-mode `default:` returns TerminalExit (the caller does `return -1`,
// skipping the tail, exactly as today's `return -1` from main).
CascadeResult runWorkMode(enum workingMode mode, WorkModeContext& ctx);

}  // namespace app_workmode
