#pragma once
// Fixed runtime config paths — formerly env.ini keys (design
// config-ini-to-json-migration.md §10). HW paths live under /config/htc
// (jffs2 persistent); sim paths anchor under ./res and the sim SD root.
//
// Pure C++ header. NOT merged into Common.h — that is a C header (extern "C")
// and these are C++ constants. Consumers use setEnvIfEmpty to inject these
// defaults into EnvManager at bootstrap; DeviceConfig/ProductConfig/Settings
// read via getEnv("CONFIG_FILE"/"PRODUCT_FILE"/"SETTING_FILE_PATH") and remain
// unchanged.
//
// C++14 note: namespace-scope constexpr has internal linkage (like static
// constexpr), so each TU gets its own copy — legal in a header, no ODR issue.

#ifdef BUILD_FOR_SIMULATION
// sim: productRootPath + simRootPath are prepended by the caller (ProcessLifecycle
// commonStartup uses projectRootPath/simRootPath), so these constants only cover
// the fixed leaf portions when used as bare defaults. The sim bootstrap composes
// projectRoot + "/res/..." inline (see ProcessLifecycle.cpp S1).
constexpr const char* kProductFilePath         = "./res/product.sim.json";
constexpr const char* kSettingFilePath         = "./res/setting.json";
constexpr const char* kSystemFilePath          = "./res/system.sim.json";
constexpr const char* kBroadcastFilelistPath   = "./sim_sdcard_runtime/media/audio/AUDIO_PLAY_LIST.txt";
constexpr const char* kBroadcastFileDir        = "./sim_sdcard_runtime/media/audio/";
constexpr const char* kIspFileDir              = "./sim_sdcard_runtime/media/audio/";
#else
constexpr const char* kProductFilePath         = "/config/htc/product.json";
constexpr const char* kSettingFilePath         = "/config/htc/setting.json";
constexpr const char* kSystemFilePath          = "/config/htc/system.json";
constexpr const char* kBroadcastFilelistPath   = "/mnt/sdcard/media/audio/AUDIO_PLAY_LIST.txt";
constexpr const char* kBroadcastFileDir        = "/mnt/sdcard/media/audio/";
constexpr const char* kIspFileDir              = "/mnt/sdcard/media/audio/";
#endif
