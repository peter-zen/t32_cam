# SDK Stub Library for PC Simulation
# 提供SDK库的空实现，用于PC环境下的模拟编译
#
# 仅在 BUILD_FOR_SIMULATION=ON 时编译

# 源文件
set(SDK_STUB_SOURCES
    system_call_stub.c
    #imp_stub.c
    #alog_stub.c
)

# 创建静态库
add_library(sdk_stub STATIC ${SDK_STUB_SOURCES})

# 设置位置无关代码，以便静态库可以被链接到共享库中
set_target_properties(sdk_stub PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 包含SDK头文件目录
target_include_directories(sdk_stub PUBLIC
    ${SDK_INC_PATH}
    ${SDK_INC_PATH}/imp
    ${SDK_INC_PATH}/systemcall
)

# 包含 HAL 头文件目录
target_include_directories(sdk_stub PRIVATE
)

#

# 定义模拟模式宏
target_compile_definitions(sdk_stub PRIVATE SIMULATION_MODE)

message(STATUS "SDK Stub: Building stub implementations for PC simulation")
