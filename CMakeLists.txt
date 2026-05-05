cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Toolchain Selection - 必须在project()之前设置
# ============================================================================
# BUILD_FOR_SIMULATION选项必须通过cmake命令行传入:
#   PC模拟编译:  cmake -DBUILD_FOR_SIMULATION=ON ..
#   真机交叉编译: cmake ..
# ============================================================================
option(BUILD_FOR_SIMULATION "Build for PC simulation instead of target hardware" OFF)

if(BUILD_FOR_SIMULATION)
    message(STATUS "=== Configuring for PC Simulation (x86_64) ===")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_sim.cmake")
else()
    message(STATUS "=== Configuring for Target Hardware (Ingenic T32 MIPS) ===")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()

# Project name and version
project(htc_firmware VERSION 1.0)

# Set default definitions
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Path to SDK")
set(SDK_PATH "${PROJECT_ROOT}/sdk" CACHE PATH "Path to SDK")
if(USE_UCLIBC)
set(HTC_SDK_LIB_SUBDIR "uclibc")
else()
set(HTC_SDK_LIB_SUBDIR "glibc")
endif()
set(SDK_LIB_PATH "${SDK_PATH}/lib/${HTC_SDK_LIB_SUBDIR}" CACHE PATH "Path to HI libraries")
set(SDK_INC_PATH "${SDK_PATH}/include" CACHE PATH "Relative include path")
set(SOURCES "${PROJECT_ROOT}/src" CACHE STRING "Source files for the project")
set(THIRD_PARTY_PATH "${PROJECT_ROOT}/third_party" CACHE STRING "Third party libraries")

# Keep build-tree binaries runnable after workspace renames by avoiding
# hard-coded absolute paths to in-tree shared libraries.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_BUILD_RPATH
    "\$ORIGIN"
    "\$ORIGIN/../lib"
    "\$ORIGIN/../../sdk/lib/${HTC_SDK_LIB_SUBDIR}"
)

#Revision info
# Only set default values if the macros are not already defined externally
if(NOT DEFINED CAMERA_VERSION)
    add_definitions(-DCAMERA_VERSION="V0.0.1")
endif()
if(NOT DEFINED CAMERA_BUILD_TIME)
    add_definitions(-DCAMERA_BUILD_TIME="20251117")
endif()

#Set options
add_definitions(-DRTC_EXIST=1)
add_definitions(-DMCU_EXIST=1)
add_definitions(-DDAEMON_ENABLE=0)
add_definitions(-DUVC_ENABLE=0)
add_definitions(-DALL_MEDIA_FILE_IN_ONE_FOLDER=1)
# set sensor type
#add_definitions(-DSENSOR_TYPE_SC4366P)
add_definitions(-DSENSOR_TYPE_GC4653)

# set WiFi type
#add_definitions(-DWIFI_TYPE_CYW43012)
add_definitions(-DWIFI_TYPE_RTL8189FS)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# ============================================================================
# Platform-specific compiler flags
# ============================================================================
if(NOT BUILD_FOR_SIMULATION)
    # 真机编译: 添加MIPS特定选项
    add_definitions(
        -O2
        -Wall
        -march=mips32r2
        -mmxu2
        -flax-vector-conversions
    )
else()
    # PC模拟编译: 使用通用选项，并定义BUILD_FOR_SIMULATION宏
    add_definitions(
        -O2
        -Wall
        -DBUILD_FOR_SIMULATION
    )
endif()

# Include subdirectory for common
add_subdirectory(src)
add_subdirectory(third_party)
if(BUILD_FOR_SIMULATION)
    add_subdirectory(tests)
endif()
