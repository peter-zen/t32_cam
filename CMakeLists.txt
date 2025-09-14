cmake_minimum_required(VERSION 3.16)

# Specify the toolchain file
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")

# Project name and version
project(htc_firmware VERSION 1.0)

# Set default definitions
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Path to SDK")
set(SDK_PATH "${PROJECT_ROOT}/sdk" CACHE PATH "Path to SDK")
if(USE_UCLIBC)
set(SDK_LIB_PATH "${SDK_PATH}/lib/uclibc" CACHE PATH "Path to HI libraries")
else()
set(SDK_LIB_PATH "${SDK_PATH}/lib/glibc" CACHE PATH "Path to HI libraries")
endif()
set(SDK_INC_PATH "${SDK_PATH}/include" CACHE PATH "Relative include path")
set(SOURCES "${PROJECT_ROOT}/src" CACHE STRING "Source files for the project")
set(THIRD_PARTY_PATH "${PROJECT_ROOT}/third_party" CACHE STRING "Third party libraries")

#Set options
add_definitions(-DRTC_EXIST=0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

add_definitions(
    -O2
    -Wall
    -march=mips32r2
)

# Include subdirectory for common
add_subdirectory(src)
add_subdirectory(third_party)