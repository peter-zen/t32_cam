# PC Simulation Toolchain Configuration
# 用于PC环境下的模拟编译，使用系统原生编译器
#
# 使用方法:
#   mkdir -p build_sim && cd build_sim
#   cmake -DBUILD_FOR_SIMULATION=ON ..
#   make -j$(nproc)

# 不设置CMAKE_SYSTEM_NAME，让CMake使用本地编译器
# 这样CMake会自动检测并使用系统的gcc/g++

# PC编译优化选项 - 保留调试信息便于开发调试
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -ffunction-sections -fdata-sections")

# 链接器选项 - 移除未使用的代码段
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--gc-sections")

# 定义模拟模式宏
add_definitions(-DSIMULATION_MODE)

message(STATUS "PC Simulation Toolchain: Using native compiler for x86_64")
