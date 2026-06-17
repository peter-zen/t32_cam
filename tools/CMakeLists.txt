# tools/CMakeLists.txt
#
# 诊断/离线工具集合。产物不随固件主程序部署，仅用于现场排障与回归验证。
# 两平台都构建（不门控 BUILD_FOR_SIMULATION），满足 CLAUDE.md 双平台编译硬约束：
#   - T32 build/      : 交叉编译，真机运行
#   - PC  build_sim/  : I2C bypass，验证编译/表枚举完整/调用路径不崩

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

