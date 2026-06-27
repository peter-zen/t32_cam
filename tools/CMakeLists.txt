# tools/CMakeLists.txt
#
# 诊断/离线工具集合。产物不随固件主程序部署，仅用于现场排障与回归验证。
# 两平台都构建（不门控 BUILD_FOR_SIMULATION），满足 CLAUDE.md 双平台编译硬约束：
#   - T32 build/      : 交叉编译，真机运行
#   - PC  build_sim/  : I2C bypass，验证编译/表枚举完整/调用路径不崩

# T32 端 SDK 库(libsystem_call / libimp / ...)统一在 sdk/lib/uclibc/。
# src/CMakeLists.txt 和 src/app/CMakeLists.txt 都有 link_directories(${SDK_LIB_PATH}),
# tools/ 目录独立 CMakeLists,需自己加上,否则 -lsystem_call 找不到。
# sim 端用 sdk_stub 替代 SDK 库(Static lib),不需要 -L。
if(NOT BUILD_FOR_SIMULATION)
    link_directories(${SDK_LIB_PATH})
endif()

add_executable(htc_mcu_api_test mcu_api_test.cpp)

# mcu 库 (src/hardware/mcu) 已封装 MCU.cpp + IIC.cpp，并 PUBLIC link logger；
# MCU.cpp 用到的 StringConvert.h / Common.h / RTC.h 均为 header-only（宏/inline），
# 故 link mcu 即可（transitively 带 logger/easylogger）。
target_link_libraries(htc_mcu_api_test
    PRIVATE
    mcu
    logger
    easylogger
    pthread
)

target_include_directories(htc_mcu_api_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/common/utils/string
    ${CMAKE_SOURCE_DIR}/src/common/time/rtc
    ${CMAKE_SOURCE_DIR}/src/logger
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

# 输出到统一 bin/ 目录（与主程序 htc_main_app 等一致，便于 NFS 共享到 T32）
set_target_properties(htc_mcu_api_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ----------------------------------------------------------------------------
# json_stress_test（既有工具，commit d04f46c 加入；本目录首次接入 CMake，一并纳入）
# 模拟 generateDescInfo 的 jsoncpp 路径压力测试，验证 zram 行为。纯用户态，无 SDK/HAL。
# ----------------------------------------------------------------------------
add_executable(json_stress_test json_stress_test.cpp)

target_link_libraries(json_stress_test
    PRIVATE
    jsoncpp
    pthread
)

target_include_directories(json_stress_test PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

set_target_properties(json_stress_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ----------------------------------------------------------------------------
# upload_test —— upload 端到端最小独立 binary。
# 喂入 jpg 路径(内部造最小 desc JSON),实例化 app_workmode::UploadWorker,
# 验证 connect + auth + uploadFile 链路不崩/不卡。
#
# 链接策略:不 link `app_workmode` 库 —— 它 PUBLIC-link 了 media_snap /
# media_recorder / audio_recorder,这些 TU 直接调 Ingenic SDK(IMP/alog/
# sysutils),整库传递地引入 -limp -lalog -lsysutils 等 undefined symbols,
# 链接器会因这些 SDK 库在本机不提供而失败。upload_test 根本不走 SDK。
#
# 改为:直接编译产品 UploadWorker TU。UploadWorker 本身只依赖 network/json/
# config/misc/logger,不包含 capture/media/HAL。这样 upload_test 是同一上传
# 功能的薄测试壳,不会和 wm 的产品 worker 漂移。
# ----------------------------------------------------------------------------
add_executable(upload_test
    upload_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workmode/upload_worker.cpp
)

target_link_libraries(upload_test
    PRIVATE
    # 镜像 htc_net_app T32 link 闭包(同样调 network 但不 link app_workmode):
    # 1) 业务/单例/工具 — 直接对应 upload 路径
    network                   # MgmtServClient + StorageServClient (PRIVATE 传: mcu/setting/env/devconf/disk/power/jsoncpp/md5/common_misc/...)
    logger                    # Logger::log
    devconf                   # DeviceConfig::getInstance
    setting                   # Settings::getInstance
    env                       # EnvManager::getInstance
    mcu                       # MCU::getInstance (authenticate 调 readFirmwareVersion)
    common_misc               # Misc::getFilename / isJsonFile / deleteFile
    common_time_timezone      # Timezone(网络库 PRIVATE 链)
    jsoncpp                   # json/json.h
    # 2) network → StorageServClient.cpp 调 CRC::calculate_crc16(src/common/utils/crc/CRC.cpp),
    #    network 库本身没链 common_utils_crc(latent missing-dep,见 common/utils/crc/CMakeLists 注释),
    #    final link 时需显式补;镜像 htc_net_app 的 link 闭包。
    common_utils_crc
    crc16
    # 3) 线程/时钟
    pthread
    rt
)

# 3) HW SDK 闭包(common_misc 内部调 system_call; sim 用 sdk_stub 替代)
#    tools/CMakeLists.txt 顶部已加 link_directories(${SDK_LIB_PATH})(仅 T32)
#    cmake 的 target_link_libraries 不支持 inline if(),所以单独 add。
if(BUILD_FOR_SIMULATION)
    target_link_libraries(upload_test PRIVATE sdk_stub)
else()
    target_link_libraries(upload_test PRIVATE system_call)
endif()

target_include_directories(upload_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app/workmode          # upload_worker.h(原源所在目录,shim 编译时按原始相对路径查找)
    ${CMAKE_SOURCE_DIR}/src/logger
    ${CMAKE_SOURCE_DIR}/src/config/devconf
    ${CMAKE_SOURCE_DIR}/src/config/setting
    ${CMAKE_SOURCE_DIR}/src/config/env
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${CMAKE_SOURCE_DIR}/src/hardware
    ${CMAKE_SOURCE_DIR}/src/hardware/power
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/common/misc
    ${CMAKE_SOURCE_DIR}/src/common/utils/string
    ${CMAKE_SOURCE_DIR}/src/common/utils/base64
    ${CMAKE_SOURCE_DIR}/src/common/utils/serial
    ${CMAKE_SOURCE_DIR}/src/common/time/rtc
    ${CMAKE_SOURCE_DIR}/src/common/time/timezone
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
    ${THIRD_PARTY_PATH}/md5/include
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

set_target_properties(upload_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
