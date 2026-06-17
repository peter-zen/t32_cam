# Include directories
include_directories(
    ${SDK_INC_PATH}
    ${SOURCES}/media/common
    ${SOURCES}/media/snap
    ${THIRD_PARTY_PATH}/easylogger/inc
)

# Specify the directory where the libraries are located
link_directories(
    ${SDK_LIB_PATH}
    ${CMAKE_BINARY_DIR}/lib
)

# ============================================================================
# SDK Stub for PC Simulation
# ============================================================================
if(BUILD_FOR_SIMULATION)
endif()

# Ensure static libraries are preferred
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Include subdirectory for common
add_subdirectory(logger)
add_subdirectory(common)

# HAL (Hardware Abstraction Layer)
add_subdirectory(hal)
add_subdirectory(config)
add_subdirectory(media)
add_subdirectory(network)
add_subdirectory(hardware)
add_subdirectory(platform)
add_subdirectory(service)
add_subdirectory(storage)
add_subdirectory(manifest)

add_subdirectory(app)

